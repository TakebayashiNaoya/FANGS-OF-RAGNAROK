# UEの材質をHLSLに変換する計画

要件: `docs/design/plans/effect-material-ue-requirements.md`
関連: `docs/design/plans/effect-recorder.md`(データ形式)、`docs/design/plans/effect-recorder-ue.md`(UE側の記録ツール)

## 結論

- **UEの材質をT3D(テキスト)で書き出し、出力(発光色・不透明度)からノードをたどってHLSLの関数1つに変換する**。材質インスタンスのパラメータ(数値・色・テクスチャ)は定数として埋め込む
- エンジン側(D3D12)が呼ぶのは`float4 FxMaterial(FxMaterialInput i)`だけ。入力の形(`FxMaterialInput`)は全材質で共通にする。**この形はエンジン側の設計なので、たたき台としてユーザーが決める**(5節)
- 材質が使う「粒子ごとの値」(Dynamic Material Parameter)と、1を超える明るさの色を記録できるよう、`.fxrec`をversion 3にする
- 変換したHLSLが正しいかは、Unityで同じHLSLを包んだシェーダーを作り、記録を描いてUEのスクリーンショットと見比べる

## 1. 調べて分かったこと(Niagara Slashの`NE_attack01`)

- 斬撃の本体はメッシュ粒子3つで、粒子はほぼ動かない。見た目は材質の中で、UVのスクロール(Panner)と粒子ごとの値(Dynamic Parameter)で作られている
- 材質はT3Dで書き出せる(UTF-16)。出力は`EmissiveColor=(Expression="...MaterialExpressionMultiply_1")`のようにノードを指し、各ノードは入力のノードと設定値を持つ
- 材質インスタンスのT3D(UTF-8)には、親の材質と、上書きしたパラメータ(`ScalarParameterValues`など)と合成方法(`BasePropertyOverrides`)が書かれている
- 4つの材質で使われているノード:

| 種類 | ノード |
|---|---|
| 計算 | Multiply、Add、Power、OneMinus、LinearInterpolate、ComponentMask、AppendVector、Reroute |
| 定数・パラメータ | ScalarParameter、VectorParameter |
| テクスチャ | TextureSample、TextureCoordinate、Panner |
| 粒子・時間 | ParticleColor、DynamicParameter、Time |
| 変換が難しいもの | DepthFade(深度が要る)、MaterialFunctionCall(関数の中身も変換が要る)、CurveAtlasRowParameter(曲線の表が要る) |

- 粒子の色は1を超える値(例: 5倍の明るさ)を使っている。今の形式(RGBA各8bit)では失われる

## 2. データ形式の変更(version 3)

JSONのエミッターに次を足す。

| 項目 | 内容 |
|---|---|
| `material` | 変換したHLSLのファイル名(例: `MI_height_lighting01.hlsl`)。変換していなければ空文字。空でなければ再生側はこれで描き、`texture` / `tint`は使わない |
| `textures` | HLSLの`FxTexture0`〜 に順に割り当てるテクスチャのファイル名 |
| `textureLinear` | `textures`と同じ並びで、リニア(sRGBでない)で読むならtrue |
| `dynamicParams` | 粒子ごとの値の組の数(0〜4)。1組はfloat × 4 |
| `colorScale` | 色の倍率。本当の色は「binのRGB ÷ 255 × colorScale」、Aは「binのA ÷ 255」。1を超える明るさを残すため。記録側は、そのエミッターの全粒子・全フレームのRGBの最大値(1未満なら1)をcolorScaleにし、RGBをcolorScaleで割ってから0〜255にする。Aを同じ倍率で割らないのは、Niagaraの色はRGBが1万近くになることがあり、Aが0に潰れるため |
| `alignment` | 既存の値に`VelocityAligned`(板はカメラを向き、板の上を速度の向きに合わせる)を足す。NiagaraのVelocity Aligned用 |

binの粒子は、60バイトの後ろに`dynamicParams × 16`バイト(float × 4を組の数だけ)を付ける。エミッターごとに長さが変わるので、再生側はJSONの`dynamicParams`を見て読む。`dynamicParams`が0なら今と同じ60バイト。

Unity側の記録ツールは`dynamicParams = 0`、`colorScale = 1`、`material = ""`を書く(今と同じ見た目)。

## 3. 作るもの

UE側(`C:\Git\EffectRecorderUE`)

| ファイル | 内容 |
|---|---|
| `Content/Python/fxrec/material_t3d.py` | T3Dを読んで、ノードの一覧(種類・設定値・入力のつながり)にする。UEなしで動く |
| `Content/Python/fxrec/material_hlsl.py` | ノードの一覧をHLSLにする。UEなしで動く |
| `Content/Python/fxrec/recorder.py` | 材質をT3Dに書き出して変換し、`material` / `textures`をJSONに書く。`DynamicMaterialParameter`〜`3`を`read_vector4_attribute`で読む。色の倍率を決める |
| `Content/Python/fxrec/fxrec_file.py` | version 3の書き出し |
| `Tests/test_material.py` | 小さなT3Dの文字列からHLSLを作り、期待した式になるか |
| `Tests/data/*.t3d` | テスト用のT3D(自作の小さなもの。ストアの素材は入れない) |

Unity側(`C:\Git\EffectRecorder`)

