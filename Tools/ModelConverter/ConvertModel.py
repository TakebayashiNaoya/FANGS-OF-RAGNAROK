"""glTFのモデルを、エンジンが読む.fmodelに焼く。

使い方:
    python ConvertModel.py <入力の.gltf> [出力の.fmodel]

出力を省いたときは、入力と同じ場所に同じ名前の.fmodelを書く。
glTF Separate（.gltfと.binが別のファイル）だけを読む。

.fmodelの並び（リトルエンディアン）:
    ヘッダー: "FMDL"、版（uint32）、メッシュの数（uint32）
    メッシュごと: 頂点の数（uint32）、インデックスの数（uint32）、インデックス1つのバイト数（uint32、2か4）
                  頂点の列（1頂点32バイト: 位置float3、法線float3、UV float2）
                  インデックスの列
"""
import json
import struct
import sys
from pathlib import Path

MAGIC   = b"FMDL"
VERSION = 1

# glTFのcomponentTypeと、struct の書式とバイト数の対応
COMPONENT_FORMATS = {
    5120: ("b", 1),  # BYTE
    5121: ("B", 1),  # UNSIGNED_BYTE
    5122: ("h", 2),  # SHORT
    5123: ("H", 2),  # UNSIGNED_SHORT
    5125: ("I", 4),  # UNSIGNED_INT
    5126: ("f", 4),  # FLOAT
}

# 正規化した整数を0〜1（符号付きは-1〜1）へ戻すための最大値
NORMALIZED_MAXIMUMS = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}

# glTFのtypeと、要素の数の対応
TYPE_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

TRIANGLES = 4  # primitiveのmodeで三角形を表す値


class ConvertError(Exception):
    pass


def load_buffers(gltf, gltf_path):
    """bufferのファイルを全部読む。"""
    buffers = []
    for buffer in gltf["buffers"]:
        uri = buffer.get("uri")
        if uri is None or uri.startswith("data:"):
            raise ConvertError("glTF Separateだけを読むため、.binのファイルが要る")
        buffers.append((gltf_path.parent / uri).read_bytes())
    return buffers


def read_accessor(gltf, buffers, accessor_index):
    """accessorの値を、要素ごとのタプルの配列で返す。"""
    accessor = gltf["accessors"][accessor_index]
    if "sparse" in accessor:
        raise ConvertError(f"accessor {accessor_index}: sparseは読めない")
    if "bufferView" not in accessor:
        raise ConvertError(f"accessor {accessor_index}: bufferViewがない")

    component_type = accessor["componentType"]
    component_format, component_size = COMPONENT_FORMATS[component_type]
    count_per_element = TYPE_COUNTS[accessor["type"]]
    element_size = component_size * count_per_element

    buffer_view = gltf["bufferViews"][accessor["bufferView"]]
    data = buffers[buffer_view["buffer"]]
    start = buffer_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = buffer_view.get("byteStride", element_size)

    element_format = "<" + component_format * count_per_element
    elements = [struct.unpack_from(element_format, data, start + stride * i) for i in range(accessor["count"])]

    if accessor.get("normalized", False):
        maximum = NORMALIZED_MAXIMUMS[component_type]
        elements = [tuple(max(value / maximum, -1.0) for value in element) for element in elements]
    return elements, component_type


def convert_primitive(gltf, buffers, primitive, name):
    """primitiveを1つ、.fmodelのメッシュ1つぶんのバイト列にする。"""
    if primitive.get("mode", TRIANGLES) != TRIANGLES:
        raise ConvertError(f"{name}: 三角形のprimitiveだけを読む")

    attributes = primitive["attributes"]
    for required in ("POSITION", "NORMAL", "TEXCOORD_0"):
        if required not in attributes:
            raise ConvertError(f"{name}: {required}がない")

    positions, _ = read_accessor(gltf, buffers, attributes["POSITION"])
    normals,   _ = read_accessor(gltf, buffers, attributes["NORMAL"])
    uvs,       _ = read_accessor(gltf, buffers, attributes["TEXCOORD_0"])
    vertex_count = len(positions)

    if "indices" in primitive:
        indices, component_type = read_accessor(gltf, buffers, primitive["indices"])
        indices = [element[0] for element in indices]
    else:
        indices = list(range(vertex_count))
        component_type = 5125 if vertex_count > 0xFFFF else 5123
    if len(indices) % 3 != 0:
        raise ConvertError(f"{name}: インデックスの数が3の倍数でない")

    # u8はDirectX 12のインデックスにないため、u16に広げる
    index_size = 4 if component_type == 5125 else 2

    # glTFは右手系、エンジンは左手系のため、zを反転する
    vertex_bytes = bytearray()
    for (px, py, pz), (nx, ny, nz), (u, v) in zip(positions, normals, uvs):
        vertex_bytes += struct.pack("<8f", px, py, -pz, nx, ny, -nz, u, v)

    # zを反転すると表の巻き順も逆になるため、三角形の2番目と3番目を入れ替える
    for i in range(0, len(indices), 3):
        indices[i + 1], indices[i + 2] = indices[i + 2], indices[i + 1]
    index_bytes = struct.pack("<" + ("I" if index_size == 4 else "H") * len(indices), *indices)

    header = struct.pack("<3I", vertex_count, len(indices), index_size)
    return header + vertex_bytes + index_bytes, vertex_count, len(indices), index_size


def convert(gltf_path, output_path):
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    buffers = load_buffers(gltf, gltf_path)

    # ノードの変換はかけない。
    # スキンの付いたメッシュはglTFの決まりでノードの変換を無視し、頂点はもうメートルで入っているため。
    meshes = []
    for mesh_index, mesh in enumerate(gltf["meshes"]):
        for primitive_index, primitive in enumerate(mesh["primitives"]):
            name = f"mesh {mesh_index} ({mesh.get('name', '')}) primitive {primitive_index}"
            meshes.append((name, convert_primitive(gltf, buffers, primitive, name)))

    with output_path.open("wb") as file:
        file.write(struct.pack("<4s2I", MAGIC, VERSION, len(meshes)))
        for _, (mesh_bytes, _, _, _) in meshes:
            file.write(mesh_bytes)

    total_vertices = 0
    total_indices = 0
    for name, (_, vertex_count, index_count, index_size) in meshes:
        print(f"{name}: 頂点 {vertex_count}、インデックス {index_count}（{index_size}バイト）")
        total_vertices += vertex_count
        total_indices += index_count
    print(f"合計: メッシュ {len(meshes)}、頂点 {total_vertices}、三角形 {total_indices // 3}")
    print(f"書き出した: {output_path}（{output_path.stat().st_size}バイト）")


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 1
    gltf_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2]) if len(sys.argv) == 3 else gltf_path.with_suffix(".fmodel")
    try:
        convert(gltf_path, output_path)
    except ConvertError as error:
        print(f"変換できなかった: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
