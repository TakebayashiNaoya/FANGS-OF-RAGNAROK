# 新エンジン設計メモ

`ClassDiagram.drawio`（draw.io、13 ページ）の設計で決めたことと、その理由をまとめる。
ProjectBeast をもとに、エンジンとゲームをきちんと分けて作り直す。

参考にしたもの:

- ProjectBeast（このリポジトリ）
- Test（TatsuyukiSugahara/Test の aqEngine）
- FANGS-OF-RAGNAROK-ecs（Xbox 向けに途中まで作ったもの。入力・遮蔽回避・当たり判定）

## 前提

| 項目 | 内容 |
|---|---|
| ジャンル | Elden Ring のようなソウルライク |
| 対象 | Xbox One S。UWP Dev Mode（道A）で「Game」分類にしてデプロイする |
| 制約 | CPU は 4 コア占有 + 2 コア共有（Jaguar 1.75GHz）、RAM 5GB、D3D12 FL12_0 |
| マルチプレイ | 入れるかもしれない。入れるならローカル（画面分割） |
| ECS | 使わない。IGameObject の継承方式 |
| アロケーター | CPU のメモリはアロケーターから取る（`HeapAllocator` で使用量を追跡、`StackAllocator` はフレーム内の一時領域）。GPU リソースは対象外（`CreateCommittedResource` をそのまま使う） |

## 1. GameObject と進行（図の 2 ページ目）

- `Application`（エンジン）が `vector<unique_ptr<IGameObject>>` で所有する。
  - 生成は `CreateObject<T>()`。既定でアクティブレベルに所属し、`Persistent` を付けると常駐する。
  - 破棄は `RequestDestroy` で予約し、フレームの最後にまとめて行う。
- ライフサイクルは Unity と同じ名前にそろえる。
  - `Awake(params)`：生成直後に呼ぶ。**Load Thread やジョブから呼ばれることがあるので、自分のことだけをする**（他のオブジェクトの検索や生成、シングルトンの書き換えは禁止）。
  - `Start`：Main で、登録された次のフレームの最初の Update の直前に 1 回呼ぶ。他のオブジェクトとの連携はここで行う。
  - `Update` / `OnDestroy`：Main で呼ぶ。
- 更新順は `UpdateGroup`（PreUpdate / Gameplay / ParallelGameplay / PostPhysics / PostAnimation / Camera / Late）で決める。どのグループかはクラス側が決め、同じグループの中は登録順。
- `ParallelGameplay` は `JobSystem::ParallelFor` で分割して実行する。
  - 書き換えてよいのは自分だけ。
  - 読んでよいのは、前のグループで確定した値だけ。
  - `CreateObject` と `RequestDestroy` はキューに予約するだけ。
- フレームをまたいで覚える参照は `ObjectRef<T>`（ID と世代番号）を使う。
  - 破棄済みなら `Get()` が `nullptr` を返す。
  - 実際の破棄はフレームの最後の Main だけなので、ジョブの中で `Get()` しても安全。
- 1 フレームの `dt` は最大 1/30 秒に制限する。30fps を下回ると、ゲーム全体（Update・物理・アニメ）が同じ割合でゆっくりになる。
- 進行はエンジンの `GameStateMachine` と `IGameState`（予約制で遷移）。中身の状態（Title / Loading / InGame / Result）はゲームが作る。

## 2. レベルとロード（3 ページ目）

- シーン方式ではなくレベル方式にする。同じ世界のまま進行や画面だけを変えたいため。
- `LevelManager` が `.level.json` を読み、`GameObjectRegistry`（型名から生成関数を引く表）でオブジェクトを作る。
- JSON のオブジェクトは `type` / `name` / `transform` / `params`。`params` は `Awake` に渡す。
- 実行中に生成する物のリソースは、JSON の `preload` に書いて先読みする。
- ロードの流れ：IO スレッドでファイルを読む → ジョブでパース・生成・Awake → Main の同期点で登録する。

## 3. スレッド（6 ページ目）

- 専用スレッドは Main（Core0）、Render（Core1）、IO（共有コア）の 3 本だけ。
- それ以外の並列処理はすべて `JobSystem`（`Schedule` / `ParallelFor` / `Wait`）で行う。ワーカーは 4 本で、コアに固定する。

## 4. 描画（4 ページ目）

- 案C：ゲーム側は `RenderContext` に触らない。コマンドを積み、`RenderThread` が実行する。
- `RenderScene` は保持方式にし、全ビューで共有する。
  - ModelRender は Start でプロキシを登録し、動いたときだけ `UpdateTransform` を呼ぶ。
  - 行列は `SceneBuffer<T>` のダブルバッファで持つ。入れ替えた直後に、直前フレームの dirty 分を書く側へコピーする。
