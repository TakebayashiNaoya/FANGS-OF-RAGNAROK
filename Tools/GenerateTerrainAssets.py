#!/usr/bin/env python3
"""ハイトマップ地形の検証用アセットを Game/Assets/Terrain へ書き出す。

地形の実装を目で確かめるための仮アセットを作る。
手で描いた画像を置くと、傾斜としきい値の対応が分からなくなるため、
高さも地表レイヤの重みも同じ規約から計算して生成する。

    py Tools\\GenerateTerrainAssets.py

書き出すのは次の 5 ファイル。

    Heightmap.dds   513x513 R16_UNORM      地形の高さ
    Splatmap.dds    513x513 R8G8B8A8_UNORM 地表レイヤの重み（R=草 G=岩 B=土 A=255）
    LayerGrass.dds  256x256 ミップ付き     草のアルベド
    LayerRock.dds   256x256 ミップ付き     岩のアルベド
    LayerDirt.dds   256x256 ミップ付き     土のアルベド

レイヤアルベドは texconv があれば BC7_UNORM_SRGB へ焼く。
見つからなければ R8G8B8A8_UNORM_SRGB を直接書き、その旨を表示する。
"""

import array
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


ROOT_DIRECTORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIRECTORY = os.path.join(ROOT_DIRECTORY, "Game", "Assets", "Terrain")


# --------------------------------------------------------------------------------------
# ワールド規約
# --------------------------------------------------------------------------------------

# 地形が覆う範囲。X と Z に同じだけ広がる
TERRAIN_SIZE_CENTIMETER = 8192.0

# 画素値 65535 が指す高さ。0 は高さ 0
HEIGHT_SCALE_CENTIMETER = 600.0

# 高さ画像の一辺。四隅を共有する格子なので 2 の冪 + 1 になる
HEIGHTMAP_RESOLUTION = 513

# 格子の目の数。画素数より 1 少ない
HEIGHTMAP_CELL_COUNT = HEIGHTMAP_RESOLUTION - 1

# 隣り合う画素の間隔
CELL_SIZE_CENTIMETER = TERRAIN_SIZE_CENTIMETER / HEIGHTMAP_CELL_COUNT

# 画素 (0, 0) が乗るワールド座標。中心を原点に置く
TERRAIN_ORIGIN_CENTIMETER = -TERRAIN_SIZE_CENTIMETER / 2.0

# 画素 (px, pz) は次の位置に対応する。
#     x = TERRAIN_ORIGIN_CENTIMETER + px * CELL_SIZE_CENTIMETER
#     z = TERRAIN_ORIGIN_CENTIMETER + pz * CELL_SIZE_CENTIMETER
# 左手系 Y-up なので、行番号 pz が増えると +Z 側へ進む。
# エンジンは画像の上下を反転しないため、この向きのまま書き出す。


# --------------------------------------------------------------------------------------
# 地形の形
# --------------------------------------------------------------------------------------

# なだらかな丘の基準の高さ。正規化した 0〜1 のうちの値。
# 振幅の合計より小さくしてあるので、谷は 0 で止まって低地になる
HILL_BASE_HEIGHT = 0.150

# 丘を作る正弦のオクターブ。(振幅, X の周波数, Z の周波数, 位相)。
# X と Z の積ではなく和を取る。
# 積にすると波が軸に沿って格子状に並び、人工物に見えてしまう。
# 周波数の向きをオクターブごとに変えて、稜線が交差するようにしてある
HILL_OCTAVES = (
    (0.130, 1.1, 0.7, 0.13),
    (0.075, -1.7, 2.3, 0.57),
    (0.040, 3.7, 3.1, 0.85),
)

# 正弦だけでは筋が規則的に見えるので、弱い格子ノイズを重ねる
NOISE_SEED = 20260904
NOISE_LATTICE_COUNT = 16
NOISE_AMPLITUDE = 0.030

# 崖で持ち上げる高台。北東（+X かつ +Z）の角に置く
CLIFF_CORNER_U = 1.0
CLIFF_CORNER_V = 1.0

