# UE 側のエフェクト記録ツールの計画

元の計画: `docs/design/plans/effect-recorder.md`(Unity 側。データ形式は 2 節)

## 結論

- **UE 5.6 のエディターを起動し、起動時に読み込まれる Python から、Niagara を 1/60 秒ずつ進めて毎フレームの全粒子を読む**。読んだ値は Unity 側と同じ `.fxrec`(version 2)で書き出す。エンジン側の再生処理は Unity 由来・UE 由来を区別しない
- 粒子は Niagara の Sim Cache で読む。`CaptureNiagaraSimCacheImmediate` で今の状態を 1 フレーム分取り、`ReadPositionAttribute` などで属性を読む
- 材質(UE のノードグラフ)は変換しない。テクスチャと合成方法(加算・半透明)だけを取り出す。材質の変換は次の段階(ノード → HLSL)で行う

## 1. 試作で分かったこと

`C:\Git\EffectRecorderUE` で、Niagara のテンプレート `SimpleExplosion` の 3 エミッターの粒子の位置を、40 フレーム分読めた。

| やり方 | 結果 |
|---|---|
| コマンドライン(`-run=pythonscript`) | 使えない。アクターを置く処理で落ちる。描画を有効にしても Niagara のコンパイルが進まず、コンポーネントが動かない |
| エディター + `-ExecutePythonScript` | 使えない。スクリプトを実行した直後にエディターが閉じる |
| **エディター + `Content/Python/init_unreal.py`** | **動く**。毎フレームのコールバック(`register_slate_post_tick_callback`)で、コンパイルが終わるのを待ってから記録し、最後に `quit_editor` で閉じる |

- Niagara は `spawn_system_at_location` で出し、`set_force_solo(True)` にして、`advance_simulation(1, 1/60)` で 1 フレームずつ進める。**記録を始めるときに `set_component_tick_enabled(False)` で通常の更新を止める**(止めないと、エディターの毎フレームの更新でも実時間分進み、記録の間隔が 1/60 秒にならない)
- 記録中はエディターの画面が一瞬開く(自動で閉じる)
- 粒子の属性は `read_position_attribute` / `read_vector_attribute` / `read_vector2_attribute`(`SpriteSize`)/ `read_float_attribute` / `read_color_attribute` / `read_int_attribute`(`UniqueID`)で読めた
- 描画設定は、システムからエミッターをたどる関数が Python にないので、`unreal.ObjectIterator(unreal.NiagaraRendererProperties)` で読み込み済みの描画設定を列挙し、パスがこのシステムの下にあるものを拾う。板(`NiagaraSpriteRendererProperties`)は `material` / `sub_image_size` / `alignment` / `facing_mode` / `pivot_in_uv_space` / `sort_mode`、メッシュは `meshes` が読めた
- 描画設定の親(エミッター)の名前は `OmnidirectionalBurst_0` のように末尾に番号が付くことがあり、Sim Cache のエミッター名(`OmnidirectionalBurst`)とは前方一致で対応させる

## 2. 作るもの

新しい private リポジトリ `TakebayashiNaoya/EffectRecorderUE`(`C:\Git\EffectRecorderUE`)。Unity 側とはプロジェクトの作りが違うので分ける。

| ファイル | 内容 |
|---|---|
| `EffectRecorderUE.uproject` | Python と Niagara を有効にした空のプロジェクト(試作のものを使う) |
| `Content/Python/init_unreal.py` | 起動時に読まれる。コマンドラインに `-fxrec=<Niagara のパス>` があるときだけ記録を始める |
| `Content/Python/fxrec/recorder.py` | 記録の本体。毎フレームのコールバックで進めて読み、終わったら書き出して閉じる |
| `Content/Python/fxrec/convert.py` | UE の値を `.fxrec` の値に直す(座標・単位・回転・コマ番号) |
| `Content/Python/fxrec/assets.py` | 材質からテクスチャを探して PNG で書き出す。メッシュ粒子のメッシュを OBJ で書き出す(頂点も座標を直す) |
| `Content/Python/fxrec/fxrec_file.py` | `.fxrec`(JSON + bin)の書き出し。Unity 側の `FxRecFile.cs` と同じ形式 |
| `Tests/test_convert.py` | 座標の変換と書き出しのテスト。UE なしの Python で動く |
| `record.bat` | `record.bat /Niagara/.../SimpleExplosion` で記録する |

Unity 側(`C:\Git\EffectRecorder`)には次を足す。

| ファイル | 内容 |
|---|---|
| `FxRecPlayer.cs` | 元の材質・メッシュがないとき(UE 由来のデータ)、JSON のテクスチャと合成方法から URP の粒子用の材質を作り、JSON の `mesh`(OBJ)を読み込んで描く |
| `FxRecObj.cs`(新規) | OBJ の読み込み(頂点・UV・法線・面だけの素朴なもの) |

