# エフェクト記録ツールの計画

要件: `docs/design/plans/effect-recorder-requirements.md`

## 結論

- **Unityでエフェクトを1/60秒ずつ進め、毎フレームの全粒子の状態を記録する**。エンジンは記録を再生するだけなので、調整の手作業が要らない
- データは**JSON(エミッターの情報)+ bin(粒子)** の2ファイル。粒子1つは60バイト固定
- 記録が正しいかは、Unity上で記録を再生し、元のエフェクトと並べて確かめる

## 1. 作るもの

Unityプロジェクト`C:\Git\EffectRecorder`(新規、6000.3.9f1。GitHubのprivateリポジトリ`TakebayashiNaoya/EffectRecorder`)に作る。Assetsのうちgitに入れるのは自作の`EffectRecorder`フォルダだけで、ストアの素材は入れない。

| ファイル | 内容 |
|---|---|
| `Assets/EffectRecorder/Editor/FxRecorderWindow.cs` | メニュー「Tools/Effect Recorder」。Prefabを選んで「記録」を押すと、出力フォルダにJSON・bin・テクスチャ・メッシュを書き出す |
| `Assets/EffectRecorder/Editor/FxRecorder.cs` | 記録の本体。Prefabを一時的に置き、`ParticleSystem.Simulate`で1/60秒ずつ進めて`GetParticles`で読む |
| `Assets/EffectRecorder/Runtime/FxRecFile.cs` | データ形式の読み書き(記録と確認用の再生の両方が使う) |
| `Assets/EffectRecorder/Runtime/FxRecSimulation.cs` | エフェクトを止めて乱数の種を固定する処理と、1/60秒進める処理。記録と比較で同じ手順を使うために分けた |
| `Assets/EffectRecorder/Runtime/FxRecPlayer.cs` | 確認用の再生。記録を読み、元のマテリアルで板ポリゴンを描く |
| `Assets/EffectRecorder/Runtime/FxRecCompare.cs` | 左に元のエフェクト、右に記録の再生を置き、同時に1/60秒ずつ進める |
| `Assets/EffectRecorder/Editor/FxRecCapture.cs` | バッチモードで記録し、元と記録を左右に並べたスクリーンショットを書き出す(Unityを開かずに見比べるため) |
| `Assets/EffectRecorder/Scenes/Compare.unity` | ウィンドウの「比較シーンを作る」で作る。FxRecCompareとカメラだけを置く |
| `Assets/EffectRecorder/Tests/Editor/FxRecFileTests.cs` | 書いて読み戻すと同じ値になるかのテスト |
| `Assets/EffectRecorder/Tests/Editor/FxRecorderTests.cs` | テスト内で作った簡単なエフェクトを記録し、粒子が記録されるかのテスト |

FangEngineDesign側は、データ形式をこの計画の2節に残す。試作の結果は`docs/design/pilots/effects-pilot-result.md`に書く。

## 2. データ形式(後から変えにくいので、ここだけ決める)

出力は`<名前>.fxrec.json`と`<名前>.fxrec.bin`、そこから参照するテクスチャとメッシュ。

座標はUnityのまま(左手系、Yが上、メートル)。エフェクトの根元のTransformからの相対位置で持つ。エンジンの座標系が違えば、エンジン側で読み込み時に変換する。

### JSON

```json
{
  "version": 3,
  "fps": 60,
  "frameCount": 90,
  "emitters": [
    {
      "name": "Sparks",
      "render": "Stretched",
      "lengthScale": 2.0,
      "velocityScale": 0.1,
      "mesh": "",
      "texture": "Spark01.png",
      "tint": [1.0, 0.8, 0.5, 1.0],
      "blend": "Additive",
      "tiles": [4, 4],
      "alignment": "View",
      "pivot": [0.0, 0.0, 0.0],
      "maxScreenSize": 0.5,
      "sortByDistance": false,
      "trail": {
        "enabled": true,
        "texture": "Trail01.png",
        "tint": [1.0, 1.0, 1.0, 1.0],
        "blend": "Additive",
        "ratio": 1.0,
        "seconds": 0.3,
        "minVertexDistance": 0.1,
        "sizeAffectsWidth": true,
        "sizeAffectsLifetime": false,
        "inheritParticleColor": true,
        "textureMode": "Stretch",
        "width": [1, 1, 1, 1, 1, 1, 1, 0],
        "color": [1, 1, 1, 1,  1, 1, 1, 0.8,  1, 1, 1, 0.6,  1, 1, 1, 0.5,  1, 1, 1, 0.4,  1, 1, 1, 0.3,  1, 1, 1, 0.1,  1, 1, 1, 0]
      }
    }
  ]
}
```