# 角からの距離がこれより近ければ高台。正規化した 0〜1 の距離
CLIFF_RADIUS = 0.420

# 崖の縁がなまる幅。狭いほど勾配が急になる
CLIFF_EDGE_WIDTH = 0.035

# 縁を真円にしないための揺らぎ
CLIFF_RADIUS_WAVE = 0.030
CLIFF_RADIUS_WAVE_COUNT = 3.0

# 高台が持ち上がる量
CLIFF_HEIGHT = 0.450

# 谷底の底上げ。正規化した高さで、クランプした後の全画素へ一律に足す。
# 谷底が 0 のままだと、y=0 に置く床メッシュと同じ平面に乗って Z ファイトする
BASE_HEIGHT_OFFSET = 0.020

# 底上げの量をワールドの長さで見た値
BASE_HEIGHT_OFFSET_CENTIMETER = BASE_HEIGHT_OFFSET * HEIGHT_SCALE_CENTIMETER

# 原点まわりに開ける平地の半径。
# ここを丘のままにすると、原点に置いた狼と y=53cm を回るカメラが丘の内側に入り、
# 背面カリングで手前の地形が抜けた絵になる
CLEARING_RADIUS_CENTIMETER = 800.0

# 平地から元の丘へ戻しきる距離。この間をスムーズステップでつなぐ
CLEARING_BLEND_END_CENTIMETER = 1600.0


# --------------------------------------------------------------------------------------
# スプラットマップのしきい値
# --------------------------------------------------------------------------------------

# 勾配（高さの変化量 / 水平の距離）がこれを超えると岩が混じり始める。
# 画素の間隔が 16cm と粗いので、崖の勾配も均されてこのあたりに収まる
SPLAT_ROCK_SLOPE_BEGIN = 0.28

# ここまで急になると岩だけになる
SPLAT_ROCK_SLOPE_END = 0.62

# 高さがこれを下回ると土が混じり始める。
# 高さは谷底からの値で測る。
# 絶対の高さで測ると、BASE_HEIGHT_OFFSET を変えるたびに土の広がり方まで動いてしまう
SPLAT_DIRT_HEIGHT_BEGIN_CENTIMETER = 55.0

# ここまで低いと土だけになる。しきい値 40cm を挟んで前後に振る
SPLAT_DIRT_HEIGHT_END_CENTIMETER = 25.0


# --------------------------------------------------------------------------------------
# レイヤアルベド
# --------------------------------------------------------------------------------------

LAYER_RESOLUTION = 256

# (ファイル名, 基準色, ノイズの振幅, ノイズの周波数)
# 周波数は整数にしてある。
# タイリングしたときに端で途切れないよう、正弦がテクスチャの幅でちょうど一周する
LAYER_DEFINITIONS = (
    ("LayerGrass.dds", (86, 124, 54), 14, (3, 7)),
    ("LayerRock.dds", (128, 130, 133), 12, (5, 11)),
    ("LayerDirt.dds", (122, 92, 62), 13, (2, 5)),
)


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
DXGI_FORMAT_BC7_UNORM_SRGB = 99

FORMAT_NAMES = {
    DXGI_FORMAT_R8G8B8A8_UNORM: "R8G8B8A8_UNORM",
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: "R8G8B8A8_UNORM_SRGB",
    DXGI_FORMAT_R16_UNORM: "R16_UNORM",
    DXGI_FORMAT_BC7_UNORM_SRGB: "BC7_UNORM_SRGB",
}

# ブロック圧縮でない形式の 1 テクセルあたりのバイト数
TEXEL_BYTE_SIZES = {
    DXGI_FORMAT_R8G8B8A8_UNORM: 4,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: 4,
    DXGI_FORMAT_R16_UNORM: 2,
}

BLOCK_TEXEL_SIZE = 4
BC7_BLOCK_BYTE_SIZE = 16


# --------------------------------------------------------------------------------------
# 数値の道具
# --------------------------------------------------------------------------------------

