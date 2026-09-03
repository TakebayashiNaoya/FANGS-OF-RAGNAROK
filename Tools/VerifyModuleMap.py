#!/usr/bin/env python3
"""ModuleMap.json の deps を .vcxproj の実体（FangModuleDependencies）と突き合わせる。

ModuleMap.json は layers / desc / phase / kind / note に人が書いた意味情報を持つので、
このスクリプトは deps 配列だけを対象にする。他のキーは触らない。

    py Tools\\VerifyModuleMap.py                  差分を表で表示する（書き換えない）
    py Tools\\VerifyModuleMap.py --update          deps だけを実体で書き換えて保存する
    py Tools\\VerifyModuleMap.py --map <path>      ModuleMap.json の場所を指定する

ModuleMap.json はこのリポジトリの外（既定で ..\\FANGS-OF-RAGNAROK-notes\\docs\\Engine\\ModuleMap.json）にある。
"""

import argparse
import datetime
import io
import json
import os
import re
import sys
import unicodedata


ROOT_DIRECTORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLUTION_PATH = os.path.join(ROOT_DIRECTORY, "FangsOfRagnarok.sln")
DEFAULT_MAP_PATH = os.path.join(
    ROOT_DIRECTORY, "..", "FANGS-OF-RAGNAROK-notes", "docs", "Engine", "ModuleMap.json")

# .sln の Project 行のうち C++ プロジェクトを指す種別 GUID（フォルダ項目は別の GUID を持つ）
CPP_PROJECT_TYPE_GUID = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"

PROJECT_LINE_PATTERN = re.compile(
    r'Project\("\{' + CPP_PROJECT_TYPE_GUID + r'\}"\)\s*=\s*"([^"]+)",\s*"([^"]+)"')

FANG_MODULE_DEPENDENCIES_PATTERN = re.compile(
    r"<FangModuleDependencies[^>]*>([^<]*)</FangModuleDependencies>")

# ModuleMap.json は 1 モジュール 1 行。行の中の "deps": [...] だけを狙い撃ちする
MODULE_LINE_PATTERN = re.compile(r'("name":\s*"([^"]+)".*?"deps":\s*)(\[[^\]]*\])')

# meta ブロックはインデント 1 個、閉じは "\t}," の 1 行。閉じ行の手前までを差し替え対象にする
META_BLOCK_PATTERN = re.compile(r'("meta":\s*\{)(.*?)(\n\t\},)', re.DOTALL)

NEW_SOURCE = (
    "deps は Tools/VerifyModuleMap.py で .vcxproj の FangModuleDependencies から検証。"
    "layers / desc / phase / note は 01_アーキテクチャ.md 1 / 02_ソリューション構成.md 3 を手で転記したまま")
NEW_NOTE = (
    "deps が実体とずれたら Tools/VerifyModuleMap.py --update で直す。"
    "ModuleMap.html は本ファイルと同じデータを埋め込みで持つ")


def FindSolutionProjects():
    """.sln から C++ プロジェクトの (名前, .vcxproj の絶対パス) を集める。"""
    with io.open(SOLUTION_PATH, encoding="utf-8-sig") as solution_file:
        text = solution_file.read()

    projects = []
    for name, relative_path in PROJECT_LINE_PATTERN.findall(text):
        if not relative_path.lower().endswith(".vcxproj"):
            continue
        projects.append((name, os.path.normpath(os.path.join(ROOT_DIRECTORY, relative_path))))

    return projects


def ReadActualDependencies(vcxproj_path):
    """.vcxproj の FangModuleDependencies を実体の依存名の並びで返す。

    Editor が Release 構成で外れる、のような条件付きの追記も区別せず全部含める
    （ModuleMap.json 側も条件を書き分けていないため、note 列で言及するだけの扱いに合わせる）。
    FangModuleDependencies を持たないプロジェクト（ThirdParty の imgui / ozz 等）は None を返す。
    """
    with io.open(vcxproj_path, encoding="utf-8-sig") as project_file:
        text = project_file.read()

    matches = FANG_MODULE_DEPENDENCIES_PATTERN.findall(text)
    if not matches:
        return None

    names = []
    for raw_value in matches:
        # 2 個目以降は "$(FangModuleDependencies);Editor" のように自分自身を参照して追記する
        value = raw_value.replace("$(FangModuleDependencies)", ";".join(names))
        names = [name for name in value.split(";") if name]

    return names


def CollectActualDependencies():
    """モジュール名 -> 実体の deps（.vcxproj に書かれた順）の辞書を返す。"""
    actual = {}
    for name, vcxproj_path in FindSolutionProjects():
        dependencies = ReadActualDependencies(vcxproj_path)
        if dependencies is None:
            continue  # FangModule の対象外（ThirdParty など）

        actual[name] = dependencies

    return actual


def LoadModuleList(map_text):
    """ModuleMap.json の modules 配列から (名前, deps) の並びを返す。"""
    data = json.loads(map_text)
    return [(module["name"], list(module.get("deps", []))) for module in data["modules"]]


def ComputeDifferences(json_modules, actual_by_name):
    """deps の差分と、モジュール一覧そのものの食い違いを求める。"""
    mismatches = []
    seen_names = set()

    for name, deps in json_modules:
        seen_names.add(name)
        if name not in actual_by_name:
            continue  # モジュール一覧の食い違いとして別に報告する

        actual = actual_by_name[name]
        only_json = [item for item in deps if item not in actual]
        only_actual = [item for item in actual if item not in deps]
        if only_json or only_actual:
            mismatches.append((name, only_json, only_actual))

    missing_in_json = [name for name in actual_by_name if name not in seen_names]
    missing_in_actual = [name for name, _ in json_modules if name not in actual_by_name]

    return mismatches, missing_in_json, missing_in_actual


