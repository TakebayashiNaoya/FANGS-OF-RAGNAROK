#!/usr/bin/env python3
"""ディスクを走査して .vcxproj のファイル一覧と .vcxproj.filters を書き直す。

VC プロジェクトはワイルドカードを公式にはサポートしないので、ファイル一覧は実名で持つ。
手で並べると追加し忘れが起きるため、このスクリプトで生成する。

    py Tools\\GenerateProjectFiles.py           一覧を書き直す
    py Tools\\GenerateProjectFiles.py --check   ずれていたら 1 を返す（何も書かない）

.vcxproj のうち書き換えるのは FANG_FILES_BEGIN / FANG_FILES_END で挟まれた範囲だけ。
"""

import argparse
import hashlib
import io
import os
import sys
import uuid


ROOT_DIRECTORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BEGIN_MARKER = "<!-- FANG_FILES_BEGIN 自動生成。Tools\\GenerateProjectFiles.py が書き換える。手で編集しない -->"
END_MARKER = "<!-- FANG_FILES_END -->"

HEADER_EXTENSIONS = (".h", ".inl")
SOURCE_EXTENSIONS = (".cpp",)

# 構成名で Xbox かどうかが決まるので、$(Platform) ではなく $(FangIsXbox) を見る
WINDOWS_ONLY_CONDITION = "'$(FangIsXbox)' != 'true'"
XBOX_ONLY_CONDITION = "'$(FangIsXbox)' == 'true'"

ENGINE_MODULE_NAMES = [
    "Core", "RHI", "Renderer", "Collision", "Animation", "Resource", "Scene",
    "Input", "Audio", "UI", "AI", "Effects", "Runtime", "Editor",
]


class Project:
    """一覧を生成する対象のプロジェクト 1 つ分。"""

    def __init__(self, project_path, source_roots, platform_directories=None, skip_directories=()):
        self.project_path = project_path
        self.source_roots = source_roots
        # ディレクトリ名 -> その中のファイルに付ける Condition
        self.platform_directories = platform_directories or {}
        self.skip_directories = set(skip_directories)

    @property
    def directory(self):
        return os.path.dirname(self.project_path)

    @property
    def filters_path(self):
        return self.project_path + ".filters"


def MakeEngineModule(name):
    return Project(
        # Public / Private は廃止したので、モジュール直下をそのまま走査する
        project_path=os.path.join(ROOT_DIRECTORY, "Engine", name, name + ".vcxproj"),
        source_roots=["."],
        platform_directories={
            "Windows": WINDOWS_ONLY_CONDITION,
            "Xbox": XBOX_ONLY_CONDITION,
        },
        # ビルドの中間生成物。VS が .vcxproj の隣に作る
        skip_directories=["Generated Files"],
    )


def CollectProjects():
    projects = [MakeEngineModule(name) for name in ENGINE_MODULE_NAMES]

    projects.append(Project(
        project_path=os.path.join(ROOT_DIRECTORY, "Game", "Game.vcxproj"),
        source_roots=["Source"],
        skip_directories=["UWP"]))

    projects.append(Project(
        project_path=os.path.join(ROOT_DIRECTORY, "Game", "GameUWP.vcxproj"),
        source_roots=["Source"],
        skip_directories=["Windows"]))

    projects.append(Project(
        project_path=os.path.join(ROOT_DIRECTORY, "Tests", "Tests.vcxproj"),
        source_roots=["Source"]))

    projects.append(Project(
        project_path=os.path.join(ROOT_DIRECTORY, "Tools", "AssetBuilder", "AssetBuilder.vcxproj"),
        source_roots=["Source"]))

    return projects


def FindFiles(project):
    """プロジェクト内のファイルを (相対パス, Condition) の並びで返す。"""
    found = []
    for source_root in project.source_roots:
        root_directory = os.path.join(project.directory, source_root)
        if not os.path.isdir(root_directory):
            continue

        for current_directory, subdirectory_names, file_names in os.walk(root_directory):
            subdirectory_names[:] = sorted(
                name for name in subdirectory_names if name not in project.skip_directories)

            relative_directory = os.path.relpath(current_directory, project.directory)
            components = relative_directory.split(os.sep)
            condition = None
            for component in components:
                if component in project.platform_directories:
                    condition = project.platform_directories[component]

            for file_name in sorted(file_names):
                extension = os.path.splitext(file_name)[1].lower()
                if extension not in HEADER_EXTENSIONS + SOURCE_EXTENSIONS:
                    continue

                # source_roots が "." のときは relpath が "." になるので、頭に .\ を付けない
                if relative_directory == os.curdir:
                    relative_path = file_name
                else:
                    relative_path = os.path.join(relative_directory, file_name)

                found.append((relative_path.replace("/", "\\"), condition))

    return found


def MakeFilterGuid(filter_name):
    """フィルタ名から決め打ちの GUID を作る。再生成しても変わらないようにするため。"""
    digest = hashlib.md5(("FangsOfRagnarok/Filter/" + filter_name).encode("utf-8")).digest()
    return "{" + str(uuid.UUID(bytes=digest)).upper() + "}"


