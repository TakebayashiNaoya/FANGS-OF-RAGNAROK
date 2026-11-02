# UE側のエフェクト記録ツールの計画

元の計画: `docs/design/plans/effect-recorder.md`(Unity側。データ形式は2節)

## 結論

- **UE 5.6のエディターを起動し、起動時に読み込まれるPythonから、Niagaraを1/60秒ずつ進めて毎フレームの全粒子を読む**。読んだ値はUnity側と同じ`.fxrec`(version 2)で書き出す。エンジン側の再生処理はUnity由来・UE由来を区別しない
- 粒子はNiagaraのSim Cacheで読む。`CaptureNiagaraSimCacheImmediate`で今の状態を1フレーム分取り、`ReadPositionAttribute`などで属性を読む
- 材質(UEのノードグラフ)は変換しない。テクスチャと合成方法(加算・半透明)だけを取り出す。材質の変換は次の段階(ノード → HLSL)で行う

## 1. 試作で分かったこと

`C:\Git\EffectRecorderUE`で、Niagaraのテンプレート`SimpleExplosion`の3エミッターの粒子の位置を、40フレーム分読めた。

| やり方 | 結果 |
|---|---|
| コマンドライン(`-run=pythonscript`) | 使えない。アクターを置く処理で落ちる。描画を有効にしてもNiagaraのコンパイルが進まず、コンポーネントが動かない |
| エディター + `-ExecutePythonScript` | 使えない。スクリプトを実行した直後にエディターが閉じる |
| **エディター + `Content/Python/init_unreal.py`** | **動く**。毎フレームのコールバック(`register_slate_post_tick_callback`)で、コンパイルが終わるのを待ってから記録し、最後に`quit_editor`で閉じる |

- Niagaraは`spawn_system_at_location`で出し、`set_force_solo(True)`にして、`advance_simulation(1, 1/60)`で1フレームずつ進める。**記録を始めるときに`set_component_tick_enabled(False)`で通常の更新を止める**(止めないと、エディターの毎フレームの更新でも実時間分進み、記録の間隔が1/60秒にならない)
- 記録中はエディターの画面が一瞬開く(自動で閉じる)
- 粒子の属性は`read_position_attribute` / `read_vector_attribute` / `read_vector2_attribute`(`SpriteSize`)/ `read_float_attribute` / `read_color_attribute` / `read_int_attribute`(`UniqueID`)で読めた
- 描画設定は、システムからエミッターをたどる関数がPythonにないので、`unreal.ObjectIterator(unreal.NiagaraRendererProperties)`で読み込み済みの描画設定を列挙し、パスがこのシステムの下にあるものを拾う。板(`NiagaraSpriteRendererProperties`)は`material` / `sub_image_size` / `alignment` / `facing_mode` / `pivot_in_uv_space` / `sort_mode`、メッシュは`meshes`が読めた
- 描画設定の親(エミッター)の名前は`OmnidirectionalBurst_0`のように末尾に番号が付くことがあり、Sim Cacheのエミッター名(`OmnidirectionalBurst`)とは前方一致で対応させる

## 2. 作るもの

新しいprivateリポジトリ`TakebayashiNaoya/EffectRecorderUE`(`C:\Git\EffectRecorderUE`)。Unity側とはプロジェクトの作りが違うので分ける。

| ファイル | 内容 |
|---|---|
| `EffectRecorderUE.uproject` | PythonとNiagaraを有効にした空のプロジェクト(試作のものを使う) |
| `Content/Python/init_unreal.py` | 起動時に読まれる。コマンドラインに`-fxrec=<Niagara のパス>`があるときだけ記録を始める |
| `Content/Python/fxrec/recorder.py` | 記録の本体。毎フレームのコールバックで進めて読み、終わったら書き出して閉じる |
| `Content/Python/fxrec/convert.py` | UEの値を`.fxrec`の値に直す(座標・単位・回転・コマ番号) |
| `Content/Python/fxrec/assets.py` | 材質からテクスチャを探してPNGで書き出す。メッシュ粒子のメッシュをOBJで書き出す(頂点も座標を直す) |
| `Content/Python/fxrec/fxrec_file.py` | `.fxrec`(JSON + bin)の書き出し。Unity側の`FxRecFile.cs`と同じ形式 |
| `Tests/test_convert.py` | 座標の変換と書き出しのテスト。UEなしのPythonで動く |
| `record.bat` | `record.bat /Niagara/.../SimpleExplosion`で記録する |

Unity側(`C:\Git\EffectRecorder`)には次を足す。