def Clamp(value, lowest, highest):
    """value を lowest 以上 highest 以下に収める。"""
    if value < lowest:
        return lowest
    if value > highest:
        return highest
    return value


def LinearStep(edge_zero, edge_one, value):
    """edge_zero で 0、edge_one で 1 になるよう線形に補間する。

    edge_zero が edge_one より大きくてもよい。
    その場合は向きが逆になる。
    """
    if edge_zero == edge_one:
        return 0.0 if value < edge_zero else 1.0

    return Clamp((value - edge_zero) / (edge_one - edge_zero), 0.0, 1.0)


def SmoothStep(edge_zero, edge_one, value):
    """LinearStep の結果を両端でなめらかにつなぐ。"""
    ratio = LinearStep(edge_zero, edge_one, value)
    return ratio * ratio * (3.0 - 2.0 * ratio)


# --------------------------------------------------------------------------------------
# 高さ
# --------------------------------------------------------------------------------------

def BuildNoiseLattice():
    """格子ノイズの制御点を作る。

    seed を固定しているので、何度実行しても同じ地形になる。
    """
    generator = random.Random(NOISE_SEED)
    point_count = NOISE_LATTICE_COUNT + 1
    return [
        [generator.uniform(-1.0, 1.0) for _ in range(point_count)]
        for _ in range(point_count)
    ]


def SampleNoiseLattice(lattice, u, v):
    """格子ノイズを (u, v) で読む。u と v は 0〜1。"""
    x = Clamp(u, 0.0, 1.0) * NOISE_LATTICE_COUNT
    z = Clamp(v, 0.0, 1.0) * NOISE_LATTICE_COUNT

    x_index = min(int(x), NOISE_LATTICE_COUNT - 1)
    z_index = min(int(z), NOISE_LATTICE_COUNT - 1)

    # 制御点の間は三次で補間する。
    # 線形のままだと格子の目が折れ線になって見えてしまう
    x_ratio = x - x_index
    z_ratio = z - z_index
    x_weight = x_ratio * x_ratio * (3.0 - 2.0 * x_ratio)
    z_weight = z_ratio * z_ratio * (3.0 - 2.0 * z_ratio)

    top = (lattice[z_index][x_index] * (1.0 - x_weight)
           + lattice[z_index][x_index + 1] * x_weight)
    bottom = (lattice[z_index + 1][x_index] * (1.0 - x_weight)
              + lattice[z_index + 1][x_index + 1] * x_weight)

    return top * (1.0 - z_weight) + bottom * z_weight


def CalculateHillHeight(u, v):
    """低周波の正弦を重ねて、なだらかな丘の高さを出す。"""
    height = HILL_BASE_HEIGHT
    for amplitude, frequency_x, frequency_z, phase in HILL_OCTAVES:
        height += amplitude * math.sin(
            math.tau * (u * frequency_x + v * frequency_z + phase))

    return height


def CalculateCliffWeight(u, v):
    """高台の内側で 1、外側で 0 になる重みを出す。

    崖の縁は SmoothStep で立ち上げる。
    幅を狭くしてあるので、境界だけが急勾配になる。
    """
    offset_u = CLIFF_CORNER_U - u
    offset_v = CLIFF_CORNER_V - v
    distance = math.hypot(offset_u, offset_v)

    # 真円のままだと人工物に見えるので、縁の半径を角度で揺らす
    angle = math.atan2(offset_v, offset_u)
    radius = CLIFF_RADIUS + CLIFF_RADIUS_WAVE * math.sin(CLIFF_RADIUS_WAVE_COUNT * angle)

    return SmoothStep(radius + CLIFF_EDGE_WIDTH, radius - CLIFF_EDGE_WIDTH, distance)