- `render`:`Billboard`(板)/ `Stretched`(速度の向きに伸ばす)/ `Horizontal`(地面と平行)/ `Vertical`(縦に立つ)/ `Mesh` / `None`(描かない。サブエミッターの発生源としてだけ使うもの)
- `lengthScale` / `velocityScale`:`Stretched`のときだけ使う。板の長さは「大きさ × lengthScale + 速さ × velocityScale」。**粒子の位置を先端にして、速度の逆向きに尾を引く**
- `mesh`:`Mesh`のときだけ。メッシュはOBJで書き出す。使わないときは空文字(UnityのJsonUtilityはnullを書けないため)
- `texture`:元のテクスチャのファイルをそのままコピーする
- `tint`:マテリアルの色。粒子の色に掛ける
- `blend`:`Additive` / `Alpha`
- `tiles`:連番テクスチャの縦横のコマ数。連番でなければ`[1, 1]`
- `alignment`:`Billboard`の板の向き。`View`(カメラを向き、回転はZだけを画面上で使う。**時計回りが正**)/ `World`(粒子の3D回転に従う。板は回転後のXY平面)/ `Velocity`(進む向きを正面にして、その上で3D回転する)。UnityのLocalは、記録時にエフェクトの向きを回転に含めて`World`にする
- `pivot`:基準点のずれ。`Billboard`などの板は「大きさ × pivot」だけ板の右・上方向にずらす。`Mesh`は「大きさ × pivot × メッシュの大きさ(bounds)」を粒子の回転で回してずらす
- `trail`:粒子の軌跡(UnityのTrails、Particlesモード)。`enabled`がfalseなら軌跡はない。再生側は、同じ`id`の粒子の過去の位置を`seconds`(`sizeAffectsLifetime`なら粒子の大きさを掛ける)の分だけさかのぼってつなぎ、カメラを向いた帯を作る。`minVertexDistance`より近い過去の位置は飛ばす。`width`と`color`は先端から末尾まで等間隔に8点取った値で、間は線形補間する。`ratio`は軌跡を持つ粒子の割合で、`id % 1000 < ratio × 1000`の粒子だけが持つ(Unityと同じ粒子にはならない)
- version 3で足した項目(UEの材質の変換用。Unityで記録したデータは既定値のまま):`material`(変換したHLSL)、`textures` / `textureLinear`(HLSLの`FxTexture0`〜 とその色空間)、`dynamicParams`(粒子ごとの値の組の数)、`colorScale`(色の倍率)。意味とHLSLの約束は`docs/design/plans/effect-material-ue.md`の2節・5節。`alignment`には`VelocityAligned`(板はカメラを向き、上を速度の向きに合わせる)も入る
- `maxScreenSize`:粒子が画面に占める大きさの上限。画面の幅と高さの大きい方に対する割合。カメラの目の前に来た粒子が画面を覆わないようにする
- `sortByDistance`:`true`なら再生側で粒子をカメラから遠い順に描く。`false`なら記録の並び順のまま描く(UnityのSort ModeがDistanceかどうか。並べ替えない設定の粒子を並べ替えると、重なった粒子の前後がフレームごとに入れ替わって点滅する)
- エミッターの並び順が描画順(前のものから描く)。記録時にUnityのOrder in Layerの小さい順に並べる。再生側はこの順を固定で守る(距離で並べ替えると、フレームごとに前後が入れ替わって点滅する)

### bin

フレーム順に、各フレームの中ではエミッター順に、次を並べる。

| 内容 | 型 |
|---|---|
| 粒子の数 | uint32 |
| 粒子 × 数 | 下の表 |

粒子1つ(60バイト、リトルエンディアン):

