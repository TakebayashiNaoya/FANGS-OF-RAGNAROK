#!/usr/bin/env python3
"""ハイトマップ地形の検証用アセットを Game/Assets/Terrain へ書き出す。

地形の実装を目で確かめるための仮アセットを作る。
手で描いた画像を置くと、傾斜としきい値の対応が分からなくなるため、
高さも地表レイヤの重みも同じ規約から計算して生成する。

    py Tools\\GenerateTerrainAssets.py

書き出すのは次の 8 ファイル。

    Heightmap.dds        513x513 R16_UNORM      地形の高さ
    Splatmap.dds         513x513 R8G8B8A8_UNORM 地表レイヤの重み（R=草 G=岩 B=土 A=255）
    LayerGrass.dds       256x256 ミップ付き     草のアルベド
    LayerRock.dds        256x256 ミップ付き     岩のアルベド
    LayerDirt.dds        256x256 ミップ付き     土のアルベド
    LayerGrassNormal.dds 256x256 ミップ付き     草の法線マップ
    LayerRockNormal.dds  256x256 ミップ付き     岩の法線マップ
    LayerDirtNormal.dds  256x256 ミップ付き     土の法線マップ

texconv があればアルベドは BC7_UNORM_SRGB、法線は BC5_UNORM へ焼く。
見つからなければ RGBA8 を直接書き、その旨を表示する。
法線に sRGB を掛けないのは、色ではなく数値だから（掛けると陰影が浅くなる）。

DDS の組み立てと texconv 呼び出しは Tools/TextureBaking.py にある。
"""

import array
import math
import os
import random
import sys

import TextureBaking


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

# 凹凸の高さを作る正弦のオクターブ。(振幅, u の周波数, v の周波数)。
# アルベドと同じく周波数は整数なので、端で位相がそろいタイリングしても継ぎ目が出ない
# (ファイル名, オクターブ, 割れ目の本数, 傾きに掛ける倍率)
# 草は細かい粒、岩は大きい塊に割れ目、土は細かいざらつき
LAYER_NORMAL_DEFINITIONS = (
    ("LayerGrassNormal.dds", ((0.55, 24, 24), (0.30, 37, 37), (0.15, 61, 13)), 0, 3.0),
    ("LayerRockNormal.dds", ((1.00, 3, 3), (0.45, 5, 9), (0.20, 17, 23)), 4, 6.0),
    ("LayerDirtNormal.dds", ((0.50, 48, 48), (0.30, 29, 71), (0.20, 83, 83)), 0, 2.0),
)

# 割れ目の深さと、縁の鋭さ。大きいほど溝が細く深くなる
CRACK_DEPTH = 0.9
CRACK_SHARPNESS = 6


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


def BuildLayerHeights(octaves, crack_count):
    """レイヤの凹凸の高さを 0〜1 で作る。行優先で LAYER_RESOLUTION の 2 乗個。

    正弦の重ね合わせなので、周波数が整数である限りタイリングの継ぎ目は出ない。
    最後に 0〜1 へ伸ばして幅をそろえる ➡ 強さの調整は倍率 1 か所で済む。
    """
    heights = []
    for y in range(LAYER_RESOLUTION):
        v = y / LAYER_RESOLUTION
        for x in range(LAYER_RESOLUTION):
            u = x / LAYER_RESOLUTION

            height = 0.0
            for amplitude, frequency_u, frequency_v in octaves:
                height += (amplitude
                           * math.sin(math.tau * frequency_u * u)
                           * math.cos(math.tau * frequency_v * v))

            # 岩の割れ目。正弦が 0 を切る細い線だけを深く落として溝にする
            if crack_count > 0:
                groove = abs(math.sin(math.tau * crack_count * (u + 0.35 * v)))
                height -= CRACK_DEPTH * (1.0 - groove) ** CRACK_SHARPNESS

            heights.append(height)

    lowest = min(heights)
    highest = max(heights)
    span = max(highest - lowest, 1e-6)

    return [(value - lowest) / span for value in heights]


# --------------------------------------------------------------------------------------
# 生成の手順
# --------------------------------------------------------------------------------------

def WriteHeightmap(height_field):
    path = os.path.join(OUTPUT_DIRECTORY, "Heightmap.dds")
    TextureBaking.WriteDdsFile(
        path,
        TextureBaking.DXGI_FORMAT_R16_UNORM,
        HEIGHTMAP_RESOLUTION,
        HEIGHTMAP_RESOLUTION,
        [BuildHeightmapPixels(height_field)],
    )
    return path


