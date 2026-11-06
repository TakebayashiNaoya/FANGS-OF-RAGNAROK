# 新エンジン設計メモ

`ClassDiagram.drawio`（draw.io、13ページ）の設計で決めたことと、その理由をまとめる。
ProjectBeastをもとに、エンジンとゲームをきちんと分けて作り直す。

参考にしたもの:

- ProjectBeast（このリポジトリ）
- Test（TatsuyukiSugahara/TestのaqEngine）
- FANGS-OF-RAGNAROK-ecs（Xbox向けに途中まで作ったもの。入力・遮蔽回避・当たり判定）

## 前提

| 項目 | 内容 |
|---|---|
| ジャンル | Elden Ringのようなソウルライク |
| 対象 | Xbox One S。UWP Dev Mode（道A）で「Game」分類にしてデプロイする |
| 制約 | CPUは4コア占有 + 2コア共有（Jaguar 1.75GHz）、RAM 5GB、D3D12 FL12_0 |
| マルチプレイ | 入れるかもしれない。入れるならローカル（画面分割） |
| ECS | 使わない。IGameObjectの継承方式 |
| アロケーター | CPUのメモリはアロケーターから取る（`HeapAllocator`で使用量を追跡、`StackAllocator`はフレーム内の一時領域）。GPUリソースは対象外（`CreateCommittedResource`をそのまま使う） |

## 1. GameObjectと進行（図の2ページ目）

- `Application`（エンジン）が`vector<unique_ptr<IGameObject>>`で所有する。
  - 生成は`CreateObject<T>()`。既定でアクティブレベルに所属し、`Persistent`を付けると常駐する。
  - 破棄は`RequestDestroy`で予約し、フレームの最後にまとめて行う。
- ライフサイクルはUnityと同じ名前にそろえる。
  - `Awake(params)`：生成直後に呼ぶ。**Load Threadやジョブから呼ばれることがあるので、自分のことだけをする**（他のオブジェクトの検索や生成、シングルトンの書き換えは禁止）。
  - `Start`：Mainで、登録された次のフレームの最初のUpdateの直前に1回呼ぶ。他のオブジェクトとの連携はここで行う。
  - `Update` / `OnDestroy`：Mainで呼ぶ。
- 更新順は`UpdateGroup`（PreUpdate / Gameplay / ParallelGameplay / PostPhysics / PostAnimation / Camera / Late）で決める。どのグループかはクラス側が決め、同じグループの中は登録順。
- `ParallelGameplay`は`JobSystem::ParallelFor`で分割して実行する。
  - 書き換えてよいのは自分だけ。
  - 読んでよいのは、前のグループで確定した値だけ。
  - `CreateObject`と`RequestDestroy`はキューに予約するだけ。
- フレームをまたいで覚える参照は`ObjectRef<T>`（IDと世代番号）を使う。
  - 破棄済みなら`Get()`が`nullptr`を返す。
  - 実際の破棄はフレームの最後のMainだけなので、ジョブの中で`Get()`しても安全。
- 1フレームの`dt`は最大1/30秒に制限する。30fpsを下回ると、ゲーム全体（Update・物理・アニメ）が同じ割合でゆっくりになる。
- 進行はエンジンの`GameStateMachine`と`IGameState`（予約制で遷移）。中身の状態（Title / Loading / InGame / Result）はゲームが作る。

## 2. レベルとロード（3ページ目）

- シーン方式ではなくレベル方式にする。同じ世界のまま進行や画面だけを変えたいため。
- `LevelManager`が`.level.json`を読み、`GameObjectRegistry`（型名から生成関数を引く表）でオブジェクトを作る。
- JSONのオブジェクトは`type` / `name` / `transform` / `params`。`params`は`Awake`に渡す。
- 実行中に生成する物のリソースは、JSONの`preload`に書いて先読みする。
- ロードの流れ：IOスレッドでファイルを読む → ジョブでパース・生成・Awake → Mainの同期点で登録する。

## 3. スレッド（6ページ目）

- 専用スレッドはMain（Core0）、Render（Core1）、IO（共有コア）の3本だけ。
- それ以外の並列処理はすべて`JobSystem`（`Schedule` / `ParallelFor` / `Wait`）で行う。ワーカーは4本で、コアに固定する。