- ワールド行列の転送とスキニングは、1 フレームに 1 回だけ行う。
- `RenderView` はビューごとに持つ。中身はカメラ、描画先、ビューポート、パイプライン、影（Own / ShareMain / None）、オクルージョン、レイヤーマスク、更新頻度、カリング結果。
- 2 つ目のカメラの用途：キャラクターの 3D プレビュー（`Preview` レイヤー、RenderTexture）と、画面分割。

## 5. UI（5 ページ目）

- IGameObject とは別系統にする：`UIContext` / `UIScreen` / `UIObject`。
- UIObject はコンポーネント式。Transform は必須、描画系は 1 つまで、振る舞い系は任意。
- レイアウトは JSON で持つ。
- ゲームから UI へは Push 型（`Screens().Find<T>()->SetXxx()`）。UI はゲームオブジェクトを参照しない。
- UI 表示中はマウスカーソルを出し、クリックで操作できる。

## 6. リソース（7 ページ目）

- `Load<T>` はどのスレッドからでも呼べ、`ResourceHandle<T>` を返す。
- Decode と CreateGpu はジョブで行い、Copy Queue で転送する。Fence の完了で Ready になる。
- 依存しているリソースがすべて Ready になって初めて、親も Ready になる。
- レベルを Unload した後に `UnloadUnused` を呼ぶ。GPU リソースは Fence を待ってから遅延解放する。

## 7. 入力と操作（8・9 ページ目）

詳細は `docs/design/plans/input-actions.md`。

- ゲームのコードは**アクション**で読む（`GetActions().Get(Action::Roll).IsTapped()`）。ボタン名はコードに出さない（Unity の Input System と同じ考え方）。
  - どのボタン・キーがどのアクションかは `actions.json` に書く。マップは `Gameplay` と `UI` の 2 つ。
  - 短押し、長押し、押した時刻（先行入力用）はアクションごとに判定する。
- パッドのボタンは位置の名前（`South / East / West / North` など）で呼ぶ。画面のボタン表示（A か ×）は、最後に使ったパッドの機種で切り替える。
  - Xbox 版は Windows.Gaming.Input、PC 版は SDL3 で読む（PC 版は Xbox のパッドと DualSense を使える）。
- キーの割り当ては、ゲーム内の画面（`KeyConfigScreen`）で変えられる。既定との差分だけを `input_overrides.json` に保存する。
- 操作を受け付ける画面（`UIScreen::capturesInput`）が出ている間だけ `UI` マップに切り替える。HUD は対象外。
- ローカルマルチを入れるなら、`LocalPlayer` ごとに ActionSet を持つ。振動も扱う。
- キャラクターは入力を直接読まない。
  - `ICharacterController`（Player / AI）が、ゲーム定義の `CharacterCommand`（意図）を作る。
  - Character のステートは `CharacterCommand` だけを読む。プレイヤー専用のクラスは作らず、プレイヤーと敵は同じ `Character` で、Controller の差し替えで区別する。

## 8. カメラ（10 ページ目）

- `CameraManager` はカメラ枠ごとに、モードの切り替えと補間、演出（Modifier）、遮蔽物の回避を行う。
- モードは FreeLook / LockOn / Event / Debug。
- ロックオンは役割を分ける。
  - 状態（誰を狙うか）：Character
  - 選択：LockOnSystem（視点を引数で受け取るので AI も使える）
  - 演出：Camera と UI
- `LocalPlayer`：ローカルマルチの継ぎ目。今は 1 人分だけ作る。

## 9. 物理（11 ページ目）

- Jolt Physics を使う。ヘッダーに Jolt の型は出さない。
- 固定タイムステップは 60Hz、1 フレームの上限は 2 ステップ、描画側で補間する。
  - 30〜60fps では、フレームによって 0〜2 回ステップして実時間に追いつく。30fps を下回ると `dt` の上限（1/30 秒）でゲーム全体がゆっくりになる。
- クエリは const。ステップ中以外はジョブから同時に呼べる。
- 本体の種類は Static / Kinematic / Dynamic / Trigger。剛体の力学とラグドールも使う。
- ラグドールの本体は死亡時（`GoLimp`）に初めて作る。生きている間にアニメで本体を動かすこと（`DriveFromAnimation`）はしない（→ 10）。
- キャラクターは `CharacterMover`（CharacterVirtual を包む）で動かす。
- 攻撃の当たり判定は、アニメの Hit 窓の間だけ、武器のカプセルを前のフレームから今のフレームまで掃引して行う（→ 10）。

