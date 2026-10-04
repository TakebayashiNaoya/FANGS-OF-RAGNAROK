# UE の材質を HLSL に変換する計画

要件: `docs/design/plans/effect-material-ue-requirements.md`
関連: `docs/design/plans/effect-recorder.md`(データ形式)、`docs/design/plans/effect-recorder-ue.md`(UE 側の記録ツール)

## 結論

- **UE の材質を T3D(テキスト)で書き出し、出力(発光色・不透明度)からノードをたどって HLSL の関数 1 つに変換する**。材質インスタンスのパラメータ(数値・色・テクスチャ)は定数として埋め込む
- エンジン側(D3D12)が呼ぶのは `float4 FxMaterial(FxMaterialInput i)` だけ。入力の形(`FxMaterialInput`)は全材質で共通にする。**この形はエンジン側の設計なので、たたき台としてユーザーが決める**(5 節)
- 材質が使う「粒子ごとの値」(Dynamic Material Parameter)と、1 を超える明るさの色を記録できるよう、`.fxrec` を version 3 にする
- 変換した HLSL が正しいかは、Unity で同じ HLSL を包んだシェーダーを作り、記録を描いて UE のスクリーンショットと見比べる

## 1. 調べて分かったこと(Niagara Slash の `NE_attack01`)

- 斬撃の本体はメッシュ粒子 3 つで、粒子はほぼ動かない。見た目は材質の中で、UV のスクロール(Panner)と粒子ごとの値(Dynamic Parameter)で作られている
- 材質は T3D で書き出せる(UTF-16)。出力は `EmissiveColor=(Expression="...MaterialExpressionMultiply_1")` のようにノードを指し、各ノードは入力のノードと設定値を持つ
- 材質インスタンスの T3D(UTF-8)には、親の材質と、上書きしたパラメータ(`ScalarParameterValues` など)と合成方法(`BasePropertyOverrides`)が書かれている
- 4 つの材質で使われているノード:

| 種類 | ノード |
|---|---|
| 計算 | Multiply、Add、Power、OneMinus、LinearInterpolate、ComponentMask、AppendVector、Reroute |
| 定数・パラメータ | ScalarParameter、VectorParameter |
| テクスチャ | TextureSample、TextureCoordinate、Panner |
| 粒子・時間 | ParticleColor、DynamicParameter、Time |
| 変換が難しいもの | DepthFade(深度が要る)、MaterialFunctionCall(関数の中身も変換が要る)、CurveAtlasRowParameter(曲線の表が要る) |

- 粒子の色は 1 を超える値(例: 5 倍の明るさ)を使っている。今の形式(RGBA 各 8bit)では失われる

## 2. データ形式の変更(version 3)

JSON のエミッターに次を足す。

| 項目 | 内容 |
|---|---|
| `material` | 変換した HLSL のファイル名(例: `MI_height_lighting01.hlsl`)。変換していなければ空文字。空でなければ再生側はこれで描き、`texture` / `tint` は使わない |
| `textures` | HLSL の `FxTexture0` 〜 に順に割り当てるテクスチャのファイル名 |
| `textureLinear` | `textures` と同じ並びで、リニア(sRGB でない)で読むなら true |
| `dynamicParams` | 粒子ごとの値の組の数(0〜4)。1 組は float × 4 |
| `colorScale` | 色の倍率。本当の色は「bin の RGB ÷ 255 × colorScale」、A は「bin の A ÷ 255」。1 を超える明るさを残すため。記録側は、そのエミッターの全粒子・全フレームの RGB の最大値(1 未満なら 1)を colorScale にし、RGB を colorScale で割ってから 0〜255 にする。A を同じ倍率で割らないのは、Niagara の色は RGB が 1 万近くになることがあり、A が 0 に潰れるため |
| `alignment` | 既存の値に `VelocityAligned`(板はカメラを向き、板の上を速度の向きに合わせる)を足す。Niagara の Velocity Aligned 用 |

