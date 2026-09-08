# FANGS OF RAGNAROK

北欧神話を世界観とした 3D アクションゲーム。DirectX 12 + C++20 で自作するエンジン(FangEngine)の上に作る。

- 対応プラットフォーム: Windows (x64) / Xbox One
- 開発環境: Visual Studio 2026

## 現在の状態

着手したところ。ビルド設定と規約だけを置いた段階で、まだコードは無い。

## 構成(予定)

```
Engine/   モジュールごとの静的ライブラリ(Core / RHI / Renderer / Editor など)
Game/     ゲーム本体(Win32 / UWP のエントリポイント)
Tools/    アセットビルダなどの補助ツール
Tests/    テスト
Build/    全プロジェクト共通のビルド設定(.props)
```

## ビルド構成

| 構成 | 用途 |
|---|---|
| Debug | 開発用。アサート・ログ・エディタ入り |
| Preview | 最適化あり + エディタ入り |
| Release | リリース相当 |

Xbox(UWP)向けは `DebugXbox` / `PreviewXbox` / `ReleaseXbox` を使う。