## 10. アニメーション（12 ページ目）

詳細は `docs/design/plans/animation.md`。

- エンジンは再生機（`Animator`）までを担当する。どのアニメを流すかは、ゲームの CharacterStateMachine が `Play()` で指示する。アニメーション専用のステートマシンは作らない（状態が二重になるため）。
- 1 フレームの処理を 2 段に分ける。
  - `Advance`（4 の後、全員が毎フレーム）：時間・イベント・ルートモーションを確定する。
  - `Evaluate`（8 の後）：姿勢・IK・スキン行列を計算する。距離と、ビューに映っているかで LOD を決めて間引く。
  - 間引いてもイベントとルートモーションは変わらない。Hit 窓が開いている間は Full で計算する。
- `UpdateGroup::PostAnimation`（Main）を新設した。攻撃判定と GoLimp はここで行う。
- イベントはクリップごとの `.anim.json` に書く。瞬間（足音など）と時間窓（Hit / Cancel など）の 2 種類。
  - 時間窓は各レイヤーの最新の再生だけで判定する。キャンセルしてフェードアウト中の攻撃の判定は残らない。
  - レイヤーは独立している。別レイヤーの攻撃は、ゲームが `StopLayer` で止める。
- ブレンドはクロスフェード、ブレンドスペース（1D / 2D）、骨マスク付きレイヤー（狼は首・頭、人型は上半身）。
- サンプリング・ブレンド・2 ボーン IK の中身は ozz-animation を使う。ozz の型は Animation モジュールの外へ出さない。
- IK は Full のときだけ。接地 → 体の傾き → 脚の 2 ボーン IK → 頭の注視。
- 素材は FBX を Blender で glTF に書き出し、変換ツールが読む（モデルは cgltf、骨格とアニメは ozz の gltf2ozz）。
- 狼の素材（`SK_Wolf_Realistic.fbx`）に合わせて決めたこと：ルートモーションは `Root` 骨のトラックを使う。移動は速度の 1D ブレンド。横歩きがないので、ロックオン中も体は進行方向へ向け、頭を注視 IK で敵に向ける。

## 11. サウンドとエフェクト（13 ページ目）

詳細は `docs/design/plans/sound-effects.md`。

- 演出の束（Cue）を中心に置く。ゲームとアニメのイベントは `CueSystem::Play(名前, 場所・誰が・誰に)` を呼ぶだけ。音・エフェクト・ヒットストップ・揺れ・振動・画面演出は `cues/*.json` に書く。
  - アニメのイベントは animset の `eventCues` で Cue に対応づける。
  - Cue は PostAnimation の直後（8'''）に Main でまとめて配る。攻撃が当たったフレームのうちに音と火花が出る。
  - エンジンはゲームの型を知らないので、`IGameObject::GetAnimator()` で Animator を取り出す。骨の位置・接地の材質・ヒットストップは Animator 経由。
- サウンドは XAudio2 で自作する。XAudio2 に触るのは Main の `AudioSystem::Update`（10'）だけ。
  - 3D 音響（X3DAudio）、BGM のクロスフェード、地面の材質で変わる足音、遮蔽（レイキャスト）、残響（レベルの ReverbZone）。
  - SE は全部 MS-ADPCM 48kHz にそろえ、ボイスを使い回す。BGM は ADPCM のストリーミング。
- エフェクトは**記録方式**：Unity / Unreal で再生したエフェクトの全粒子を毎フレーム記録し、エンジンは再生するだけ（試作の結果：`docs/design/pilots/effects-pilot-result.md`、データ形式：`docs/design/plans/effect-recorder.md`）。Effekseer は使わない。
  - 再生に触るのは RenderThread だけで、Main は RenderFrame にコマンドを書く。描画は EffectPass。
  - 骨への追従は再生位置・向きの変更、色や大きさは全体の倍率として扱う（記録の中身は変えない）。
- 画面全体の演出はポストエフェクト（`ScreenEffectController`）。
- 地面の材質は物理の `SurfaceId`。足音はキャラクターの接地情報（`CharacterMover::GetGroundSurface`）から取る。

## 未決・次に決めること

- 狼のアニメの追加制作（横歩き・後ろへの回避・連続攻撃・強攻撃）
- エフェクト：ループのつなぎ目、歪み（Shockwave）の再現、Xbox での描画コスト
- セーブ
- 画面分割を入れる場合の、パッドの割り当てと HUD の配置
