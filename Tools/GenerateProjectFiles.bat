@echo off
rem ファイル一覧と .vcxproj.filters を作り直す。VS の「ツール → 外部ツール」に登録しても良い。
py "%~dp0GenerateProjectFiles.py" %*
if errorlevel 1 pause