## 3. UE の値と `.fxrec` の対応

座標は UE(X が前、Y が右、Z が上、cm)から `.fxrec`(X が右、Y が上、Z が前、m)へ、`(y, z, x) × 0.01` で直す。どちらも左手系なので反転はない。

| `.fxrec` | Niagara の属性 | 変換 |
|---|---|---|
| 位置・速度 | `Position` / `Velocity` | 座標を直す |
| 大きさ | 板: `SpriteSize`(X・Y、`read_vector2_attribute`)、メッシュ: `Scale` | 板は cm → m。メッシュの `Scale` は倍率なのでそのまま(メッシュ本体を OBJ にするときに cm → m にする) |
| 回転 | 板: `SpriteRotation`(度)→ Z、メッシュ: `MeshOrientation`(クォータニオン) | メッシュは座標を直してからオイラー角にする |
| 色 | `Color` | 0〜1 に切り詰めて RGBA8 |
| コマ番号・混ぜ具合 | `SubImageIndex` | 整数部がコマ番号、小数部が混ぜ具合 |
| ID | `UniqueID` | そのまま |

描画の設定は、エミッターの描画設定(Renderer)から読む。

| `.fxrec` の JSON | Niagara の描画設定 |
|---|---|
| `render` | Sprite Renderer → `Billboard`(Velocity Aligned なら `Stretched` に近いが、まずは `Billboard`)、Mesh Renderer → `Mesh`、Ribbon Renderer → 未対応として報告 |
| `mesh` | Mesh Renderer の `meshes` の最初のメッシュを OBJ で書き出し、そのファイル名。頂点は位置と同じく `(y, z, x) × 0.01` で直す |
| `tiles` | Sprite Renderer の `SubImageSize` |
| `texture` / `blend` | 材質のテクスチャのパラメータ / 材質の Blend Mode(Additive か Translucent か) |
| `alignment` | Sprite Renderer の `FacingMode` / `Alignment` |

上の対応のうち、Python から読めない設定があれば、その項目は既定値にして報告する。

## 4. 確認の方法

UE 側で元と並べて描くのは、材質が粒子専用の作りなので難しい。次の 2 つで確かめる。

- 数値: 書き出した bin を読み戻し、Sim Cache の値(座標変換後)と一致する(`Tests/test_convert.py` と、記録時の自己チェック)
- 見た目: Unity の確認用再生に UE 由来のデータを読ませ、UE のエディターで撮った同じフレームのスクリーンショットと並べる。材質はテクスチャだけの仮のものなので、形・動き・大きさ・色の傾向が合っているかを見る

## 4.5 実装中に分かったこと

- `capture_niagara_sim_cache_immediate` の戻り値は、書き込んだ Sim Cache だけ(失敗したら None)
- UE のスクリーンショットは、撮影先のテクスチャを 8bit の RGBA にしないと EXR で書き出される
- メッシュ用の描画設定の `FacingMode` は、型が Python に公開されていないので読めない。描画設定を T3D(テキスト)で書き出して `FacingMode=Velocity` の行を読む(`assets.text_properties`)。`Velocity` のときは、速度の向きにメッシュの前(Z)を向ける回転を記録する
- テンプレートの多くはループするので、`record.bat` の 2 つ目の引数で記録するフレーム数を指定する

## 5. 採らなかった案

| 案 | 却下理由 |
|---|---|
| C++ のプラグインで記録する | ビルドの手間が増える。属性(`read_vector2_attribute` を含む)も描画設定(`ObjectIterator`)も Python で読めた |
| `CaptureNiagaraSimCacheMultiFrame` でまとめて取る | 非同期で、終わりを待つ仕組みが別に要る。1 フレームずつ取れば足りる |
| UE 側に左右比較の画面を作る | 材質が粒子専用で、記録の再生に使えない |

## 6. やらないこと

- 材質の変換(次の段階)
- Ribbon Renderer、GPU で動くエミッター(見つけたら報告だけする)
- Content Examples の入手(ユーザーが Epic Games Launcher で行う)

## 7. 完了チェック

- [x] `record.bat` で `SimpleExplosion` を記録でき、JSON・bin・メッシュ(OBJ)が出る(このテンプレートの材質はテクスチャを使わないので、テクスチャは出ない)
- [x] `Tests/test_convert.py` が通る
- [x] Unity の確認用再生で UE 由来のデータ(板とメッシュ)が描け、UE のスクリーンショットと形・動きが合う
- [ ] Content Examples の Niagara を 1 つ以上記録できる(入手後)