## 4. 描画（4ページ目）

- 案C：ゲーム側は`RenderContext`に触らない。コマンドを積み、`RenderThread`が実行する。
- `RenderContext`はGPUに直接触る下の層にする。Device、Queue、SwapChain、RTV、Allocator、CommandList、Fenceを持つ。
  - `Renderer`はシーンとビューをまとめる上の層で、シーンを描くときに作る。➡`Renderer`に今のコードを入れると、シーンを足すときにGPUの部分をまた外へ出すことになるため、層を分ける。
- 描画は次の順で作る。
  1. `Main.cpp`の描画を、動きを変えずに`RenderContext`へ移す。Allocatorは1つのままにする。➡移す作業と動きを変える作業を分けるため。
  2. ウィンドウとループを`Application`へ移す。
  3. Allocatorをバッファの数だけ持ち、CPUとGPUを重ねる。1フレームの時間はCPUとGPUの足し算から、遅いほうの時間になる。➡定数バッファのように毎フレーム書くものの置き場所を、三角形より先に作るため。
  4. ImGuiを入れ、`Memory`の一覧と1フレームのmsとFPSを出す。ImGuiは自分のシェーダーを持つため、三角形より先に動かせる。
     - ImGuiは`ThirdParty/imgui/`にコピーして入れる。➡`git clone`だけでビルドできるようにするため。
     - `RenderFrame`を`BeginFrame`と`EndFrame`に分け、その間で描く。➡三角形も同じ場所に入るため。
     - ImGuiは`render::DebugOverlay`に閉じ込める。➡GPUの層の`RenderContext`が、`Memory`や画面の中身を知らないようにするため。
     - 窓の文字は日本語にする。今はWindowsのメイリオを読む。➡Xboxにはないため、UWPの構成を作るときに配布できるフォントへ替える。
     - msは、待ちを除いたCPUの処理時間と1フレームの時間を出す。➡`1000 / FPS`だけでは画面を待つ時間も入り、処理の重さが読めないため。
  5. Profile構成を足す。Debugは最適化なしでImGuiと一覧あり、Profileは最適化ありでImGuiと一覧あり、Releaseは製品の形でどちらもなしにする。➡最適化した状態の重さを測りながら、製品には計測の処理を残さないため。
     - 一覧とImGuiの切り替えは、`_DEBUG`から`FANG_DEBUG_OR_PROFILE`へ替える。➡Profileでは`_DEBUG`が定義されないため。
  6. GPUのタイムスタンプを取り、GPUの処理時間をImGuiに足す。➡三角形の重さをすぐ見るため。
  7. シェーダーと三角形を描く。
  8. 処理が増えたら、区間ごとのmsをImGuiに足す。
- ウィンドウの大きさの変更（`WM_SIZE`、`ResizeBuffers`）と描く解像度は、まだ決めない。今のバッファは作ったときの大きさのままで、DXGIが引き伸ばして映す。➡解像度で中身が変わるものを書くときに決める。
- `RenderScene`は保持方式にし、全ビューで共有する。
  - ModelRenderはStartでプロキシを登録し、動いたときだけ`UpdateTransform`を呼ぶ。
  - 行列は`SceneBuffer<T>`のダブルバッファで持つ。入れ替えた直後に、直前フレームのdirty分を書く側へコピーする。
- ワールド行列の転送とスキニングは、1フレームに1回だけ行う。
- `RenderView`はビューごとに持つ。中身はカメラ、描画先、ビューポート、パイプライン、影（Own / ShareMain / None）、オクルージョン、レイヤーマスク、更新頻度、カリング結果。
- 2つ目のカメラの用途：キャラクターの3Dプレビュー（`Preview`レイヤー、RenderTexture）と、画面分割。
- GPU全体のmsはImGuiに出し、内訳はPIXで見る。
  - 描画の最小形ではコードを触らない。exeをPIXから起動してGPUキャプチャを取る。
  - 三角形のあと、パスが2つ以上になったら、`PIXBeginEvent` / `PIXEndEvent`で区間に名前を付ける。マーカーはフラグで切り替え、Releaseには入れない。➡今のEventsは6行で、名前がなくても読めるため。
  - TODO: フラグの名前と、有効にする構成は入れるときに決める。

