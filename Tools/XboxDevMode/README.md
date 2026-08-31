# Xbox One（開発者モード）への配置

ビルドで出てくる実際のパスと、実機に置くときの手順を置く。

## パッケージの場所

`GameUWP` を Xbox 構成でビルドすると、レイアウトと .msix がここに出る。

```
Bin/x64/<構成>/GameUWP/GameUWP.exe
Bin/x64/<構成>/GameUWP/AppPackages/GameUWP_<バージョン>_x64_<構成>_Test/
    GameUWP_<バージョン>_x64_<構成>.msix
    Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx
```

`<構成>` は `DebugXbox` / `PreviewXbox` / `ReleaseXbox`。実機に置くのは `PreviewXbox` か `ReleaseXbox`。

## 置くときに毎回見るところ

1. Device Portal（`https://<Xbox の IP>:11443`）の **My games & apps → Add** に .msix と `Microsoft.VCLibs.x64.14.00.appx` を渡す。
2. Dev Home で **App type を「Game」にする**。App 分類だと D3D12 が WARP になり、メモリも 1GB に落ちる。
3. 起動したら `LocalState/` のログで D3D12 デバイスの生成と Feature Level を確かめる。`12_0` 以上でなければ分類が「Game」になっていない。

## 今できていないこと

- PLM（Suspend / Resume）が未対応。ホームに戻って復帰すると描画が壊れる可能性がある（`IDXGIDevice3::Trim` 未実装）。
- 実機ログがファイルに落ちない（デバッガ出力のみ）。起動しない場合の調査は VS のリモートデバッグで行う。
- 入力が未実装なので、エディタ UI は表示されるだけで操作できない。
