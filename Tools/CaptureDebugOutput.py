#!/usr/bin/env python3
"""OutputDebugString(FANG_LOG_* の出力先)を DBWIN_BUFFER から読んで標準出力へ流す。

デバッガを繋がずに FANG_LOG_* の出力を確かめたいときに使う（Debug / Preview だけが対象。
Release はログごとコンパイルから外れる）。DBWIN_BUFFER は Windows がプロセス間で 1 つだけ
持つ共有メモリで、どのプロセスの OutputDebugString もここへ届く。既に別のデバッガ
（Visual Studio など）が同じ名前のイベントを握っていると起動に失敗する。

    py Tools\\CaptureDebugOutput.py --seconds 60
    py Tools\\CaptureDebugOutput.py --seconds 60 > capture.log

Ctrl+C でも終了できる。
"""

import argparse
import ctypes
import datetime
import sys
from ctypes import wintypes


BUFFER_SIZE = 4096
PROCESS_ID_SIZE = 4
MESSAGE_SIZE = BUFFER_SIZE - PROCESS_ID_SIZE

WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102
FILE_MAP_ALL_ACCESS = 0x000F001F
POLL_TIMEOUT_MILLISECONDS = 200


def PrepareKernel32():
    """kernel32 の呼ぶ関数だけ、64bit を切り詰めないよう戻り値・引数の型を明示する。

    ctypes は既定で戻り値を c_int(32bit) とみなす。HANDLE やポインタをそのまま返す
    関数にこれを付け忘れると、上位 32bit が切り捨てられて別のアドレスを指してしまう
    （MapViewOfFile のポインタで実際に踏んだ地雷）。
    """
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    kernel32.CreateEventW.restype = wintypes.HANDLE
    kernel32.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]

    kernel32.CreateFileMappingW.restype = wintypes.HANDLE
    kernel32.CreateFileMappingW.argtypes = [
        wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR
    ]

    kernel32.MapViewOfFile.restype = wintypes.LPVOID
    kernel32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]

    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]

    kernel32.SetEvent.restype = wintypes.BOOL
    kernel32.SetEvent.argtypes = [wintypes.HANDLE]

    kernel32.UnmapViewOfFile.restype = wintypes.BOOL
    kernel32.UnmapViewOfFile.argtypes = [wintypes.LPVOID]

    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    return kernel32


def OpenDbWinBuffer():
    """DBWIN 系のイベントと共有メモリを開く（無ければ作る）。開けなければ None。"""
    kernel32 = PrepareKernel32()

    buffer_ready = kernel32.CreateEventW(None, False, True, "DBWIN_BUFFER_READY")
    data_ready = kernel32.CreateEventW(None, False, False, "DBWIN_DATA_READY")
    if not buffer_ready or not data_ready:
        return None

    mapping = kernel32.CreateFileMappingW(-1, None, 0x04, 0, BUFFER_SIZE, "DBWIN_BUFFER")
    if not mapping:
        return None

    view = kernel32.MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, BUFFER_SIZE)
    if not view:
        return None

    return kernel32, buffer_ready, data_ready, mapping, view


def ReadMessage(view):
    """共有メモリから (プロセス ID, 文字列) を読む。"""
    raw = ctypes.string_at(view, BUFFER_SIZE)
    process_id = int.from_bytes(raw[0:PROCESS_ID_SIZE], byteorder="little", signed=False)

    message_bytes = raw[PROCESS_ID_SIZE:]
    terminator = message_bytes.find(b"\x00")
    if terminator >= 0:
        message_bytes = message_bytes[:terminator]

    # FANG_LOG_* は /utf-8 でビルドした std::string を OutputDebugStringA に渡すので UTF-8 で読む
    # （システムの ANSI コードページでは化ける）。
    message = message_bytes.decode("utf-8", errors="replace")
    return process_id, message


def Capture(seconds, out):
    """seconds 秒のあいだ DBWIN_BUFFER を読み続け、1 行ずつ out へ書く。"""
    handles = OpenDbWinBuffer()
    if handles is None:
        print("DBWIN_BUFFER を開けなかった（他のデバッガが握っている可能性）", file=sys.stderr)
        return 1

    kernel32, buffer_ready, data_ready, mapping, view = handles

    deadline = datetime.datetime.now() + datetime.timedelta(seconds=seconds)
    try:
        while datetime.datetime.now() < deadline:
            waitResult = kernel32.WaitForSingleObject(data_ready, POLL_TIMEOUT_MILLISECONDS)
            if waitResult != WAIT_OBJECT_0:
                continue

            process_id, message = ReadMessage(view)
            kernel32.SetEvent(buffer_ready)  # 読み終えたので次の書き手へ席を返す。

            timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            line = message if message.endswith("\n") else message + "\n"
            out.write(f"[{timestamp} pid {process_id}] {line}")
            out.flush()
    finally:
        kernel32.UnmapViewOfFile(view)
        kernel32.CloseHandle(mapping)
        kernel32.CloseHandle(data_ready)
        kernel32.CloseHandle(buffer_ready)

    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=60.0, help="読み続ける秒数(既定 60)")
    args = parser.parse_args()

    # コンソールの無いリダイレクト先ではコードページが cp932 になり、mbcs の置換文字（U+FFFD）を
    # 書き戻せずに落ちることがある。出力は常に UTF-8 に固定する。
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    try:
        return Capture(args.seconds, sys.stdout)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