def CalculateClearingWeight(px, pz):
    """原点まわりの平地で 1、外側で 0 になる重みを出す。

    崖は原点から 2000cm 以上離れているので、この重みは崖に届かない。
    """
    x_centimeter = TERRAIN_ORIGIN_CENTIMETER + px * CELL_SIZE_CENTIMETER
    z_centimeter = TERRAIN_ORIGIN_CENTIMETER + pz * CELL_SIZE_CENTIMETER
    distance = math.hypot(x_centimeter, z_centimeter)

    return SmoothStep(
        CLEARING_BLEND_END_CENTIMETER, CLEARING_RADIUS_CENTIMETER, distance)


def GenerateHeightField():
    """正規化した高さ（0〜1）を行ごとの並びで返す。"""
    lattice = BuildNoiseLattice()

    height_field = []
    for pz in range(HEIGHTMAP_RESOLUTION):
        v = pz / HEIGHTMAP_CELL_COUNT
        row = []
        for px in range(HEIGHTMAP_RESOLUTION):
            u = px / HEIGHTMAP_CELL_COUNT

            height = CalculateHillHeight(u, v)
            height += NOISE_AMPLITUDE * SampleNoiseLattice(lattice, u, v)

            # 谷は 0 で止める。
            # 低地が平らになって、土のしきい値の効き方が分かりやすくなる
            height = Clamp(height, 0.0, 1.0)
            height += CLIFF_HEIGHT * CalculateCliffWeight(u, v)

            # 底上げはクランプの後に足す。
            # 先に足すと谷底が 0 で潰れて、底上げの意味が無くなる
            height = Clamp(height + BASE_HEIGHT_OFFSET, 0.0, 1.0)

            # 平地は底上げ値そのものの高さにする。
            # 縁はスムーズステップなので、丘との継ぎ目に折れ目が出ない
            clearing = CalculateClearingWeight(px, pz)
            height = height * (1.0 - clearing) + BASE_HEIGHT_OFFSET * clearing

            row.append(Clamp(height, 0.0, 1.0))

        height_field.append(row)

    return height_field


def CalculateSlope(height_field, px, pz):
    """隣接画素の高さ差から勾配を出す。

    戻り値は「高さの変化量 / 水平の距離」なので、傾きの正接にあたる。
    端の画素では片側だけを見るため、実際に離れている距離で割る。
    """
    left_index = max(px - 1, 0)
    right_index = min(px + 1, HEIGHTMAP_RESOLUTION - 1)
    near_index = max(pz - 1, 0)
    far_index = min(pz + 1, HEIGHTMAP_RESOLUTION - 1)

    distance_x = (right_index - left_index) * CELL_SIZE_CENTIMETER
    distance_z = (far_index - near_index) * CELL_SIZE_CENTIMETER

    difference_x = (height_field[pz][right_index] - height_field[pz][left_index])
    difference_z = (height_field[far_index][px] - height_field[near_index][px])

    gradient_x = difference_x * HEIGHT_SCALE_CENTIMETER / distance_x
    gradient_z = difference_z * HEIGHT_SCALE_CENTIMETER / distance_z

    return math.hypot(gradient_x, gradient_z)


# --------------------------------------------------------------------------------------
# 画像の中身
# --------------------------------------------------------------------------------------

def BuildHeightmapPixels(height_field):
    """正規化した高さを 16 bit の並びに直す。"""
    values = array.array("H")
    for row in height_field:
        for height in row:
            values.append(int(round(height * 65535.0)))

    # DDS はリトルエンディアンで書く
    if sys.byteorder == "big":
        values.byteswap()

    return values.tobytes()