bin の粒子は、60 バイトの後ろに `dynamicParams × 16` バイト(float × 4 を組の数だけ)を付ける。エミッターごとに長さが変わるので、再生側は JSON の `dynamicParams` を見て読む。`dynamicParams` が 0 なら今と同じ 60 バイト。

Unity 側の記録ツールは `dynamicParams = 0`、`colorScale = 1`、`material = ""` を書く(今と同じ見た目)。

## 3. 作るもの

UE 側(`C:\Git\EffectRecorderUE`)

| ファイル | 内容 |
|---|---|
| `Content/Python/fxrec/material_t3d.py` | T3D を読んで、ノードの一覧(種類・設定値・入力のつながり)にする。UE なしで動く |
| `Content/Python/fxrec/material_hlsl.py` | ノードの一覧を HLSL にする。UE なしで動く |
| `Content/Python/fxrec/recorder.py` | 材質を T3D に書き出して変換し、`material` / `textures` を JSON に書く。`DynamicMaterialParameter`〜`3` を `read_vector4_attribute` で読む。色の倍率を決める |
| `Content/Python/fxrec/fxrec_file.py` | version 3 の書き出し |
| `Tests/test_material.py` | 小さな T3D の文字列から HLSL を作り、期待した式になるか |
| `Tests/data/*.t3d` | テスト用の T3D(自作の小さなもの。ストアの素材は入れない) |

Unity 側(`C:\Git\EffectRecorder`)

| ファイル | 内容 |
|---|---|
| `FxRecFile.cs` | version 3 の読み書き(`dynamicParams`・`colorScale`・`material`・`textures`) |
| `Editor/FxRecShader.cs`(新規) | 変換した HLSL を包んだ Unity のシェーダーを `Assets/EffectRecorder/Generated/` に書き出す |
| `FxRecPlayer.cs` | `material` があれば、そのシェーダーで描く。板は粒子ごとの値を頂点の UV2〜UV5 に入れる。メッシュ粒子は 1 粒ずつ描くので、粒子ごとの値と色は描画ごとのプロパティ(MaterialPropertyBlock)で渡す。`VelocityAligned` の板の向き |

## 4. 変換の仕方

- 出力は `EmissiveColor`(発光色)と `Opacity`(不透明度)だけを使う。Unlit(ライティングなし)の材質が対象
- 出力からノードをたどり、1 ノードを 1 つの一時変数にする(例: `float3 n5 = n3 * n4;`)。同じノードは 1 回だけ計算する
- 型(float〜float4)は、各ノードの出力の要素数から決める。要素数が違う掛け算・足し算は、HLSL と同じく 1 要素のものを広げる
- パラメータは、材質インスタンスで上書きした値、なければ親の材質の既定値を定数にする
- 対応していないノードは、0 か 1 の定数に置き換えてコメントに残し、記録のログに「未対応のノード」として出す。`DepthFade` は 1(ぼかさない)にする

## 5. エンジン側との約束(たたき台。ユーザーが決める)

変換した HLSL は次の形になる。エンジン側の再生処理は、頂点シェーダーで `FxMaterialInput` を作ってピクセルシェーダーで `FxMaterial` を呼ぶ。

```hlsl
// 全材質で共通。エンジン側が用意する
struct FxMaterialInput
{
    float2 uv;              // テクスチャ座標(連番のコマを反映したもの)。D3D と同じく左上が原点、V は下向き
    float4 color;           // 粒子の色(colorScale を掛けた、1 を超えうる値)
    float4 dynamicParam[4]; // 粒子ごとの値。使わない組は 0
    float  time;            // エフェクトが始まってからの秒数
};

// 材質ごとに生成される(レジスタの割り当てはエンジン側で決める)
Texture2D    FxTexture0;       // JSON の textures[0]。textureLinear[0] が true ならリニア(UNORM)、false なら sRGB で読む
SamplerState FxSampler;        // 線形補間・繰り返す
SamplerState FxSamplerClamp;   // 線形補間・端で止める(UE で Clamp のテクスチャに使う)
float4 FxMaterial(FxMaterialInput i)      // rgb = 発光色、a = 不透明度
{
    ...
}
```