def DisplayWidth(text):
    """全角文字を 2、半角を 1 として数えた見た目の幅。表の桁揃えに使う。"""
    return sum(2 if unicodedata.east_asian_width(character) in ("W", "F") else 1 for character in text)


def PadToWidth(text, width):
    return text + " " * max(0, width - DisplayWidth(text))


def PrintTable(headers, rows):
    widths = [DisplayWidth(header) for header in headers]
    for row in rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], DisplayWidth(cell))

    def FormatRow(cells):
        return "  ".join(PadToWidth(cell, widths[index]) for index, cell in enumerate(cells))

    print(FormatRow(headers))
    print(FormatRow(["-" * (widths[index] // 2 + 1) for index in range(len(headers))]))
    for row in rows:
        print(FormatRow(row))


def PrintReport(mismatches, missing_in_json, missing_in_actual):
    if missing_in_json:
        print("vcxproj にあって ModuleMap.json に無いモジュール: %s" % ", ".join(missing_in_json))

    if missing_in_actual:
        print("ModuleMap.json にあって .sln の .vcxproj に無いモジュール: %s" % ", ".join(missing_in_actual))

    if missing_in_json or missing_in_actual:
        print("")

    if not mismatches:
        print("deps の差分: なし")
        return

    print("deps の差分:")
    rows = [
        (name, ", ".join(only_json) if only_json else "-", ", ".join(only_actual) if only_actual else "-")
        for name, only_json, only_actual in mismatches
    ]
    PrintTable(["モジュール", "json のみ（実体に無い）", "vcxproj のみ（json に無い）"], rows)


def MergeDependencyOrder(existing_deps, actual_deps):
    """実体の集合に合わせつつ、手書きの並び順（依存の深い順）はできるだけ保つ。

    既存の並びのうち実体にも残っているものはその順序をそのまま使い、実体にしか無い新規分だけ
    .vcxproj の宣言順で末尾に足す。実体から消えたものは落とす。
    """
    actual_set = set(actual_deps)
    kept = [name for name in existing_deps if name in actual_set]
    kept_set = set(kept)
    added = [name for name in actual_deps if name not in kept_set]
    return kept + added


def FormatDependencyArray(names):
    if not names:
        return "[]"

    return "[" + ", ".join('"%s"' % name for name in names) + "]"


def UpdateMetaBlock(text):
    today = datetime.date.today().isoformat()

    def ReplaceField(inner, field_name, new_value):
        pattern = re.compile(r'("%s":\s*)"[^"]*"' % field_name)
        return pattern.sub(lambda match: match.group(1) + '"%s"' % new_value, inner)

    def ReplaceBlock(match):
        inner = match.group(2)
        inner = ReplaceField(inner, "updated", today)
        inner = ReplaceField(inner, "source", NEW_SOURCE)
        inner = ReplaceField(inner, "note", NEW_NOTE)
        return match.group(1) + inner + match.group(3)

    return META_BLOCK_PATTERN.sub(ReplaceBlock, text, count=1)


def ApplyUpdate(map_text, actual_by_name):
    """deps の行だけを書き換えたテキストと、実際に変わったモジュール名の一覧を返す。"""
    lines = map_text.split("\n")
    updated_names = []

    for index, line in enumerate(lines):
        match = MODULE_LINE_PATTERN.search(line)
        if not match:
            continue

        name = match.group(2)
        if name not in actual_by_name:
            continue  # 実体が無いモジュールは触らない（差分としては報告済み）

        existing_deps = json.loads(match.group(3))
        new_deps = MergeDependencyOrder(existing_deps, actual_by_name[name])
        if new_deps == existing_deps:
            continue

        lines[index] = line[:match.start(3)] + FormatDependencyArray(new_deps) + line[match.end(3):]
        updated_names.append(name)

    return UpdateMetaBlock("\n".join(lines)), updated_names


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="deps を実体で書き換えて保存する")
    parser.add_argument("--map", dest="map_path", default=DEFAULT_MAP_PATH,
                         help="ModuleMap.json の場所（既定: %s）" % os.path.normpath(DEFAULT_MAP_PATH))
    arguments = parser.parse_args()

    map_path = os.path.abspath(arguments.map_path)
    if not os.path.isfile(map_path):
        print("ModuleMap.json が見つかりません: %s" % map_path)
        return 1

    with io.open(map_path, encoding="utf-8-sig") as map_file:
        map_text = map_file.read()

    json_modules = LoadModuleList(map_text)
    actual_by_name = CollectActualDependencies()

    mismatches, missing_in_json, missing_in_actual = ComputeDifferences(json_modules, actual_by_name)
    PrintReport(mismatches, missing_in_json, missing_in_actual)

    is_consistent = not mismatches and not missing_in_json and not missing_in_actual

    if not arguments.update:
        print("")
        if is_consistent:
            print("ModuleMap.json の deps は .vcxproj の実体と一致しています")
            return 0

        print("ModuleMap.json の deps が実体とずれています。--update で書き換えられます")
        return 1

    updated_text, updated_names = ApplyUpdate(map_text, actual_by_name)
    with io.open(map_path, "w", encoding="utf-8", newline="") as map_file:
        map_file.write(updated_text)

    print("")
    if updated_names:
        print("deps を実体に合わせて更新しました: %s" % ", ".join(updated_names))
    else:
        print("deps に差分は無かったので meta だけ更新しました")
    print(map_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