def BuildSplatmapPixels(height_field):
    """傾斜と高度から地表レイヤの重みを決める。

    R=草 G=岩 B=土 A=255。
    重みの合計が 255 になるよう、丸め誤差を最も大きい成分へ寄せる。
    """
    pixels = bytearray()
    weight_counts = {"grass": 0, "rock": 0, "dirt": 0}

    for pz in range(HEIGHTMAP_RESOLUTION):
        for px in range(HEIGHTMAP_RESOLUTION):
            # 土のしきい値は谷底を 0 として測るので、底上げのぶんを引く
            height_centimeter = (height_field[pz][px] * HEIGHT_SCALE_CENTIMETER
                                 - BASE_HEIGHT_OFFSET_CENTIMETER)
            slope = CalculateSlope(height_field, px, pz)

            # 急斜面は土も草も乗らないので、岩を先に取る
            rock = LinearStep(SPLAT_ROCK_SLOPE_BEGIN, SPLAT_ROCK_SLOPE_END, slope)

            dirt_ratio = LinearStep(
                SPLAT_DIRT_HEIGHT_BEGIN_CENTIMETER,
                SPLAT_DIRT_HEIGHT_END_CENTIMETER,
                height_centimeter,
            )
            dirt = (1.0 - rock) * dirt_ratio
            grass = 1.0 - rock - dirt

            red = int(round(Clamp(grass, 0.0, 1.0) * 255.0))
            green = int(round(Clamp(rock, 0.0, 1.0) * 255.0))
            blue = int(round(Clamp(dirt, 0.0, 1.0) * 255.0))

            difference = 255 - (red + green + blue)
            if difference != 0:
                largest = max(red, green, blue)
                if largest == red:
                    red = Clamp(red + difference, 0, 255)
                elif largest == green:
                    green = Clamp(green + difference, 0, 255)
                else:
                    blue = Clamp(blue + difference, 0, 255)

            pixels += bytes((red, green, blue, 255))

            if green >= red and green >= blue:
                weight_counts["rock"] += 1
            elif blue >= red:
                weight_counts["dirt"] += 1
            else:
                weight_counts["grass"] += 1

    return bytes(pixels), weight_counts


def BuildLayerPixels(base_color, noise_amplitude, frequencies):
    """単色にわずかなノイズを足したアルベドを作る。

    ノイズは座標の正弦で、周波数が整数なのでテクスチャの端で位相が揃う。
    タイリングしても継ぎ目が出ない。
    """
    frequency_low, frequency_high = frequencies

    pixels = bytearray()
    for y in range(LAYER_RESOLUTION):
        v = y / LAYER_RESOLUTION
        for x in range(LAYER_RESOLUTION):
            u = x / LAYER_RESOLUTION

            noise = 0.6 * math.sin(math.tau * frequency_low * u) * math.cos(math.tau * frequency_low * v)
            noise += 0.4 * math.sin(math.tau * frequency_high * (u + v))

            offset = int(round(noise * noise_amplitude))
            pixels += bytes(Clamp(channel + offset, 0, 255) for channel in base_color)
            pixels.append(255)

    return bytes(pixels)


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
# DDS の書き出しと読み戻し
# --------------------------------------------------------------------------------------

def CalculateMipByteSize(dxgi_format, width, height):
    """ミップ 1 段ぶんのバイト数を出す。

    数え方は Engine/Resource/DdsImage.cpp の CalculateMipLayout と合わせてある。
    """
    if dxgi_format == DXGI_FORMAT_BC7_UNORM_SRGB:
        block_count_x = (width + BLOCK_TEXEL_SIZE - 1) // BLOCK_TEXEL_SIZE
        block_count_y = (height + BLOCK_TEXEL_SIZE - 1) // BLOCK_TEXEL_SIZE
        return block_count_x * block_count_y * BC7_BLOCK_BYTE_SIZE

    return width * TEXEL_BYTE_SIZES[dxgi_format] * height


def BuildDdsBytes(dxgi_format, width, height, mip_chain):
    """DX10 拡張ヘッダ付きの DDS を組み立てる。

    行間に詰め物は入れない。
    ピクセルはミップ 0 から順に並べる。
    """
    mip_count = len(mip_chain)

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    caps = DDSCAPS_TEXTURE
    if mip_count > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP

    if dxgi_format == DXGI_FORMAT_BC7_UNORM_SRGB:
        flags |= DDSD_LINEARSIZE
        pitch_or_linear_size = CalculateMipByteSize(dxgi_format, width, height)
    else:
        flags |= DDSD_PITCH
        pitch_or_linear_size = width * TEXEL_BYTE_SIZES[dxgi_format]

    header = struct.pack(
        "<7I",
        DDS_HEADER_BYTE_SIZE,
        flags,
        height,
        width,
        pitch_or_linear_size,
        0,
        mip_count,
    )
    header += b"\x00" * 44  # reserved1[11]
    header += struct.pack("<8I", 32, DDPF_FOURCC, FOURCC_DX10, 0, 0, 0, 0, 0)
    header += struct.pack("<5I", caps, 0, 0, 0, 0)

    header_dx10 = struct.pack(
        "<5I", dxgi_format, DDS_DIMENSION_TEXTURE2D, 0, 1, 0)

    return DDS_MAGIC + header + header_dx10 + b"".join(mip_chain)