## 5. UI（5ページ目）

- IGameObjectとは別系統にする：`UIContext` / `UIScreen` / `UIObject`。
- UIObjectはコンポーネント式。Transformは必須、描画系は1つまで、振る舞い系は任意。
- レイアウトはJSONで持つ。
- ゲームからUIへはPush型（`Screens().Find<T>()->SetXxx()`）。UIはゲームオブジェクトを参照しない。
- UI表示中はマウスカーソルを出し、クリックで操作できる。

## 6. リソース（7ページ目）

- `Load<T>`はどのスレッドからでも呼べ、`ResourceHandle<T>`を返す。
- DecodeとCreateGpuはジョブで行い、Copy Queueで転送する。Fenceの完了でReadyになる。
- 依存しているリソースがすべてReadyになって初めて、親もReadyになる。
- レベルをUnloadした後に`UnloadUnused`を呼ぶ。GPUリソースはFenceを待ってから遅延解放する。

## 7. 入力と操作（8・9ページ目）

詳細は`docs/design/plans/input-actions.md`。

- ゲームのコードは**アクション**で読む（`GetActions().Get(Action::Roll).IsTapped()`）。ボタン名はコードに出さない（UnityのInput Systemと同じ考え方）。
  - どのボタン・キーがどのアクションかは`actions.json`に書く。マップは`Gameplay`と`UI`の2つ。
  - 短押し、長押し、押した時刻（先行入力用）はアクションごとに判定する。
- パッドのボタンは位置の名前（`South / East / West / North`など）で呼ぶ。画面のボタン表示（Aか ×）は、最後に使ったパッドの機種で切り替える。
  - Xbox版はWindows.Gaming.Input、PC版はSDL3で読む（PC版はXboxのパッドとDualSenseを使える）。
- キーの割り当ては、ゲーム内の画面（`KeyConfigScreen`）で変えられる。既定との差分だけを`input_overrides.json`に保存する。
- 操作を受け付ける画面（`UIScreen::capturesInput`）が出ている間だけ`UI`マップに切り替える。HUDは対象外。
- ローカルマルチを入れるなら、`LocalPlayer`ごとにActionSetを持つ。振動も扱う。
- キャラクターは入力を直接読まない。
  - `ICharacterController`（Player / AI）が、ゲーム定義の`CharacterCommand`（意図）を作る。
  - Characterのステートは`CharacterCommand`だけを読む。プレイヤー専用のクラスは作らず、プレイヤーと敵は同じ`Character`で、Controllerの差し替えで区別する。

## 8. カメラ（10ページ目）

- `CameraManager`はカメラ枠ごとに、モードの切り替えと補間、演出（Modifier）、遮蔽物の回避を行う。
- モードはFreeLook / LockOn / Event / Debug。
- ロックオンは役割を分ける。
  - 状態（誰を狙うか）：Character
  - 選択：LockOnSystem（視点を引数で受け取るのでAIも使える）
  - 演出：CameraとUI
- `LocalPlayer`：ローカルマルチの継ぎ目。今は1人分だけ作る。

## 9. 物理（11ページ目）

- Jolt Physicsを使う。ヘッダーにJoltの型は出さない。
- 固定タイムステップは60Hz、1フレームの上限は2ステップ、描画側で補間する。
  - 30〜60fpsでは、フレームによって0〜2回ステップして実時間に追いつく。30fpsを下回ると`dt`の上限（1/30秒）でゲーム全体がゆっくりになる。
- クエリはconst。ステップ中以外はジョブから同時に呼べる。
- 本体の種類はStatic / Kinematic / Dynamic / Trigger。剛体の力学とラグドールも使う。
- ラグドールの本体は死亡時（`GoLimp`）に初めて作る。生きている間にアニメで本体を動かすこと（`DriveFromAnimation`）はしない（→ 10）。
- キャラクターは`CharacterMover`（CharacterVirtualを包む）で動かす。
- 攻撃の当たり判定は、アニメのHit窓の間だけ、武器のカプセルを前のフレームから今のフレームまで掃引して行う（→ 10）。

## 10. アニメーション（12ページ目）

詳細は`docs/design/plans/animation.md`。

