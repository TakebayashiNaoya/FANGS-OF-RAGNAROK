#!/usr/bin/env python3
"""DDS の組み立てと texconv 呼び出し。アセット生成スクリプトが import して使う。

単体では何もしない。GenerateTerrainAssets.py と GenerateStageGltf.py が
同じ DDS を書けるよう、書き出しの手順をここ 1 か所に置いてある。

DDS の数え方（ミップの行のバイト数とブロック単位の端数）は
Engine/Resource/DdsImage.cpp の CalculateMipLayout と対。片方だけ変えない。
"""

import math
import os
import shutil
import struct
import subprocess
import tempfile
import zlib


# --------------------------------------------------------------------------------------
# DDS
# --------------------------------------------------------------------------------------

DDS_MAGIC = b"DDS "
DDS_HEADER_BYTE_SIZE = 124
DDS_HEADER_DX10_BYTE_SIZE = 20
DDS_DATA_OFFSET = len(DDS_MAGIC) + DDS_HEADER_BYTE_SIZE + DDS_HEADER_DX10_BYTE_SIZE

DDSD_CAPS = 0x00000001
DDSD_HEIGHT = 0x00000002
DDSD_WIDTH = 0x00000004
DDSD_PITCH = 0x00000008
DDSD_PIXELFORMAT = 0x00001000
DDSD_MIPMAPCOUNT = 0x00020000
DDSD_LINEARSIZE = 0x00080000

DDPF_FOURCC = 0x00000004
FOURCC_DX10 = 0x30315844

DDSCAPS_COMPLEX = 0x00000008
DDSCAPS_TEXTURE = 0x00001000
DDSCAPS_MIPMAP = 0x00400000

DDS_DIMENSION_TEXTURE2D = 3

DXGI_FORMAT_R8G8B8A8_UNORM = 28
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29
DXGI_FORMAT_R16_UNORM = 56
DXGI_FORMAT_BC5_UNORM = 83
DXGI_FORMAT_BC7_UNORM_SRGB = 99

FORMAT_NAMES = {
    DXGI_FORMAT_R8G8B8A8_UNORM: "R8G8B8A8_UNORM",
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: "R8G8B8A8_UNORM_SRGB",
    DXGI_FORMAT_R16_UNORM: "R16_UNORM",
    DXGI_FORMAT_BC5_UNORM: "BC5_UNORM",
    DXGI_FORMAT_BC7_UNORM_SRGB: "BC7_UNORM_SRGB",
}

# ブロック圧縮でない形式の 1 テクセルあたりのバイト数
TEXEL_BYTE_SIZES = {
    DXGI_FORMAT_R8G8B8A8_UNORM: 4,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: 4,
    DXGI_FORMAT_R16_UNORM: 2,
}

BLOCK_TEXEL_SIZE = 4

# BC5 も BC7 もブロック 1 個は 16 バイト
BLOCK_BYTE_SIZE = 16

BLOCK_COMPRESSED_FORMATS = (DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB)


def IsBlockCompressed(dxgi_format):
    """ブロック圧縮の形式なら True。"""
    return dxgi_format in BLOCK_COMPRESSED_FORMATS