def BuildItemElement(item_type, relative_path, condition):
    attributes = 'Include="%s"' % relative_path
    if condition is not None:
        attributes += ' Condition="%s"' % condition

    # PCH を作る TU だけは追加のメタデータが要る
    if relative_path.lower() == "pch.cpp":
        return ("    <%s %s>\r\n"
                "      <PrecompiledHeader>Create</PrecompiledHeader>\r\n"
                "    </%s>\r\n") % (item_type, attributes, item_type)

    return "    <%s %s />\r\n" % (item_type, attributes)


def BuildProjectItems(files):
    headers = [entry for entry in files if os.path.splitext(entry[0])[1].lower() in HEADER_EXTENSIONS]
    sources = [entry for entry in files if os.path.splitext(entry[0])[1].lower() in SOURCE_EXTENSIONS]

    text = ""
    for item_type, entries in (("ClInclude", headers), ("ClCompile", sources)):
        if not entries:
            continue

        text += "  <ItemGroup>\r\n"
        for relative_path, condition in entries:
            text += BuildItemElement(item_type, relative_path, condition)
        text += "  </ItemGroup>\r\n"

    return text


def BuildFiltersDocument(files):
    filter_names = set()
    for relative_path, _ in files:
        directory = os.path.dirname(relative_path)
        while directory:
            filter_names.add(directory)
            directory = os.path.dirname(directory)

    text = '﻿<?xml version="1.0" encoding="utf-8"?>\r\n'
    text += "<!-- 自動生成。Tools\\GenerateProjectFiles.py が書き換える。手で編集しない -->\r\n"
    text += '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\r\n'

    text += "  <ItemGroup>\r\n"
    for filter_name in sorted(filter_names):
        text += '    <Filter Include="%s">\r\n' % filter_name
        text += "      <UniqueIdentifier>%s</UniqueIdentifier>\r\n" % MakeFilterGuid(filter_name)
        text += "    </Filter>\r\n"
    text += "  </ItemGroup>\r\n"

    headers = [entry for entry in files if os.path.splitext(entry[0])[1].lower() in HEADER_EXTENSIONS]
    sources = [entry for entry in files if os.path.splitext(entry[0])[1].lower() in SOURCE_EXTENSIONS]

    for item_type, entries in (("ClInclude", headers), ("ClCompile", sources)):
        if not entries:
            continue

        text += "  <ItemGroup>\r\n"
        for relative_path, _ in entries:
            directory = os.path.dirname(relative_path)
            if directory:
                text += '    <%s Include="%s">\r\n' % (item_type, relative_path)
                text += "      <Filter>%s</Filter>\r\n" % directory
                text += "    </%s>\r\n" % item_type
            else:
                text += '    <%s Include="%s" />\r\n' % (item_type, relative_path)
        text += "  </ItemGroup>\r\n"

    text += "</Project>\r\n"
    return text


def BuildProjectDocument(project):
    with io.open(project.project_path, encoding="utf-8-sig", newline="") as project_file:
        text = project_file.read()

    begin = text.find(BEGIN_MARKER)
    end = text.find(END_MARKER)
    if begin < 0 or end < 0:
        raise SystemExit("%s に FANG_FILES_BEGIN / FANG_FILES_END がありません" % project.project_path)

    files = FindFiles(project)
    replacement = BEGIN_MARKER + "\r\n" + BuildProjectItems(files) + "  "
    return text[:begin] + replacement + text[end:], files


def WriteIfChanged(path, text, is_check_only):
    existing = None
    if os.path.exists(path):
        with io.open(path, encoding="utf-8-sig", newline="") as existing_file:
            existing = existing_file.read()

    # utf-8-sig で読むと BOM が落ちるので、比較の前に揃える
    normalized = text[1:] if text.startswith("﻿") else text
    if existing == normalized:
        return False

    if not is_check_only:
        with io.open(path, "w", encoding="utf-8-sig", newline="") as output_file:
            output_file.write(normalized)

    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="書き換えずに、ずれていたら 1 を返す")
    arguments = parser.parse_args()

    changed_paths = []
    for project in CollectProjects():
        project_text, files = BuildProjectDocument(project)
        if WriteIfChanged(project.project_path, project_text, arguments.check):
            changed_paths.append(project.project_path)

        if WriteIfChanged(project.filters_path, BuildFiltersDocument(files), arguments.check):
            changed_paths.append(project.filters_path)

        print("%-14s %d 個" % (os.path.basename(project.project_path), len(files)))

    if not changed_paths:
        print("すべて最新です")
        return 0

    if arguments.check:
        print("\n一覧がずれています。py Tools\\GenerateProjectFiles.py を実行してください:")
        for path in changed_paths:
            print("  " + os.path.relpath(path, ROOT_DIRECTORY))
        return 1

    print("\n%d ファイルを書き直しました" % len(changed_paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
