#!/usr/bin/env python3
"""複数メッシュ + ノード階層を持つステージの glTF（.gltf + .bin）を Game/Assets/Models へ書き出す。

柱・梁・階段・目印・幟という単純な形の積み重ねで、テーブルの列と柱間の並び・段の高さがそのまま
配置の格子とアーチの形に対応するようにしてある。乱数は使わず、座標はすべてこのファイルの中の
定数から決まる ➡ 何度実行しても同じ .gltf / .bin が出る。

    py Tools\\GenerateStageGltf.py

出力先はこのリポジトリの Game/Assets/Models 固定（Wolf.gltf と同じ場所。テクスチャの相対パスも
Wolf 用の textures/Wolf.png を指すので、狼と同じ textures フォルダをそのまま使う）。
"""

import io
import json
import math
import os
import struct


ROOT_DIRECTORY   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIRECTORY = os.path.join(ROOT_DIRECTORY, "Game", "Assets", "Models")

GLTF_PATH    = os.path.join(OUTPUT_DIRECTORY, "Stage.gltf")
BIN_PATH     = os.path.join(OUTPUT_DIRECTORY, "Stage.bin")
BIN_FILENAME = "Stage.bin"

# 狼と同じ画像（Game/Assets/Models/textures/Wolf.dds。実行時に拡張子を .dds へ差し替えて読む）を
# ベースカラーに使う。新しい画像を用意しなくても、複数マテリアルが同じ画像パスを指す状態を作れる。
BASE_COLOR_IMAGE_URI = "textures/Wolf.png"

COMPONENT_TYPE_FLOAT          = 5126
COMPONENT_TYPE_UNSIGNED_SHORT = 5123
PRIMITIVE_MODE_TRIANGLES      = 4


#---------------------------------------------------------------------------
# ベクトル演算（タプル 3 要素）
#---------------------------------------------------------------------------
def Sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def Cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def Dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def QuaternionFromAxisAngle(axis, angleRadians):
    """軸 axis（単位ベクトル）まわりに angleRadians 回した回転を (x, y, z, w) で返す。"""
    half     = angleRadians * 0.5
    sinHalf  = math.sin(half)
    return (axis[0] * sinHalf, axis[1] * sinHalf, axis[2] * sinHalf, math.cos(half))