def WriteDdsFile(path, dxgi_format, width, height, mip_chain):
    with open(path, "wb") as output_file:
        output_file.write(BuildDdsBytes(dxgi_format, width, height, mip_chain))


def VerifyDdsFile(path, dxgi_format, width, height, mip_count):
    """書いたファイルを読み戻してヘッダと大きさを確かめる。

    戻り値は (成否, 説明) の組。
    """
    with open(path, "rb") as input_file:
        file_bytes = input_file.read()

    if len(file_bytes) < DDS_DATA_OFFSET:
        return False, "ヘッダが足りない"

    if file_bytes[:4] != DDS_MAGIC:
        return False, "magic が違う"

    header = struct.unpack_from("<7I", file_bytes, 4)
    if header[0] != DDS_HEADER_BYTE_SIZE:
        return False, "ヘッダの大きさが %d" % header[0]

    pixel_format = struct.unpack_from("<8I", file_bytes, 4 + 28 + 44)
    if pixel_format[0] != 32:
        return False, "ピクセルフォーマットの大きさが %d" % pixel_format[0]
    if (pixel_format[1] & DDPF_FOURCC) == 0 or pixel_format[2] != FOURCC_DX10:
        return False, "DX10 拡張ヘッダの印が無い"

    header_dx10 = struct.unpack_from(
        "<5I", file_bytes, 4 + DDS_HEADER_BYTE_SIZE)
    if header_dx10[0] != dxgi_format:
        return False, "DXGI_FORMAT が %d" % header_dx10[0]
    if header_dx10[1] != DDS_DIMENSION_TEXTURE2D:
        return False, "resourceDimension が %d" % header_dx10[1]
    if header_dx10[3] != 1:
        return False, "arraySize が %d" % header_dx10[3]

    if header[2] != height or header[3] != width:
        return False, "寸法が %dx%d" % (header[3], header[2])

    stored_mip_count = header[6] if header[6] > 0 else 1
    if stored_mip_count != mip_count:
        return False, "ミップ段数が %d" % stored_mip_count

    expected_size = DDS_DATA_OFFSET
    mip_width = width
    mip_height = height
    for _ in range(mip_count):
        expected_size += CalculateMipByteSize(dxgi_format, mip_width, mip_height)
        mip_width = mip_width // 2 if mip_width > 1 else 1
        mip_height = mip_height // 2 if mip_height > 1 else 1

    if len(file_bytes) != expected_size:
        return False, "中身が %d バイト（想定 %d）" % (len(file_bytes), expected_size)

    return True, "%s %dx%d ミップ %d 段 %d バイト" % (
        FORMAT_NAMES[dxgi_format], width, height, mip_count, len(file_bytes))


# --------------------------------------------------------------------------------------
# texconv
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


def ConvertByTexconv(texconv_path, source_png_path, output_path):
    """PNG を BC7_UNORM_SRGB の DDS へ焼く。

    texconv は出力先をディレクトリで受け取り、名前を入力から決める。
    作業用のディレクトリへ出してから目的の名前へ移す。
    """
    work_directory = os.path.dirname(source_png_path)
    command = [
        texconv_path,
        "-nologo",
        "-y",
        "-f", "BC7_UNORM_SRGB",
        "-dx10",
        "-m", "0",  # 1x1 までの全ミップ
        "-o", work_directory,
        source_png_path,
    ]

    completed = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        return False

    base_name = os.path.splitext(os.path.basename(source_png_path))[0]
    produced_path = os.path.join(work_directory, base_name + ".DDS")
    if not os.path.isfile(produced_path):
        return False

    shutil.move(produced_path, output_path)
    return True


