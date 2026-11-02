# アニメーション設計(図の12ページ目)

要件: `docs/design/plans/animation-requirements.md`

## 結論

- エンジンは「再生機(Animator)」までを持つ。どのアニメを流すかはゲームのCharacterStateMachineが`Play()`で指示する。アニメーション用のステートマシンは作らない
- 1フレームを**Advance(時間・イベント・ルートモーション)** と**Evaluate(姿勢・IK・行列)** に分ける。Advanceは全員が毎フレーム行い、EvaluateだけをLODで間引く。間引いてもイベントとルートモーションは変わらない
- イベントは「瞬間(足音など)」と「時間窓(攻撃判定・キャンセル受付など)」の2種類。時間窓は**各レイヤーの最新の再生だけ**で判定するので、キャンセルしてフェードアウト中の攻撃の判定は残らない

## 1. フレームの流れへの追加

| 位置 | 処理 | スレッド |
|---|---|---|
| 4' | `AnimationSystem::Advance(dt)`:時間・フェードを進め、イベントとルートモーションを確定 | ParallelFor |
| 5〜6' | CharacterのUpdate:イベント・時間窓を見て`Play()`。ルートモーションをCharacterMoverへ | Main / ParallelFor |
| 8' | `AnimationSystem::Evaluate()`:サンプリング → ブレンド → IK → 行列 → スキン行列 | ParallelFor |
| 8''(新設) | `UpdateGroup::PostAnimation`:骨の位置を使う処理(WeaponHitDetector、GoLimp) | Main |

- Updateの時点で、そのフレームのイベントとルートモーションは確定している。Updateで`Play()`したアニメは、同じフレームのEvaluateから見た目に出る
- EvaluateをPostPhysicsの後に置くのは、IKが物理の確定後の位置でクエリするため

## 2. クラス(エンジン)

```
Animator  (部品。ModelRender と同じく「持つだけ」)
- m_set : ResourceHandle<AnimSetResource>
- m_layers : vector<AnimationLayer>     // 0 = 全身
- m_rootMotion / m_events               // Advance で確定
- m_pose / m_modelMatrices
- m_poseSource : Animation / Ragdoll
- m_model : ModelRender*                // 非所有。proxy とワールド行列
+ Bind(model) : void                    // Awake
+ Play(state, fade, layer = 0) : void
+ StopLayer(layer, fade) : void         // そのレイヤーの窓も閉じる
+ SetLayerWeight / SetBlendParam / SetTimeScale
+ SetLookAtTarget(pos, weight) / SetFootIkEnabled(bool)
+ SetPoseSource(Animation / Ragdoll)
+ GetEvents() / IsWindowActive(name) / GetRootMotion()
+ GetNormalizedTime(layer) / IsFinished(layer)
+ FindBone(name) / GetBoneWorld(bone)   // GetBoneWorld は PostAnimation 以降で今フレームの値

AnimationLayer : 再生 (最新 + フェードアウト中) / BoneMask / 重み / ブレンドパラメータ
AnimationPlayback : Clip か BlendSpace / time / weight
AnimationSystem (Application が所有) : Advance / Evaluate / LOD の決定
```

- 書き換えは持ち主だけ。ParallelGameplayから`Play()`してよい
- クロスフェードは線形。レイヤーは上書き合成(骨マスク × 重み)。ルートモーションはレイヤー0だけ
- レイヤーは互いに独立。別レイヤーの攻撃をキャンセルするときは、ゲームが`StopLayer()`を呼ぶ

## 3. イベント

クリップごとのJSONに書く(FBXを書き出し直しても消えないように、バイナリとは別ファイル)。

```json
{
  "clip": "Wolfr_Attack_Bite_Forward.anim", "loop": false, "rootMotion": true,
  "events": [
    { "name": "Tracking", "start": 0.00, "end": 0.35 },
    { "name": "Hit",      "start": 0.40, "end": 0.55, "params": { "bone": "Jaw" } },
    { "name": "Cancel",   "start": 0.70, "end": 1.00 },
    { "name": "Footstep", "time": 0.62, "params": { "leg": "FL" } }
  ]
}
```

- `time` = 瞬間イベント、`start` / `end` = 時間窓。名前の意味はゲームが決める
- 瞬間イベントは、前回から今回の時刻の間に入ったものを出す。時間窓は今の時刻が窓の中なら有効
- 時間窓は2フレーム(0.067秒)以上の長さにする(データの決まり)
- `Footstep`の`leg`は、下のIK設定の脚の名前。足の骨はそこから引く

## 4. ルートモーション