- 合成方法は JSON の `blend` で、エンジン側のパイプラインの設定で行う。`Additive` は「発光色をそのまま足す」(不透明度は使わない)、`Alpha` は「発光色 × 不透明度 + 背景 × (1 − 不透明度)」
- OBJ のメッシュの UV は OBJ の慣習どおり左下が原点。エンジン側は読み込むときに V を反転する(1 − V)
- `time` は UE の Time ノード(ワールドの時間)の代わり。エフェクトごとに 0 から数えるので、UV のスクロールの始まりの位置は UE と違ってよい

## 5.5 確認の方法

- 数値: `Tests/test_material.py`(T3D → HLSL の変換)と、既存のテスト
- 見た目: Unity で `FxRecShader` が作ったシェーダーで記録を描き、`FxRecCapture.RunViewBatch` で UE のスクリーンショットと並べる。Niagara Slash の `NE_attack01`〜`05` で、斬撃の形・明るさ・消え方が近いこと

## 5.6 実装中に分かったこと

| 分かったこと | 対応 |
|---|---|
| Niagara の色は RGB が 1 万近くになることがある。A も同じ倍率で割ると 0 に潰れる | `colorScale` は RGB だけに掛ける(2 節) |
| UE の既定の粒子の材質は SphereMask と TwoSidedSign を使う | 2 つとも変換に対応した |
| UE の材質の UV は左上が原点 | `FxMaterialInput.uv` は左上原点と決めた。Unity で確かめるときは、包むシェーダーの側で上下を反転する |
| テクスチャごとに「端で止める(Clamp)」設定がある。UV をずらす材質では、繰り返すと見た目が大きく変わる | サンプラーを 2 つ(`FxSampler` / `FxSamplerClamp`)にした |
| 法線マップとして読むテクスチャ(`SamplerType=SAMPLERTYPE_Normal`)は、UE が 0〜1 を −1〜1 に戻して読む | 変換した HLSL に `FxUnpackNormal` を入れる |
| sRGB でないテクスチャ(法線マップ・マスク)をリニアで読まないと値がずれる | JSON に `textureLinear` を足した |
| 背景(夜空・床)と自動露出で、UE と Unity の見え方が大きく違う | 比較のときは、UE 側のレベルを空にして自動露出とブルームを切り、Unity 側も背景を黒にする |

## 6. 採らなかった案

| 案 | 却下理由 |
|---|---|
| UE が内部で作る HLSL を取り出す | UE の共通コードに強く依存していて、切り離せない |
| `MaterialEditingLibrary` でノードをたどる | ノードの設定値を読む関数がそろっていない。T3D なら全部書かれている |
| パラメータを定数バッファで渡す | エンジン側の仕事が増える。今は材質インスタンスごとに HLSL を作れば足りる |
| 色を float × 4 で持つ | 粒子 1 つが 12 バイト増える。エミッターごとの倍率で足りる |

## 7. やらないこと

- ライティングのある材質、法線マップ、屈折・歪み、頂点を動かす処理(World Position Offset)
- 材質関数(MaterialFunctionCall)と Curve Atlas の変換。見つけたら報告する
- エンジン側(D3D12)の再生処理

## 8. 完了チェック

- [x] `Tests/test_material.py` と既存のテストが通る
- [x] `NE_attack01` を記録すると、`material` の HLSL・`textures`・粒子ごとの値が出る
- [x] Unity で変換した HLSL で描いた斬撃が、UE のスクリーンショットと形・消え方で近い(色味は UE のトーンマップの分だけ違う。閃光の `M_flare` は材質関数と Curve Atlas を使うので出ない)
- [x] `docs/design/plans/effect-recorder.md` のデータ形式を version 3 に更新する