# --------------------------------------------------------------------------------------
# 生成の手順
# --------------------------------------------------------------------------------------

def WriteHeightmap(height_field):
    path = os.path.join(OUTPUT_DIRECTORY, "Heightmap.dds")
    WriteDdsFile(
        path,
        DXGI_FORMAT_R16_UNORM,
        HEIGHTMAP_RESOLUTION,
        HEIGHTMAP_RESOLUTION,
        [BuildHeightmapPixels(height_field)],
    )
    return path


def WriteSplatmap(height_field):
    pixels, weight_counts = BuildSplatmapPixels(height_field)
    path = os.path.join(OUTPUT_DIRECTORY, "Splatmap.dds")
    WriteDdsFile(
        path,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        HEIGHTMAP_RESOLUTION,
        HEIGHTMAP_RESOLUTION,
        [pixels],
    )
    return path, weight_counts


def WriteLayers(texconv_path):
    """レイヤアルベドを 3 枚書く。

    戻り値は (パスの並び, BC7 で焼けたか)。
    """
    written_paths = []
    is_block_compressed = texconv_path is not None

    with tempfile.TemporaryDirectory(prefix="fang_terrain_") as work_directory:
        for file_name, base_color, noise_amplitude, frequencies in LAYER_DEFINITIONS:
            pixels = BuildLayerPixels(base_color, noise_amplitude, frequencies)
            output_path = os.path.join(OUTPUT_DIRECTORY, file_name)

            if is_block_compressed:
                png_path = os.path.join(
                    work_directory, os.path.splitext(file_name)[0] + ".png")
                WritePngFile(png_path, LAYER_RESOLUTION, LAYER_RESOLUTION, pixels)

                if ConvertByTexconv(texconv_path, png_path, output_path):
                    written_paths.append(output_path)
                    continue

                # 途中で失敗したら、残りもまとめて直書きへ倒す
                print("  texconv の変換に失敗した。RGBA8 sRGB の直書きへ切り替える")
                is_block_compressed = False

            WriteDdsFile(
                output_path,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                LAYER_RESOLUTION,
                LAYER_RESOLUTION,
                BuildMipChain(LAYER_RESOLUTION, LAYER_RESOLUTION, pixels),
            )
            written_paths.append(output_path)

    return written_paths, is_block_compressed


def SummarizeHeightField(height_field):
    """崖と丘の様子を数値で拾う。"""
    lowest = 1.0
    highest = 0.0
    steep_count = 0
    plateau_count = 0
    total_count = HEIGHTMAP_RESOLUTION * HEIGHTMAP_RESOLUTION

    # 狼とカメラが動く範囲。ここが平らでないと地形にめり込む
    near_origin_highest = 0.0
    near_origin_radius = 400.0

    for pz in range(HEIGHTMAP_RESOLUTION):
        v = pz / HEIGHTMAP_CELL_COUNT
        for px in range(HEIGHTMAP_RESOLUTION):
            height = height_field[pz][px]
            lowest = min(lowest, height)
            highest = max(highest, height)

            if CalculateSlope(height_field, px, pz) >= SPLAT_ROCK_SLOPE_BEGIN:
                steep_count += 1

            # 縁の中ほどより内側を高台と数える
            if CalculateCliffWeight(px / HEIGHTMAP_CELL_COUNT, v) >= 0.5:
                plateau_count += 1

            x_centimeter = TERRAIN_ORIGIN_CENTIMETER + px * CELL_SIZE_CENTIMETER
            z_centimeter = TERRAIN_ORIGIN_CENTIMETER + pz * CELL_SIZE_CENTIMETER
            if math.hypot(x_centimeter, z_centimeter) <= near_origin_radius:
                near_origin_highest = max(near_origin_highest, height)

    # 原点はちょうど画素 (256, 256) に乗る
    origin_index = HEIGHTMAP_CELL_COUNT // 2

    return {
        "lowest_centimeter": lowest * HEIGHT_SCALE_CENTIMETER,
        "highest_centimeter": highest * HEIGHT_SCALE_CENTIMETER,
        "steep_ratio": steep_count / total_count,
        "plateau_ratio": plateau_count / total_count,
        "origin_centimeter": (height_field[origin_index][origin_index]
                              * HEIGHT_SCALE_CENTIMETER),
        "near_origin_radius": near_origin_radius,
        "near_origin_highest_centimeter": near_origin_highest * HEIGHT_SCALE_CENTIMETER,
    }


