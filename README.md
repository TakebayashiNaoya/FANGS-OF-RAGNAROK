# FANGS OF RAGNAROK

北欧神話を世界観とした3Dアクションゲーム。DirectX 12 + C++20で自作するエンジン(FangEngine)の上に作る。

- 対応プラットフォーム: Windows (x64) / Xbox One
- 開発環境: Visual Studio 2026

## 動作に必要な環境

- Windows 10 / 11(64bit)
- DirectX 12に対応したGPU

## 現在の状態

コードとビルド設定をいったん外し、下の層から作り直している。
最初はメモリ確保(`malloc`を包むところ)から始め、Windows x64のコンソールexe 1本で進める。
Xbox(UWP)向けの構成は、実機で動かす段で足す。