| ファイル | 内容 |
|---|---|
| `FxRecFile.cs` | version 3の読み書き(`dynamicParams`・`colorScale`・`material`・`textures`) |
| `Editor/FxRecShader.cs`(新規) | 変換したHLSLを包んだUnityのシェーダーを`Assets/EffectRecorder/Generated/`に書き出す |
| `FxRecPlayer.cs` | `material`があれば、そのシェーダーで描く。板は粒子ごとの値を頂点のUV2〜UV5に入れる。メッシュ粒子は1粒ずつ描くので、粒子ごとの値と色は描画ごとのプロパティ(MaterialPropertyBlock)で渡す。`VelocityAligned`の板の向き |

## 4. 変換の仕方

- 出力は`EmissiveColor`(発光色)と`Opacity`(不透明度)だけを使う。Unlit(ライティングなし)の材質が対象
- 出力からノードをたどり、1ノードを1つの一時変数にする(例: `float3 n5 = n3 * n4;`)。同じノードは1回だけ計算する
- 型(float〜float4)は、各ノードの出力の要素数から決める。要素数が違う掛け算・足し算は、HLSLと同じく1要素のものを広げる
- パラメータは、材質インスタンスで上書きした値、なければ親の材質の既定値を定数にする
- 対応していないノードは、0か1の定数に置き換えてコメントに残し、記録のログに「未対応のノード」として出す。`DepthFade`は1(ぼかさない)にする

## 5. エンジン側との約束(たたき台。ユーザーが決める)

変換したHLSLは次の形になる。エンジン側の再生処理は、頂点シェーダーで`FxMaterialInput`を作ってピクセルシェーダーで`FxMaterial`を呼ぶ。

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

- 合成方法はJSONの`blend`で、エンジン側のパイプラインの設定で行う。`Additive`は「発光色をそのまま足す」(不透明度は使わない)、`Alpha`は「発光色 × 不透明度 + 背景 × (1 − 不透明度)」
- OBJのメッシュのUVはOBJの慣習どおり左下が原点。エンジン側は読み込むときにVを反転する(1 − V)
- `time`はUEのTimeノード(ワールドの時間)の代わり。エフェクトごとに0から数えるので、UVのスクロールの始まりの位置はUEと違ってよい

## 5.5確認の方法

- 数値: `Tests/test_material.py`(T3D → HLSLの変換)と、既存のテスト
- 見た目: Unityで`FxRecShader`が作ったシェーダーで記録を描き、`FxRecCapture.RunViewBatch`でUEのスクリーンショットと並べる。Niagara Slashの`NE_attack01`〜`05`で、斬撃の形・明るさ・消え方が近いこと

## 5.6実装中に分かったこと

| 分かったこと | 対応 |
|---|---|
| Niagaraの色はRGBが1万近くになることがある。Aも同じ倍率で割ると0に潰れる | `colorScale`はRGBだけに掛ける(2節) |
| UEの既定の粒子の材質はSphereMaskとTwoSidedSignを使う | 2つとも変換に対応した |
| UEの材質のUVは左上が原点 | `FxMaterialInput.uv`は左上原点と決めた。Unityで確かめるときは、包むシェーダーの側で上下を反転する |
| テクスチャごとに「端で止める(Clamp)」設定がある。UVをずらす材質では、繰り返すと見た目が大きく変わる | サンプラーを2つ(`FxSampler` / `FxSamplerClamp`)にした |
| 法線マップとして読むテクスチャ(`SamplerType=SAMPLERTYPE_Normal`)は、UEが0〜1を −1〜1に戻して読む | 変換したHLSLに`FxUnpackNormal`を入れる |
| sRGBでないテクスチャ(法線マップ・マスク)をリニアで読まないと値がずれる | JSONに`textureLinear`を足した |
| 背景(夜空・床)と自動露出で、UEとUnityの見え方が大きく違う | 比較のときは、UE側のレベルを空にして自動露出とブルームを切り、Unity側も背景を黒にする |

## 6. 採らなかった案

| 案 | 却下理由 |
|---|---|
| UEが内部で作るHLSLを取り出す | UEの共通コードに強く依存していて、切り離せない |
| `MaterialEditingLibrary`でノードをたどる | ノードの設定値を読む関数がそろっていない。T3Dなら全部書かれている |
| パラメータを定数バッファで渡す | エンジン側の仕事が増える。今は材質インスタンスごとにHLSLを作れば足りる |
| 色をfloat × 4で持つ | 粒子1つが12バイト増える。エミッターごとの倍率で足りる |

## 7. やらないこと

- ライティングのある材質、法線マップ、屈折・歪み、頂点を動かす処理(World Position Offset)
- 材質関数(MaterialFunctionCall)とCurve Atlasの変換。見つけたら報告する
- エンジン側(D3D12)の再生処理

## 8. 完了チェック

- [x] `Tests/test_material.py`と既存のテストが通る
- [x] `NE_attack01`を記録すると、`material`のHLSL・`textures`・粒子ごとの値が出る
- [x] Unityで変換したHLSLで描いた斬撃が、UEのスクリーンショットと形・消え方で近い(色味はUEのトーンマップの分だけ違う。閃光の`M_flare`は材質関数とCurve Atlasを使うので出ない)
- [x] `docs/design/plans/effect-recorder.md`のデータ形式をversion 3に更新する