def CalculateMipByteSize(dxgi_format, width, height):
    """ミップ 1 段ぶんのバイト数。"""
    if IsBlockCompressed(dxgi_format):
        block_count_x = max((width + BLOCK_TEXEL_SIZE - 1) // BLOCK_TEXEL_SIZE, 1)
        block_count_y = max((height + BLOCK_TEXEL_SIZE - 1) // BLOCK_TEXEL_SIZE, 1)
        return block_count_x * block_count_y * BLOCK_BYTE_SIZE

    return width * height * TEXEL_BYTE_SIZES[dxgi_format]


def BuildDdsBytes(dxgi_format, width, height, mip_chain):
    """DX10 拡張ヘッダ付きの DDS を組み立てる。

    エンジンは DX10 拡張ヘッダ付きしか読まないので、旧形式は書かない。
    """
    mip_count = len(mip_chain)

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    caps = DDSCAPS_TEXTURE
    if mip_count > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP

    if IsBlockCompressed(dxgi_format):
        flags |= DDSD_LINEARSIZE
        pitch_or_linear_size = CalculateMipByteSize(dxgi_format, width, height)
    else:
        flags |= DDSD_PITCH
        pitch_or_linear_size = width * TEXEL_BYTE_SIZES[dxgi_format]

    header = struct.pack(
        "<7I11I8I5I",
        DDS_HEADER_BYTE_SIZE,
        flags,
        height,
        width,
        pitch_or_linear_size,
        0,
        mip_count,
        *([0] * 11),
        32,
        DDPF_FOURCC,
        FOURCC_DX10,
        0,
        0,
        0,
        0,
        0,
        caps,
        0,
        0,
        0,
        0,
    )

    header_dx10 = struct.pack(
        "<5I", dxgi_format, DDS_DIMENSION_TEXTURE2D, 0, 1, 0)

    return DDS_MAGIC + header + header_dx10 + b"".join(mip_chain)


def WriteDdsFile(path, dxgi_format, width, height, mip_chain):
    """DDS を 1 枚書き出す。"""
    with open(path, "wb") as output_file:
        output_file.write(BuildDdsBytes(dxgi_format, width, height, mip_chain))


def VerifyDdsFile(path, dxgi_format, width, height, mip_count):
    """書いた DDS を読み戻し、エンジンが読める形かを確かめる。

    戻り値は (真偽, 説明)。texconv が焼いたファイルもここを通す。
    """
    with open(path, "rb") as input_file:
        file_bytes = input_file.read()

    if len(file_bytes) < DDS_DATA_OFFSET:
        return False, "ヘッダが足りない（%d バイト）" % len(file_bytes)

    if file_bytes[:4] != DDS_MAGIC:
        return False, "magic が違う"

    header = struct.unpack_from("<31I", file_bytes, 4)
    if header[0] != DDS_HEADER_BYTE_SIZE:
        return False, "ヘッダの大きさが %d" % header[0]

    if header[20] != FOURCC_DX10:
        return False, "DX10 拡張ヘッダが無い"

    header_dx10 = struct.unpack_from(
        "<5I", file_bytes, 4 + DDS_HEADER_BYTE_SIZE)
    if header_dx10[0] != dxgi_format:
        return False, "DXGI_FORMAT が %d" % header_dx10[0]

    if header_dx10[1] != DDS_DIMENSION_TEXTURE2D:
        return False, "2D テクスチャではない"

    if header[3] != width or header[2] != height:
        return False, "寸法が %dx%d" % (header[3], header[2])

    stored_mip_count = header[6] if header[6] > 0 else 1
    if stored_mip_count != mip_count:
        return False, "ミップ段数が %d" % stored_mip_count

    expected_size = DDS_DATA_OFFSET
    mip_width = width
    mip_height = height
    for _ in range(mip_count):
        expected_size += CalculateMipByteSize(dxgi_format, mip_width, mip_height)
        mip_width = max(mip_width // 2, 1)
        mip_height = max(mip_height // 2, 1)

    if len(file_bytes) < expected_size:
        return False, "中身が %d バイト足りない" % (expected_size - len(file_bytes))

    return True, "%s %dx%d ミップ %d 段 %d バイト" % (
        FORMAT_NAMES[dxgi_format], width, height, mip_count, len(file_bytes))


# --------------------------------------------------------------------------------------
# ミップ
# --------------------------------------------------------------------------------------

def DownsampleByBox(width, height, pixels):
    """RGBA8 を 2x2 のボックスフィルタで半分に縮める。"""
    next_width = max(width // 2, 1)
    next_height = max(height // 2, 1)

    downsampled = bytearray(next_width * next_height * 4)
    for y in range(next_height):
        top = min(y * 2, height - 1)
        bottom = min(top + 1, height - 1)
        for x in range(next_width):
            left = min(x * 2, width - 1)
            right = min(left + 1, width - 1)

            for channel in range(4):
                total = (pixels[(top * width + left) * 4 + channel]
                         + pixels[(top * width + right) * 4 + channel]
                         + pixels[(bottom * width + left) * 4 + channel]
                         + pixels[(bottom * width + right) * 4 + channel])
                downsampled[(y * next_width + x) * 4 + channel] = (total + 2) // 4

    return bytes(downsampled)


def BuildMipChain(width, height, pixels):
    """1x1 まで縮めたミップの並びを返す。"""
    chain = [pixels]
    while width > 1 or height > 1:
        pixels = DownsampleByBox(width, height, pixels)
        width = max(width // 2, 1)
        height = max(height // 2, 1)
        chain.append(pixels)

    return chain


# --------------------------------------------------------------------------------------
# 法線マップ
# --------------------------------------------------------------------------------------

def BuildNormalMapPixels(resolution, heights, strength):
    """高さの並び（0〜1 の float、行優先）から接線空間の法線を RGBA8 で作る。

    中央差分で傾きを出し、正規化した法線を 0〜1 へ写す。端は反対側へ回り込んで
    読む ➡ タイリングしても継ぎ目が出ない。

    接線空間の規約は glTF に合わせて 従法線 = ∂P/∂v。
    ∂P/∂u = (1, 0, dh/du)、∂P/∂v = (0, 1, dh/dv) の外積が (-dh/du, -dh/dv, 1) なので、
    赤と緑には傾きの符号を反転して入れる。青は残りから決まるが、
    シェーダは読まないので参考の値として書く（BC5 では捨てられる）。

    strength は傾きに掛ける倍率。大きいほど凹凸が強く見える。
    """
    pixels = bytearray()
    for y in range(resolution):
        up = (y - 1) % resolution
        down = (y + 1) % resolution
        for x in range(resolution):
            left = (x - 1) % resolution
            right = (x + 1) % resolution

            slope_u = (heights[y * resolution + right] - heights[y * resolution + left]) * 0.5 * strength
            slope_v = (heights[down * resolution + x] - heights[up * resolution + x]) * 0.5 * strength

            length = math.sqrt(slope_u * slope_u + slope_v * slope_v + 1.0)
            normal_x = -slope_u / length
            normal_y = -slope_v / length
            normal_z = 1.0 / length

            pixels.append(EncodeNormalChannel(normal_x))
            pixels.append(EncodeNormalChannel(normal_y))
            pixels.append(EncodeNormalChannel(normal_z))
            pixels.append(255)

    return bytes(pixels)


def EncodeNormalChannel(value):
    """-1〜1 の成分を 0〜255 へ写す。"""
    encoded = int(round((value * 0.5 + 0.5) * 255.0))
    return max(0, min(255, encoded))


def SummarizeNormalMapTilt(pixels):
    """法線マップの傾き（真上からの角度）の平均と最大を度で返す。

    焼いた強さが狙いどおりかを数字で見るために使う。青（z）から角度を出すので、
    BC5 へ落とす前の RGBA8 の画素を渡すこと。
    """
    total_degrees = 0.0
    highest_degrees = 0.0
    texel_count = len(pixels) // 4

    for index in range(texel_count):
        normal_z = pixels[index * 4 + 2] / 255.0 * 2.0 - 1.0
        degrees = math.degrees(math.acos(max(-1.0, min(1.0, normal_z))))

        total_degrees += degrees
        highest_degrees = max(highest_degrees, degrees)

    return total_degrees / max(texel_count, 1), highest_degrees


# --------------------------------------------------------------------------------------
# PNG と texconv
# --------------------------------------------------------------------------------------

def WritePngFile(path, width, height, pixels):
    """texconv に渡すための 8 bit RGBA の PNG を書く。"""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # フィルタ種別。掛けない
        raw += pixels[y * width * 4:(y + 1) * width * 4]

    def BuildChunk(chunk_type, chunk_body):
        body = chunk_type + chunk_body
        return struct.pack(">I", len(chunk_body)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0)

    with open(path, "wb") as output_file:
        output_file.write(b"\x89PNG\r\n\x1a\n")
        output_file.write(BuildChunk(b"IHDR", header))
        output_file.write(BuildChunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        output_file.write(BuildChunk(b"IEND", b""))


def FindTexconvPath():
    """texconv.exe を探す。見つからなければ None。"""
    found = shutil.which("texconv")
    if found is not None:
        return found

    candidates = (
        os.path.expanduser(os.path.join("~", "Desktop", "texconv.exe")),
        os.path.expanduser(os.path.join("~", "OneDrive", "Desktop", "texconv.exe")),
    )
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate

    return None


def ConvertByTexconv(texconv_path, source_path, output_path, format_name):
    """画像を指定の DXGI フォーマットの DDS へ焼く。

    texconv は出力先をディレクトリで受け取り、名前を入力から決める。
    作業用のディレクトリへ出してから目的の名前へ移す。

    -srgb は付けない。sRGB かどうかは format_name が持つ
    （法線マップに sRGB を掛けると陰影が浅くなる）。
    """
    work_directory = os.path.dirname(source_path)
    command = [
        texconv_path,
        "-nologo",
        "-y",
        "-f", format_name,
        "-dx10",
        "-m", "0",  # 1x1 までの全ミップ
        "-o", work_directory,
        source_path,
    ]

    completed = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        return False

    base_name = os.path.splitext(os.path.basename(source_path))[0]
    produced_path = os.path.join(work_directory, base_name + ".DDS")
    if not os.path.isfile(produced_path):
        return False

    shutil.move(produced_path, output_path)
    return True


def BakeTexture(texconv_path, output_path, resolution, pixels,
                compressed_format, uncompressed_format):
    """RGBA8 の画素を正方形の DDS へ焼く。

    texconv があれば compressed_format へ、無ければ uncompressed_format の
    全ミップを直接書く。戻り値は実際に書いた DXGI フォーマット。
    """
    if texconv_path is not None:
        with tempfile.TemporaryDirectory(prefix="fang_bake_") as work_directory:
            base_name = os.path.splitext(os.path.basename(output_path))[0]
            png_path = os.path.join(work_directory, base_name + ".png")
            WritePngFile(png_path, resolution, resolution, pixels)

            if ConvertByTexconv(texconv_path, png_path, output_path,
                                FORMAT_NAMES[compressed_format]):
                return compressed_format

    WriteDdsFile(output_path, uncompressed_format, resolution, resolution,
                 BuildMipChain(resolution, resolution, pixels))
    return uncompressed_format