| ファイル | 内容 |
|---|---|
| `FxRecPlayer.cs` | 元の材質・メッシュがないとき(UE由来のデータ)、JSONのテクスチャと合成方法からURPの粒子用の材質を作り、JSONの`mesh`(OBJ)を読み込んで描く |
| `FxRecObj.cs`(新規) | OBJの読み込み(頂点・UV・法線・面だけの素朴なもの) |

## 3. UEの値と`.fxrec`の対応

座標はUE(Xが前、Yが右、Zが上、cm)から`.fxrec`(Xが右、Yが上、Zが前、m)へ、`(y, z, x) × 0.01`で直す。どちらも左手系なので反転はない。

| `.fxrec` | Niagaraの属性 | 変換 |
|---|---|---|
| 位置・速度 | `Position` / `Velocity` | 座標を直す |
| 大きさ | 板: `SpriteSize`(X・Y、`read_vector2_attribute`)、メッシュ: `Scale` | 板はcm → m。メッシュの`Scale`は倍率なのでそのまま(メッシュ本体をOBJにするときにcm → mにする) |
| 回転 | 板: `SpriteRotation`(度)→ Z、メッシュ: `MeshOrientation`(クォータニオン) | メッシュは座標を直してからオイラー角にする |
| 色 | `Color` | 0〜1に切り詰めてRGBA8 |
| コマ番号・混ぜ具合 | `SubImageIndex` | 整数部がコマ番号、小数部が混ぜ具合 |
| ID | `UniqueID` | そのまま |

描画の設定は、エミッターの描画設定(Renderer)から読む。

| `.fxrec`のJSON | Niagaraの描画設定 |
|---|---|
| `render` | Sprite Renderer → `Billboard`(Velocity Alignedなら`Stretched`に近いが、まずは`Billboard`)、Mesh Renderer → `Mesh`、Ribbon Renderer → 未対応として報告 |
| `mesh` | Mesh Rendererの`meshes`の最初のメッシュをOBJで書き出し、そのファイル名。頂点は位置と同じく`(y, z, x) × 0.01`で直す |
| `tiles` | Sprite Rendererの`SubImageSize` |
| `texture` / `blend` | 材質のテクスチャのパラメータ / 材質のBlend Mode(AdditiveかTranslucentか) |
| `alignment` | Sprite Rendererの`FacingMode` / `Alignment` |

上の対応のうち、Pythonから読めない設定があれば、その項目は既定値にして報告する。

## 4. 確認の方法

UE側で元と並べて描くのは、材質が粒子専用の作りなので難しい。次の2つで確かめる。

- 数値: 書き出したbinを読み戻し、Sim Cacheの値(座標変換後)と一致する(`Tests/test_convert.py`と、記録時の自己チェック)
- 見た目: Unityの確認用再生にUE由来のデータを読ませ、UEのエディターで撮った同じフレームのスクリーンショットと並べる。材質はテクスチャだけの仮のものなので、形・動き・大きさ・色の傾向が合っているかを見る

## 4.5実装中に分かったこと

- `capture_niagara_sim_cache_immediate`の戻り値は、書き込んだSim Cacheだけ(失敗したらNone)
- UEのスクリーンショットは、撮影先のテクスチャを8bitのRGBAにしないとEXRで書き出される
- メッシュ用の描画設定の`FacingMode`は、型がPythonに公開されていないので読めない。描画設定をT3D(テキスト)で書き出して`FacingMode=Velocity`の行を読む(`assets.text_properties`)。`Velocity`のときは、速度の向きにメッシュの前(Z)を向ける回転を記録する
- テンプレートの多くはループするので、`record.bat`の2つ目の引数で記録するフレーム数を指定する

## 5. 採らなかった案

| 案 | 却下理由 |
|---|---|
| C++のプラグインで記録する | ビルドの手間が増える。属性(`read_vector2_attribute`を含む)も描画設定(`ObjectIterator`)もPythonで読めた |
| `CaptureNiagaraSimCacheMultiFrame`でまとめて取る | 非同期で、終わりを待つ仕組みが別に要る。1フレームずつ取れば足りる |
| UE側に左右比較の画面を作る | 材質が粒子専用で、記録の再生に使えない |

## 6. やらないこと

- 材質の変換(次の段階)
- Ribbon Renderer、GPUで動くエミッター(見つけたら報告だけする)
- Content Examplesの入手(ユーザーがEpic Games Launcherで行う)

## 7. 完了チェック

- [x] `record.bat`で`SimpleExplosion`を記録でき、JSON・bin・メッシュ(OBJ)が出る(このテンプレートの材質はテクスチャを使わないので、テクスチャは出ない)
- [x] `Tests/test_convert.py`が通る
- [x] Unityの確認用再生でUE由来のデータ(板とメッシュ)が描け、UEのスクリーンショットと形・動きが合う
- [ ] Content ExamplesのNiagaraを1つ以上記録できる(入手後)