| 項目 | 型 | バイト |
|---|---|---|
| 位置 | float × 3 | 12 |
| 速度(`Stretched`で使う) | float × 3 | 12 |
| 回転(度、X・Y・Z) | float × 3 | 12 |
| 大きさ(X・Y・Z。TransformのスケールのうちScaling Modeで粒子に掛かる分を含む) | float × 3 | 12 |
| 色(本当の色 = RGB ÷ 255 × `colorScale`、A ÷ 255) | RGBA各uint8 | 4 |
| 連番のコマ番号 | uint16 | 2 |
| 次のコマとの混ぜ具合(0〜65535 = 0〜1) | uint16 | 2 |
| 粒子のID(フレームをまたいで同じ粒子を見分ける。軌跡やフレーム間の補間に使う) | uint32 | 4 |
| 粒子ごとの値(エミッターの`dynamicParams`の数だけ。0なら無い) | float × 4 × 組の数 | 16 × 組の数 |

- 再生側は読み込み時に先頭から粒子の数を読み、各フレームの位置の表を作る
- 混ぜ具合はFlipbook Blending(連番の今のコマと次のコマを混ぜて滑らかにする)用。使わない再生側は無視してよい。UnityのURPのパーティクルシェーダーは、この機能がオンだと頂点に「UV 2組(今のコマ・次のコマ)+ 混ぜ具合」を要求する
- 粒子の並びはUnityの内部順のまま。並べ替えるかどうかはJSONの`sortByDistance`に従う

## 3. 記録の手順

1. Prefabを一時的に原点に置く(向きはPrefabのまま)。乱数の種を固定する(`useAutoRandomSeed = false`)。ループは切り、1周分だけ記録する(Particle Packの爆発がループ設定だったため)
2. `Simulate(1f / 60f, true, false, false)`(子も含む、最初から再生し直さない、Fixed Timeを使わず指定した時間だけ進める)を1回呼ぶごとに、子を含む全`ParticleSystem`で`GetParticles`を読む
3. 位置は各`ParticleSystem`の座標空間(Local / World)から根元の相対位置に直す
4. 発生が終わる時間(`startDelay` + `duration`の最大)を過ぎ、全エミッターの粒子が0になったら止める(最大10秒)。`IsAlive`は`Simulate`で進めた場合にfalseにならなかったので使わない

**コマ番号だけはUnityのAPIで直接取れない**。Texture Sheet Animationの設定(`frameOverTime`・`startFrame`・`cycleCount`)と粒子の経過時間から計算する。設定が「ランダム」のものはずれる。

## 4. 確認用の再生(Unity)

- 毎フレーム、記録の粒子から板ポリゴン(1粒子4頂点)をCPUで作り、元のマテリアルで描く
- 元のマテリアルを使うので、見比べたときの差は「動き・大きさ・色・コマ」の記録の差だけになる
- `Compare.unity`で元と記録を並べ、同じタイミングで再生する
- カメラは左右それぞれの正面に置き、画面を半分ずつ使う。1台で両方を映すと見る角度が違い、奥行きのある粒子の散り方が鏡写しのように見える

## 5. 採らなかった案

| 案 | 却下理由 |
|---|---|
| Prefabの設定をEffekseer形式に変換 | 曲線や発生形状の対応付けが不完全で、見た目の手直しが要る |
| `ParticleSystemRenderer.BakeMesh`で描画結果のメッシュを記録 | 板がその時のカメラの向きに固定され、回り込むと平たい |
| 全部JSON | binの数倍の大きさになり、読み込みも遅い |
| 半精度(float16)で詰める | 説明が増える。大きさが問題になってから考える |

## 6. テスト

- `FxRecFileTests`:粒子2フレーム分を書いて読み戻し、全項目が一致する(EditModeテスト)
- `FxRecorderTests`:0.5秒で消える10粒のエフェクトを記録し、粒子の数と止まるフレームを確かめる(EditModeテスト)
- 見比べ:`Compare.unity`でParticle Packのエフェクト1つを並べ、スクリーンショットを`docs/design/pilots/`に保存する

## 7. 完了チェック

- [x] Particle Packをプロジェクトに入れる(ユーザーがAsset Storeで入手)
- [x] `FxRecFileTests`と`FxRecorderTests`が通る
- [x] エフェクト1つを記録でき、JSON・bin・テクスチャが出力される
- [x] 元と記録が並んで再生され、見分けにくい(バッチモードの撮影と、Unity上の`Compare.unity`の両方で確認)
- [x] `docs/design/pilots/effects-pilot-result.md`に結果(スクリーンショット、データの大きさ、ずれた点)を書く