#---------------------------------------------------------------------------
# メッシュ 1 個ぶんの形を組み立てる
#---------------------------------------------------------------------------
class MeshBuilder:
    """positions / normals / uvs / indices を面の単位で積み上げる入れ物。"""

    def __init__(self):
        self.positions = []
        self.normals   = []
        self.uvs       = []
        self.indices   = []

    def AddQuadFace(self, corners, normal):
        """4 点の輪をなす面を追加する。輪の向きは normal 側から見て CCW になるよう自動で直す。"""
        v0, v1, v2, v3 = corners
        computed = Cross(Sub(v1, v0), Sub(v2, v0))
        ordered  = corners if Dot(computed, normal) >= 0.0 else (v0, v3, v2, v1)

        base = len(self.positions)
        self.positions.extend(ordered)
        self.normals.extend([normal] * 4)
        self.uvs.extend([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
        self.indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    def AddTriFace(self, corners, normal, uvs):
        """3 点の面を追加する。輪の向きは normal 側から見て CCW になるよう自動で直す。"""
        v0, v1, v2 = corners
        computed = Cross(Sub(v1, v0), Sub(v2, v0))
        ordered  = corners if Dot(computed, normal) >= 0.0 else (v0, v2, v1)

        base = len(self.positions)
        self.positions.extend(ordered)
        self.normals.extend([normal] * 3)
        self.uvs.extend(uvs)
        self.indices.extend([base, base + 1, base + 2])


def MakeBox(halfExtents):
    """原点中心の直方体。面ごとに法線を持つのでシャープなエッジになる（24 頂点・36 インデックス）。"""
    hx, hy, hz = halfExtents
    builder = MeshBuilder()

    builder.AddQuadFace(
        ((hx, -hy, -hz), (hx, -hy, hz), (hx, hy, hz), (hx, hy, -hz)), (1.0, 0.0, 0.0)
    )
    builder.AddQuadFace(
        ((-hx, -hy, hz), (-hx, -hy, -hz), (-hx, hy, -hz), (-hx, hy, hz)), (-1.0, 0.0, 0.0)
    )
    builder.AddQuadFace(
        ((-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)), (0.0, 1.0, 0.0)
    )
    builder.AddQuadFace(
        ((-hx, -hy, hz), (-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz)), (0.0, -1.0, 0.0)
    )
    builder.AddQuadFace(
        ((-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)), (0.0, 0.0, 1.0)
    )
    builder.AddQuadFace(
        ((hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)), (0.0, 0.0, -1.0)
    )

    return builder


def MakePyramid(baseHalfExtent, height):
    """底面 y = 0・頂点 (0, height, 0) の四角錐。目印として corner に置く（底面 + 側面 4 枚）。"""
    b = baseHalfExtent
    corners = ((-b, 0.0, -b), (b, 0.0, -b), (b, 0.0, b), (-b, 0.0, b))
    apex    = (0.0, height, 0.0)

    builder = MeshBuilder()
    builder.AddQuadFace(corners, (0.0, -1.0, 0.0))

    for index in range(4):
        edgeStart = corners[index]
        edgeEnd   = corners[(index + 1) % 4]
        midOutward = ((edgeStart[0] + edgeEnd[0]) * 0.5, 0.0, (edgeStart[2] + edgeEnd[2]) * 0.5)
        length     = math.sqrt(midOutward[0] ** 2 + midOutward[2] ** 2)
        outwardNormal = (midOutward[0] / length, 0.3, midOutward[2] / length)
        builder.AddTriFace(
            (edgeStart, edgeEnd, apex), outwardNormal, ((0.0, 0.0), (1.0, 0.0), (0.5, 1.0))
        )

    return builder


def MakeVerticalQuad(halfWidth, height):
    """幅 halfWidth * 2・高さ height の板。足元が y = 0 で、法線は +Z 側を向く（幟・旗用）。"""
    corners = (
        (-halfWidth, 0.0, 0.0),
        (halfWidth, 0.0, 0.0),
        (halfWidth, height, 0.0),
        (-halfWidth, height, 0.0),
    )
    builder = MeshBuilder()
    builder.AddQuadFace(corners, (0.0, 0.0, 1.0))
    return builder


#---------------------------------------------------------------------------
# glTF の組み立て
#---------------------------------------------------------------------------
class GltfDocument:
    """バッファ・アクセサ・メッシュ・マテリアル・ノードを積み上げて最後に辞書へまとめる。"""

    def __init__(self):
        self.bufferBytes  = bytearray()
        self.bufferViews  = []
        self.accessors    = []
        self.meshes       = []
        self.materials    = []
        self.nodes        = []
        self.textureIndex = None

    def _AppendAligned(self, rawBytes, alignment):
        while len(self.bufferBytes) % alignment != 0:
            self.bufferBytes.append(0)
        offset = len(self.bufferBytes)
        self.bufferBytes.extend(rawBytes)
        return offset

    def _AddAccessor(self, componentType, count, accessorType, rawBytes, alignment, minMax=None):
        offset = self._AppendAligned(rawBytes, alignment)
        bufferViewIndex = len(self.bufferViews)
        self.bufferViews.append(
            {"buffer": 0, "byteOffset": offset, "byteLength": len(rawBytes)}
        )

        accessor = {
            "bufferView":    bufferViewIndex,
            "componentType": componentType,
            "count":         count,
            "type":          accessorType,
        }
        if minMax is not None:
            accessor["min"], accessor["max"] = minMax

        accessorIndex = len(self.accessors)
        self.accessors.append(accessor)
        return accessorIndex

    def _AddPositionAccessor(self, positions):
        rawBytes = struct.pack("<%df" % (len(positions) * 3), *(value for vertex in positions for value in vertex))
        minimum = [min(vertex[axis] for vertex in positions) for axis in range(3)]
        maximum = [max(vertex[axis] for vertex in positions) for axis in range(3)]
        return self._AddAccessor(
            COMPONENT_TYPE_FLOAT, len(positions), "VEC3", rawBytes, 4, (minimum, maximum)
        )

    def _AddVec3Accessor(self, values):
        rawBytes = struct.pack("<%df" % (len(values) * 3), *(value for vertex in values for value in vertex))
        return self._AddAccessor(COMPONENT_TYPE_FLOAT, len(values), "VEC3", rawBytes, 4)

    def _AddVec2Accessor(self, values):
        rawBytes = struct.pack("<%df" % (len(values) * 2), *(value for vertex in values for value in vertex))
        return self._AddAccessor(COMPONENT_TYPE_FLOAT, len(values), "VEC2", rawBytes, 4)

    def _AddIndexAccessor(self, indices):
        rawBytes = struct.pack("<%dH" % len(indices), *indices)
        return self._AddAccessor(COMPONENT_TYPE_UNSIGNED_SHORT, len(indices), "SCALAR", rawBytes, 2)

    def EnsureBaseColorTexture(self):
        """全マテリアル共通の 1 枚（狼と同じ画像）を指すテクスチャの番号を返す。1 度しか作らない。"""
        if self.textureIndex is None:
            self.textureIndex = 0
        return self.textureIndex

    def AddMesh(self, name, builder, material=None):
        """builder の中身をアクセサへ写し、mesh 番号を返す。material は (metallicFactor, roughnessFactor) か None。"""
        positionAccessor = self._AddPositionAccessor(builder.positions)
        normalAccessor    = self._AddVec3Accessor(builder.normals)
        uvAccessor         = self._AddVec2Accessor(builder.uvs)
        indexAccessor       = self._AddIndexAccessor(builder.indices)

        primitive = {
            "attributes": {
                "POSITION":   positionAccessor,
                "NORMAL":     normalAccessor,
                "TEXCOORD_0": uvAccessor,
            },
            "indices": indexAccessor,
            "mode":    PRIMITIVE_MODE_TRIANGLES,
        }

        if material is not None:
            metallicFactor, roughnessFactor = material
            materialIndex = len(self.materials)
            self.materials.append(
                {
                    "name": name,
                    "pbrMetallicRoughness": {
                        "baseColorTexture": {"index": self.EnsureBaseColorTexture()},
                        "metallicFactor":   metallicFactor,
                        "roughnessFactor":  roughnessFactor,
                    },
                }
            )
            primitive["material"] = materialIndex

        meshIndex = len(self.meshes)
        self.meshes.append({"name": name, "primitives": [primitive]})
        return meshIndex

    def AddNode(self, name, parentIndex=None, meshIndex=None, translation=None, rotation=None, scale=None):
        node = {"name": name}
        if meshIndex is not None:
            node["mesh"] = meshIndex
        if translation is not None:
            node["translation"] = list(translation)
        if rotation is not None:
            node["rotation"] = list(rotation)
        if scale is not None:
            node["scale"] = list(scale)

        nodeIndex = len(self.nodes)
        self.nodes.append(node)

        if parentIndex is not None:
            parent = self.nodes[parentIndex]
            parent.setdefault("children", []).append(nodeIndex)

        return nodeIndex

    def Build(self):
        document = {
            "asset":  {"generator": "FangsOfRagnarok/GenerateStageGltf.py", "version": "2.0"},
            "scene":  0,
            "scenes": [{"nodes": [0]}],
            "nodes":       self.nodes,
            "meshes":      self.meshes,
            "accessors":   self.accessors,
            "bufferViews": self.bufferViews,
            "buffers":     [{"uri": BIN_FILENAME, "byteLength": len(self.bufferBytes)}],
        }

        if self.materials:
            document["materials"] = self.materials
        if self.textureIndex is not None:
            document["images"]   = [{"uri": BASE_COLOR_IMAGE_URI, "mimeType": "image/png"}]
            document["samplers"] = [{"magFilter": 9729, "minFilter": 9987}]
            document["textures"] = [{"sampler": 0, "source": 0}]

        return document


#---------------------------------------------------------------------------
# ステージそのものの組み立て
#---------------------------------------------------------------------------
def BuildStage():
    document = GltfDocument()

    # マテリアルの係数は狼と同じ画像を指しつつ、メッシュごとに見え方を変える程度の意味しか持たない。
    platformMesh  = document.AddMesh("Platform", MakeBox((60.0, 5.0, 60.0)), material=(0.1, 0.75))
    pillarBaseMesh  = document.AddMesh("PillarBase", MakeBox((20.0, 10.0, 20.0)))
    pillarShaftMesh = document.AddMesh("PillarShaft", MakeBox((10.0, 80.0, 10.0)), material=(0.05, 0.55))
    pillarCapMesh   = document.AddMesh("PillarCap", MakeBox((20.0, 10.0, 20.0)), material=(0.3, 0.45))
    archBeamMesh    = document.AddMesh("ArchBeam", MakeBox((135.0, 8.0, 8.0)), material=(0.15, 0.4))
    roofSlabMesh    = document.AddMesh("RoofSlab", MakeBox((140.0, 8.0, 60.0)))
    stepBox1Mesh    = document.AddMesh("StepBox1", MakeBox((70.0, 10.0, 40.0)), material=(0.2, 0.6))
    stepBox2Mesh    = document.AddMesh("StepBox2", MakeBox((70.0, 10.0, 40.0)), material=(0.2, 0.6))
    stepBox3Mesh    = document.AddMesh("StepBox3", MakeBox((70.0, 10.0, 40.0)))
    markerMesh      = document.AddMesh("MarkerPyramid", MakePyramid(25.0, 50.0))
    bannerMesh      = document.AddMesh("Banner", MakeVerticalQuad(35.0, 180.0), material=(0.0, 0.85))
    wellMesh        = document.AddMesh("Well", MakeBox((30.0, 25.0, 30.0)))

    stageRoot = document.AddNode("StageRoot")

    #-----------------------------------------------------------------
    # 床の格子（3 x 3。同じ Platform を 9 ノードから参照する）
    #-----------------------------------------------------------------
    platformGrid = document.AddNode("PlatformGrid", parentIndex=stageRoot, translation=(0.0, 5.0, -700.0))
    for row in range(3):
        for column in range(3):
            offsetX = (column - 1) * 180.0
            offsetZ = (row - 1) * 180.0
            document.AddNode(
                "PlatformTile_%d_%d" % (row, column),
                parentIndex=platformGrid,
                meshIndex=platformMesh,
                translation=(offsetX, 0.0, offsetZ),
            )

    #-----------------------------------------------------------------
    # 柱の列（左右 2 列 x 3 本。1 本は Base / Shaft / Cap の 3 段重ね ➡ 深さ 4 の階層になる）
    #-----------------------------------------------------------------
    def AddColonnade(name, x, rotation):
        colonnade = document.AddNode(name, parentIndex=stageRoot, translation=(x, 0.0, 0.0), rotation=rotation)
        for index, z in enumerate((-200.0, 0.0, 200.0)):
            column = document.AddNode("Column_%d" % index, parentIndex=colonnade, translation=(0.0, 0.0, z))
            document.AddNode("PillarBase", parentIndex=column, meshIndex=pillarBaseMesh, translation=(0.0, 10.0, 0.0))
            document.AddNode(
                "PillarShaft", parentIndex=column, meshIndex=pillarShaftMesh, translation=(0.0, 90.0, 0.0)
            )
            document.AddNode("PillarCap", parentIndex=column, meshIndex=pillarCapMesh, translation=(0.0, 170.0, 0.0))

    AddColonnade("ColonnadeLeft", -260.0, rotation=None)
    # 右列は 180 度回して積む。柱は左右対称な形なので見た目は変わらず、回転付きノードを混ぜられる。
    AddColonnade("ColonnadeRight", 260.0, rotation=QuaternionFromAxisAngle((0.0, 1.0, 0.0), math.pi))

    #-----------------------------------------------------------------
    # 中央の門(左右の柱の頭から中央の頂点へ渡す梁 2 本 + 上に乗る屋根)
    #-----------------------------------------------------------------
    archGate = document.AddNode("ArchGate", parentIndex=stageRoot)
    beamAngle = math.atan2(70.0, 260.0)
    document.AddNode(
        "ArchBeamLeft",
        parentIndex=archGate,
        meshIndex=archBeamMesh,
        translation=(-130.0, 215.0, 0.0),
        rotation=QuaternionFromAxisAngle((0.0, 0.0, 1.0), beamAngle),
    )
    document.AddNode(
        "ArchBeamRight",
        parentIndex=archGate,
        meshIndex=archBeamMesh,
        translation=(130.0, 215.0, 0.0),
        rotation=QuaternionFromAxisAngle((0.0, 0.0, 1.0), -beamAngle),
    )
    document.AddNode(
        "RoofSlab",
        parentIndex=stageRoot,
        meshIndex=roofSlabMesh,
        translation=(0.0, 260.0, 0.0),
        scale=(1.3, 1.0, 1.6),
    )

    #-----------------------------------------------------------------
    # 階段(奥へ向かって 3 段。幅を段ごとに縮めて上るほど狭くなる形にする)
    #-----------------------------------------------------------------
    staircase = document.AddNode("Staircase", parentIndex=stageRoot, translation=(0.0, 0.0, 500.0))
    document.AddNode(
        "StepBox1", parentIndex=staircase, meshIndex=stepBox1Mesh, translation=(0.0, 10.0, 0.0)
    )
    document.AddNode(
        "StepBox2",
        parentIndex=staircase,
        meshIndex=stepBox2Mesh,
        translation=(0.0, 30.0, -70.0),
        scale=(0.8, 1.0, 1.0),
    )
    document.AddNode(
        "StepBox3",
        parentIndex=staircase,
        meshIndex=stepBox3Mesh,
        translation=(0.0, 50.0, -140.0),
        scale=(0.6, 1.0, 1.0),
    )

    #-----------------------------------------------------------------
    # 四隅の目印(同じ四角錐を 4 ノードから参照する)
    #-----------------------------------------------------------------
    markers = document.AddNode("Markers", parentIndex=stageRoot)
    for name, x, z in (
        ("MarkerNE", 450.0, 450.0),
        ("MarkerNW", -450.0, 450.0),
        ("MarkerSE", 450.0, -450.0),
        ("MarkerSW", -450.0, -450.0),
    ):
        document.AddNode(name, parentIndex=markers, meshIndex=markerMesh, translation=(x, 0.0, z))

    #-----------------------------------------------------------------
    # 門の両脇の幟(内側へ傾けて立てる。同じ板を 2 ノードから参照する)
    #-----------------------------------------------------------------
    banners = document.AddNode("Banners", parentIndex=stageRoot)
    bannerAngle = math.radians(30.0)
    document.AddNode(
        "BannerLeft",
        parentIndex=banners,
        meshIndex=bannerMesh,
        translation=(-320.0, 0.0, 0.0),
        rotation=QuaternionFromAxisAngle((0.0, 1.0, 0.0), bannerAngle),
    )
    document.AddNode(
        "BannerRight",
        parentIndex=banners,
        meshIndex=bannerMesh,
        translation=(320.0, 0.0, 0.0),
        rotation=QuaternionFromAxisAngle((0.0, 1.0, 0.0), -bannerAngle),
    )

    #-----------------------------------------------------------------
    # 井戸(非一様スケールを 1 本だけ単独で使う)
    #-----------------------------------------------------------------
    document.AddNode(
        "Well", parentIndex=stageRoot, meshIndex=wellMesh, translation=(0.0, 25.0, 700.0), scale=(1.3, 1.0, 1.3)
    )

    return document


def main():
    if not os.path.isdir(OUTPUT_DIRECTORY):
        print("出力先が無い: %s" % OUTPUT_DIRECTORY)
        return 1

    document = BuildStage()
    gltfDictionary = document.Build()

    with open(BIN_PATH, "wb") as binFile:
        binFile.write(document.bufferBytes)

    with io.open(GLTF_PATH, "w", encoding="utf-8", newline="\n") as gltfFile:
        json.dump(gltfDictionary, gltfFile, ensure_ascii=False, indent=1)
        gltfFile.write("\n")

    instanceCount = sum(1 for node in document.nodes if "mesh" in node)
    print(
        "ステージの glTF を書き出した: メッシュ %d 個 / ノード %d 個(配置 %d 個) / .bin %d バイト"
        % (len(document.meshes), len(document.nodes), instanceCount, len(document.bufferBytes))
    )
    print(GLTF_PATH)
    print(BIN_PATH)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