def main():
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)

    print("出力先: %s" % OUTPUT_DIRECTORY)
    print("範囲 %.0fcm 角 / 高さ %.0fcm / 画素間隔 %.0fcm"
          % (TERRAIN_SIZE_CENTIMETER, HEIGHT_SCALE_CENTIMETER, CELL_SIZE_CENTIMETER))
    print("")

    print("高さを計算中...")
    height_field = GenerateHeightField()

    written = []
    written.append((WriteHeightmap(height_field),
                    DXGI_FORMAT_R16_UNORM, HEIGHTMAP_RESOLUTION, 1))

    splatmap_path, weight_counts = WriteSplatmap(height_field)
    written.append((splatmap_path,
                    DXGI_FORMAT_R8G8B8A8_UNORM, HEIGHTMAP_RESOLUTION, 1))

    texconv_path = FindTexconvPath()
    if texconv_path is None:
        print("texconv が見つからないので、レイヤは RGBA8 sRGB で直接書く")
    else:
        print("texconv: %s" % texconv_path)

    layer_paths, is_block_compressed = WriteLayers(texconv_path)
    layer_format = (DXGI_FORMAT_BC7_UNORM_SRGB if is_block_compressed
                    else DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
    layer_mip_count = LAYER_RESOLUTION.bit_length()  # 256 なら 9 段
    for layer_path in layer_paths:
        written.append((layer_path, layer_format, LAYER_RESOLUTION, layer_mip_count))

    print("")
    print("自己検証")
    is_all_valid = True
    for path, dxgi_format, resolution, mip_count in written:
        is_valid, message = VerifyDdsFile(
            path, dxgi_format, resolution, resolution, mip_count)
        is_all_valid = is_all_valid and is_valid
        print("  %-14s %s %s"
              % (os.path.basename(path), "OK  " if is_valid else "NG  ", message))

    summary = SummarizeHeightField(height_field)
    total_count = HEIGHTMAP_RESOLUTION * HEIGHTMAP_RESOLUTION

    print("")
    print("地形")
    print("  高さ           %.1fcm 〜 %.1fcm" % (
        summary["lowest_centimeter"], summary["highest_centimeter"]))
    print("  原点の高さ     %.1fcm" % summary["origin_centimeter"])
    print("  半径 %.0fcm 圏  最大 %.1fcm" % (
        summary["near_origin_radius"], summary["near_origin_highest_centimeter"]))
    print("  急斜面の画素   %.1f%%（勾配 %.2f 以上）" % (
        summary["steep_ratio"] * 100.0, SPLAT_ROCK_SLOPE_BEGIN))
    print("  高台の画素     %.1f%%" % (summary["plateau_ratio"] * 100.0))

    print("")
    print("スプラットマップの割合")
    for key, label in (("grass", "草 R"), ("rock", "岩 G"), ("dirt", "土 B")):
        print("  %s          %5.1f%%（%d 画素）" % (
            label, weight_counts[key] / total_count * 100.0, weight_counts[key]))

    print("")
    if not is_all_valid:
        print("検証に失敗したファイルがある")
        return 1

    print("5 ファイルを書き出した（レイヤは %s）" % FORMAT_NAMES[layer_format])
    return 0


if __name__ == "__main__":
    sys.exit(main())
