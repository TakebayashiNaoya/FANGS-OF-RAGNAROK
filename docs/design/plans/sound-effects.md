# サウンドとエフェクトの設計(図の 13 ページ目に追加予定)

要件: `docs/design/plans/sound-effects-requirements.md`

## 結論

- **演出の束(Cue)を中心に置く**。ゲームとアニメのイベントは `CueSystem::Play(名前, 場所・誰が・誰に)` を呼ぶだけ。中身(音・エフェクト・ヒットストップ・揺れ・振動・画面演出)は JSON に書く
- **XAudio2 に触るのは Main だけ、エフェクトの再生(記録方式)に触るのは RenderThread だけ**。他のスレッドからの要求はキューに積む
- 足音の地面の材質は、キャラクターの接地情報(CharacterMover)から取る

## 1. フレームの流れへの追加

| 位置 | 処理 | スレッド |
|---|---|---|
| 4'(既存) | Advance の中で、アニメのイベントを Cue の要求に変えて積む | ParallelFor |
| 8'''(新) | `CueSystem::Flush`:積まれた Cue を、音・エフェクト・ヒットストップ・揺れ・振動・画面演出に配る | Main |
| 10'(新) | `AudioSystem::Update`:3D 計算と XAudio2 への反映 | Main |
| 13(既存) | BuildFrame で、エフェクトのコマンドと画面演出の値を RenderFrame に書く | Main |
| RenderThread | エフェクトの再生と描画(EffectPass)、画面演出(PostEffectPass) | Render + Worker |

- Flush を PostAnimation の直後に置くのは、骨の位置が確定していて、攻撃の当たり(8'')と同じフレームで音と火花を出せるため

## 2. Cue(演出の束)

`cues/wolf.cues.json`:

```json
{
  "Wolf_Bite_Hit": {
    "sound":  { "sound": "SE_Bite_Hit" },
    "effect": { "effect": "FX_Blood_Spray", "at": "position" },
    "hitStop": { "instigator": 0.06, "target": 0.08 },
    "cameraShake": "Medium",
    "vibration": { "low": 0.4, "high": 0.8, "seconds": 0.15 },
    "screen": { "effect": "DamageVignette", "intensity": 0.6 }
  },
  "Wolf_Footstep": {
    "sound":  { "sound": "SE_Footstep_Wolf", "surface": "ground" },
    "effect": { "effect": "FX_Dust_Small", "at": "bone" }
  },
  "Wolf_Breath": {
    "effect": { "effect": "FX_Breath", "at": "bone", "follow": true }
  }
}
```

- 項目はすべて省略できる
- `at`:`position`(渡された位置)か `bone`(骨の位置)。`follow: true` なら、その骨に付いて動き続ける
- 振動と画面演出は、instigator か target が自分のキャラクター(LocalPlayer)のときだけ出す

```cpp
struct CueParams {
    Vector3 position, normal;
    ObjectRef<IGameObject> owner; BoneIndex bone = kNoBone;  // 骨に出すとき
    ObjectRef<IGameObject> instigator, target;
    SurfaceId surface = kNoSurface;                          // 当たった面の材質
};
cues.Play(CueId("Wolf_Bite_Hit"), params);   // どのスレッドからでも
```

- `CueSystem` は Application が所有。`Play` はキューに積むだけで、`Flush`(Main)が配る
- エンジンはゲームの型(Character)を知らないので、`IGameObject::GetAnimator()`(仮想関数、既定は nullptr)で Animator を取り出す。骨の位置・接地の材質・ヒットストップは Animator 経由
- 破棄済みの相手(`ObjectRef::Get()` が nullptr)の Cue は捨てる

### アニメのイベントとのつなぎ

animset の JSON に対応表を書く。

```json
"eventCues": {
  "Footstep": { "cue": "Wolf_Footstep" },
  "Howl":     { "cue": "Wolf_Howl", "bone": "Head" }
}
```

- 骨は、イベントの `params.leg`(IK 設定の脚の名前 → 足の骨)か `params.bone`、なければ表の `bone`

### ヒットストップ

- `Animator::ApplyHitStop(seconds)` を足す。残っている間は Advance で時間を進めない。ゲームの `SetTimeScale` とは別に持つ
- 止まるのはアニメだけ。エフェクトと音は止めない

## 3. サウンド(XAudio2)

```
AudioSystem  (Application が所有)
+ Play(sound, position) : SoundHandle   // どのスレッドからでも(キューへ)
+ Stop(handle, fade) / SetPosition(handle, pos)
+ PlayMusic(track, fade) / StopMusic(fade)   // BGM はクロスフェード
+ SetBusVolume(bus, volume) / SetPaused(bool)
+ Update()                                   // Main。XAudio2 に触るのはここだけ
+ Suspend() / Resume()                       // UWP の中断・再開
```

- バス:BGM / SE / Ambient / UI。リバーブ用の送り先を 1 つ
- ボイスは事前に作って使い回す。**全 SE をコンバーターで 1 つの形式にそろえる**(MS-ADPCM 48kHz、3D 用はモノラル、2D 用はステレオ)。同じ形式なら同じボイスで鳴らせるため
- BGM は ADPCM のストリーミング。IOThread が読み、Main の Update が XAudio2 に渡す
- ADPCM は XAudio2 がデコードする(独自のデコーダーは要らない)

`sounds/*.sound.json`:

```json
"SE_Footstep_Wolf": {
  "bus": "SE", "maxDistance": 30, "maxInstances": 8,
  "variants": { "Dirt": ["wolf_step_dirt_01", "wolf_step_dirt_02"], "Stone": ["wolf_step_stone_01"], "default": ["wolf_step_dirt_01"] }
}
```

- `variants`:地面の材質ごとの候補からランダムに選ぶ
- `maxInstances`:同じ音が同時に鳴る数の上限(50 体の足音で溢れないため)

### 3D・遮蔽・残響

- リスナー:位置はプレイヤーのキャラクター、向きはカメラ
- 3D:X3DAudio で距離減衰と定位
- 遮蔽:リスナーから音源へレイキャストし、当たったらローパスと音量を下げる
- 残響:レベルの JSON に `ReverbZone`(箱 + プリセット名)を置き、リスナーがいる Zone のプリセットに切り替える

## 4. エフェクト(記録方式)

試作の結果(`docs/design/pilots/effects-pilot-result.md`)を受けて、Effekseer はやめ、**Unity / Unreal で再生したエフェクトの全粒子を毎フレーム記録し、エンジンはそれを再生する**方式にする。記録ツールは別リポジトリ `C:\Git\EffectRecorder`、データ形式は `docs/design/plans/effect-recorder.md` の 2 節。

```
EffectSystem  (Main 側。再生の中身には触らない)
+ Play(effect, transform) : EffectHandle   // どのスレッドからでも(キューへ)
+ Stop(handle) / Attach(handle, owner, bone)
+ SetTint(handle, color) / SetScale(handle, scale)   // 全体の色・大きさの倍率
+ WriteFrame(RenderFrame&)                 // 13 BuildFrame。コマンドと追従先の位置を書く

FxRecBackend  (RenderThread 側。記録の再生に触るのはここだけ)
+ Apply(RenderFrame)                       // コマンドを反映
+ Update(dt)                               // 再生位置を進め、粒子から板・軌跡の頂点を作る(ジョブ)
+ Draw(view)                               // EffectPass から、ビューごと
```

- Main は RenderFrame にコマンドを書くだけ。再生中のインスタンスは RenderThread 側だけが持つ(4 ページ目の「ゲームは RenderContext に触らない」と同じ)
- 記録の中身は変えない。骨への追従は「再生位置・向きの変更」、色や大きさの変更は「全体の倍率」として扱う
- 描画は EffectPass(Forward と PostEffect の間)で、ビューごと。再生側で必要なこと(試作で分かったもの):
  - エミッターは JSON の並び順で描く(距離で並べ替えない。`sortByDistance` のエミッターだけ粒子を並べ替える)
  - 板の種類(Billboard / Stretched / Horizontal / Vertical / Mesh)、連番の今のコマと次のコマの混ぜ合わせ、軌跡(Trails)
  - ソフトパーティクル(深度テクスチャを読む)と、Lit の材質のライティング
- `EffectResource` = `.fxrec.json` + `.fxrec.bin` + テクスチャ(+ メッシュ・変換した HLSL)。読み込み時に bin のフレームごとの位置の表を作り、座標を Unity の左手系 Y 上からエンジンの座標系に直す

## 5. 画面演出

- PostEffectPass に名前付きの演出を置く(`DamageVignette`、`LowHealth` など)
- `ScreenEffectController::Trigger(name, intensity)` で強さを上げ、時間で減衰させる。値は BuildFrame で RenderFrame に書く

## 6. 地面の材質

- 物理に `SurfaceId`(Dirt / Stone / Water / Grass / Flesh など)を足す:`BodyDesc.surface`、`HitResult.surface`。地形のメッシュはマテリアル名から決める
- `CharacterMover::GetGroundSurface()`:接地している面の材質。Character が Awake で `Animator::BindGround(mover)` し、Cue の `"surface": "ground"` はここから取る

## 7. 他のページとのつながり

| ページ | 変更 |
|---|---|
| 1 全体像 | AudioSystem / EffectSystem / CueSystem を追加。フレームの流れに 8''' と 10' |
| 2 GameObject | `IGameObject::GetAnimator()` |
| 4 描画 | EffectPass を追加。RenderFrame にエフェクトのコマンドと画面演出の値 |
| 7 リソース | SoundResource(ADPCM)、EffectResource、CueResource |
| 10 カメラ | `LocalPlayer.character : ObjectRef<IGameObject>`(自分のキャラクターの判定用) |
| 11 物理 | `SurfaceId`、`CharacterMover::GetGroundSurface()` |
| 12 アニメーション | `Animator::ApplyHitStop`、`BindGround`、animset の `eventCues` |

## 8. 検討した案

| 案 | 却下理由 |
|---|---|
| 音とエフェクトをゲームのコードで個別に呼ぶ | 手ごたえの調整のたびにコードを直すことになる |
| 足ごとにレイキャストして材質を取る | CharacterMover の接地情報で足りる |
| Effekseer を使う | 試作で記録方式に変えた。組み直す手作業が要らず、見た目も元とほぼ同じ(画素の差は平均 1/255 以下) |
| 再生中のエフェクトを Main で進め、描画だけ RenderThread | 2 つのスレッドが同じインスタンスに触る |
| 画面演出をエフェクトで作る | ビューごとの制御がしにくい(ユーザーがポストエフェクトと決定) |

## 9. 残る論点

- ループするエフェクト(焔など)のつなぎ目、乱数違いの複数パターン、歪み(Shockwave)の再現は、試作でもやっていない。必要になったら決める
- Xbox での描画コストは未計測