- 狼の素材(`FBX Unreal (+Root Bone)/*_RM.fbx`)は`Root`骨に移動とyawが入っている。コンバーターはこれをルートモーションのトラックにする
- 上下の移動(落下・崖登り)は、JSONに`"rootMotionY": true`を書いたクリップだけ出す

```cpp
auto rm = m_animator.GetRootMotion();
m_mover.Move(m_rotation * rm.deltaPos / dt, dt);
m_yaw += rm.deltaYaw;
```

## 5. ブレンドスペース

- 狼の移動は速度の1D(素材の実測値0 / 0.94 / 2.4 / 5.6 m/sを軸の値にする)
- 素材に横歩きがないので、**狼はロックオン中も体を進行方向へ向け**、頭を注視IKで敵へ向ける
- 2D(方向 × 速度)は人型の敵用。三角形分割して補間する

## 6. IK(LOD Fullのときだけ)

animsetのJSONに骨の名前を書く。脚の本数は自由(人型2本 / 狼4本で同じコード)。

```json
"ik": {
  "legs": [
    { "name": "FL", "upper": "L UpperArm", "lower": "L Forearm", "end": "L Hand",      "foot": "L Finger0", "group": "front" },
    { "name": "FR", "upper": "R UpperArm", "lower": "R Forearm", "end": "R Hand",      "foot": "R Finger0", "group": "front" },
    { "name": "BL", "upper": "L Thigh",    "lower": "L Calf",    "end": "L HorseLink", "foot": "L Foot",    "group": "rear" },
    { "name": "BR", "upper": "R Thigh",    "lower": "R Calf",    "end": "R HorseLink", "foot": "R Foot",    "group": "rear" }
  ],
  "body": { "root": "Pelvis", "front": "Spine2" },
  "lookAt": { "chain": ["Spine2", "Neck", "Head"] }
}
```

- 順番:接地のレイキャスト → 体の傾き(前後の脚の高さの差)→ 脚の2ボーンIK → 頭の注視
- IKは見た目だけで、ルートモーションには影響しない

## 7. LOD

| LOD | 条件(目安) | Evaluate |
|---|---|---|
| Full | 近い | 毎フレーム、IKあり |
| Reduced | 中くらい | 2フレームに1回、IKなし |
| Hidden | どのビューにも映っていない | しない |

- Hit窓が開いている間はFullにする(攻撃判定に骨の位置が要るため)

## 8. リソース

| リソース | 中身 |
|---|---|
| SkeletonResource(既存名) | 骨の階層・バインドポーズ・名前 |
| AnimationClipResource(既存名) | 骨トラック、ルートモーション、イベント(JSONから) |
| BlendSpaceResource(新) | サンプルの配置 |
| AnimSetResource(新) | ステート名 → Clip / BlendSpace、骨マスク、IK設定 |

- 素材の流れ:FBX → BlenderでglTFに書き出す → 変換ツールが読む(モデルはcgltf、骨格とアニメはozzのgltf2ozz)。FBX SDKは使わない
- 姿勢の計算(サンプリング・ブレンド・2ボーンIK)はozz-animationで行う。ozzの型はAnimationモジュールの外へ出さず、AnimatorのAPIは変えない
- 変換の前提(狼の素材で確認済み):30fps、Z-up、単位cm。スキンのウェイトは上位4本に削る
- gltf2ozzで`Root`骨のルートモーションがそのまま扱えるかは、実装時に確認する

## 9. 他のページとのつながり

- **物理(11)**:ラグドールの本体は`GoLimp`のときに作る(生きている間にアニメで本体を動かす`DriveFromAnimation`はやめる)。PostAnimationで今の姿勢から本体を作り、次のフレームから`SetPoseSource(Ragdoll)`にする
- **攻撃判定(11)**:WeaponHitDetectorはPostAnimationで、Hit窓の間だけ「前フレームの骨の位置 → 今の位置」をカプセルで掃引する。窓の最初のフレームは今の位置でOverlapだけ
- **描画(4)**:Evaluateがスキン行列を`RenderScene::UpdateSkinPalette`に書く
- **操作(9)**:ステートはCharacterCommandとアニメの窓(Cancelなど)を見て遷移する

## 10. 検討した案

| 案 | 却下理由 |
|---|---|
| エンジンにアニメーション用ステートマシン | CharacterStateMachineと状態が二重になる |
| AdvanceとEvaluateを分けない | 間引いたキャラクターでイベントやルートモーションが欠ける |
| イベントをFBXに埋め込む | 書き出し直しで消える |