- エンジンは再生機（`Animator`）までを担当する。どのアニメを流すかは、ゲームのCharacterStateMachineが`Play()`で指示する。アニメーション専用のステートマシンは作らない（状態が二重になるため）。
- 1フレームの処理を2段に分ける。
  - `Advance`（4の後、全員が毎フレーム）：時間・イベント・ルートモーションを確定する。
  - `Evaluate`（8の後）：姿勢・IK・スキン行列を計算する。距離と、ビューに映っているかでLODを決めて間引く。
  - 間引いてもイベントとルートモーションは変わらない。Hit窓が開いている間はFullで計算する。
- `UpdateGroup::PostAnimation`（Main）を新設した。攻撃判定とGoLimpはここで行う。
- イベントはクリップごとの`.anim.json`に書く。瞬間（足音など）と時間窓（Hit / Cancelなど）の2種類。
  - 時間窓は各レイヤーの最新の再生だけで判定する。キャンセルしてフェードアウト中の攻撃の判定は残らない。
  - レイヤーは独立している。別レイヤーの攻撃は、ゲームが`StopLayer`で止める。
- ブレンドはクロスフェード、ブレンドスペース（1D / 2D）、骨マスク付きレイヤー（狼は首・頭、人型は上半身）。
- サンプリング・ブレンド・2ボーンIKの中身はozz-animationを使う。ozzの型はAnimationモジュールの外へ出さない。
- IKはFullのときだけ。接地 → 体の傾き → 脚の2ボーンIK → 頭の注視。
- 素材はFBXをBlenderでglTFに書き出し、変換ツールが読む（モデルはcgltf、骨格とアニメはozzのgltf2ozz）。
- 狼の素材（`SK_Wolf_Realistic.fbx`）に合わせて決めたこと：ルートモーションは`Root`骨のトラックを使う。移動は速度の1Dブレンド。横歩きがないので、ロックオン中も体は進行方向へ向け、頭を注視IKで敵に向ける。

## 11. サウンドとエフェクト（13ページ目）

詳細は`docs/design/plans/sound-effects.md`。

- 演出の束（Cue）を中心に置く。ゲームとアニメのイベントは`CueSystem::Play(名前, 場所・誰が・誰に)`を呼ぶだけ。音・エフェクト・ヒットストップ・揺れ・振動・画面演出は`cues/*.json`に書く。
  - アニメのイベントはanimsetの`eventCues`でCueに対応づける。
  - CueはPostAnimationの直後（8'''）にMainでまとめて配る。攻撃が当たったフレームのうちに音と火花が出る。
  - エンジンはゲームの型を知らないので、`IGameObject::GetAnimator()`でAnimatorを取り出す。骨の位置・接地の材質・ヒットストップはAnimator経由。
- サウンドはXAudio2で自作する。XAudio2に触るのはMainの`AudioSystem::Update`（10'）だけ。
  - 3D音響（X3DAudio）、BGMのクロスフェード、地面の材質で変わる足音、遮蔽（レイキャスト）、残響（レベルのReverbZone）。
  - SEは全部MS-ADPCM 48kHzにそろえ、ボイスを使い回す。BGMはADPCMのストリーミング。
- エフェクトは**記録方式**：Unity / Unrealで再生したエフェクトの全粒子を毎フレーム記録し、エンジンは再生するだけ（試作の結果：`docs/design/pilots/effects-pilot-result.md`、データ形式：`docs/design/plans/effect-recorder.md`）。Effekseerは使わない。
  - 再生に触るのはRenderThreadだけで、MainはRenderFrameにコマンドを書く。描画はEffectPass。
  - 骨への追従は再生位置・向きの変更、色や大きさは全体の倍率として扱う（記録の中身は変えない）。
- 画面全体の演出はポストエフェクト（`ScreenEffectController`）。
- 地面の材質は物理の`SurfaceId`。足音はキャラクターの接地情報（`CharacterMover::GetGroundSurface`）から取る。

## 未決・次に決めること

- 狼のアニメの追加制作（横歩き・後ろへの回避・連続攻撃・強攻撃）
- エフェクト：ループのつなぎ目、歪み（Shockwave）の再現、Xboxでの描画コスト
- セーブ
- 画面分割を入れる場合の、パッドの割り当てとHUDの配置