def WriteSplatmap(height_field):
    pixels, weight_counts = BuildSplatmapPixels(height_field)
    path = os.path.join(OUTPUT_DIRECTORY, "Splatmap.dds")
    TextureBaking.WriteDdsFile(
        path,
        TextureBaking.DXGI_FORMAT_R8G8B8A8_UNORM,
        HEIGHTMAP_RESOLUTION,
        HEIGHTMAP_RESOLUTION,
        [pixels],
    )
    return path, weight_counts


def WriteLayers(texconv_path):
    """レイヤアルベドを 3 枚書く。戻り値は (パス, 実際に焼けた形式) の並び。"""
    written = []
    for file_name, base_color, noise_amplitude, frequencies in LAYER_DEFINITIONS:
        pixels = BuildLayerPixels(base_color, noise_amplitude, frequencies)
        output_path = os.path.join(OUTPUT_DIRECTORY, file_name)

        dxgi_format = TextureBaking.BakeTexture(
            texconv_path,
            output_path,
            LAYER_RESOLUTION,
            pixels,
            TextureBaking.DXGI_FORMAT_BC7_UNORM_SRGB,
            TextureBaking.DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        )
        written.append((output_path, dxgi_format))

    return written


def WriteLayerNormals(texconv_path):
    """レイヤの法線マップを 3 枚書く。戻り値は (パス, 形式, 傾きの平均, 傾きの最大) の並び。

    法線は色ではなく数値なので sRGB では焼かない。
    """
    written = []
    for file_name, octaves, crack_count, strength in LAYER_NORMAL_DEFINITIONS:
        heights = BuildLayerHeights(octaves, crack_count)
        pixels = TextureBaking.BuildNormalMapPixels(LAYER_RESOLUTION, heights, strength)
        average_tilt, highest_tilt = TextureBaking.SummarizeNormalMapTilt(pixels)

        output_path = os.path.join(OUTPUT_DIRECTORY, file_name)
        dxgi_format = TextureBaking.BakeTexture(
            texconv_path,
            output_path,
            LAYER_RESOLUTION,
            pixels,
            TextureBaking.DXGI_FORMAT_BC5_UNORM,
            TextureBaking.DXGI_FORMAT_R8G8B8A8_UNORM,
        )
        written.append((output_path, dxgi_format, average_tilt, highest_tilt))

    return written


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
                    TextureBaking.DXGI_FORMAT_R16_UNORM, HEIGHTMAP_RESOLUTION, 1))

    splatmap_path, weight_counts = WriteSplatmap(height_field)
    written.append((splatmap_path,
                    TextureBaking.DXGI_FORMAT_R8G8B8A8_UNORM, HEIGHTMAP_RESOLUTION, 1))

    texconv_path = TextureBaking.FindTexconvPath()
    if texconv_path is None:
        print("texconv が見つからないので、レイヤは RGBA8 で直接書く")
    else:
        print("texconv: %s" % texconv_path)

    layer_mip_count = LAYER_RESOLUTION.bit_length()  # 256 なら 9 段

    print("レイヤのアルベドを作成中...")
    for layer_path, layer_format in WriteLayers(texconv_path):
        written.append((layer_path, layer_format, LAYER_RESOLUTION, layer_mip_count))

    print("レイヤの法線マップを作成中...")
    normal_tilts = []
    for normal_path, normal_format, average_tilt, highest_tilt in WriteLayerNormals(texconv_path):
        written.append((normal_path, normal_format, LAYER_RESOLUTION, layer_mip_count))
        normal_tilts.append((os.path.basename(normal_path), average_tilt, highest_tilt))

    print("")
    print("自己検証")
    is_all_valid = True
    for path, dxgi_format, resolution, mip_count in written:
        is_valid, message = TextureBaking.VerifyDdsFile(
            path, dxgi_format, resolution, resolution, mip_count)
        is_all_valid = is_all_valid and is_valid
        print("  %-20s %s %s"
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
    print("法線マップの傾き（真上からの角度）")
    for file_name, average_tilt, highest_tilt in normal_tilts:
        print("  %-20s 平均 %4.1f 度 / 最大 %4.1f 度" % (file_name, average_tilt, highest_tilt))

    print("")
    if not is_all_valid:
        print("検証に失敗したファイルがある")
        return 1

    print("%d ファイルを書き出した" % len(written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
