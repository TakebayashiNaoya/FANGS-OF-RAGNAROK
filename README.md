# FANGS OF RAGNAROK

北欧神話を世界観とした 3D アクションゲーム。DirectX 12 + C++20 で自作するエンジン（FangEngine）の上に作る。

- 対応プラットフォーム: Windows (x64) / Xbox One
- 開発環境: Visual Studio 2026

## ビルド

`FangsOfRagnarok.sln` を開いて `Game` をスタートアッププロジェクトにして実行する。

| 構成 | 用途 |
|---|---|
| Debug | 開発用。アサート・ログ・エディタ入り |
| Preview | 最適化あり + エディタ入り |
| Release | 出荷相当 |

Xbox（UWP）向けは `DebugXbox` / `PreviewXbox` / `ReleaseXbox` で `GameUWP` をビルドする。実機への配置は [Tools/XboxDevMode/README.md](Tools/XboxDevMode/README.md)。

## 構成

```
Engine/   モジュールごとの静的ライブラリ（Core / RHI / Renderer / Editor など）
Game/     ゲーム本体（Win32 / UWP のエントリポイント）
Tools/    アセットビルダなどの補助ツール
Tests/    テスト
Build/    全プロジェクト共通のビルド設定（.props）
```
