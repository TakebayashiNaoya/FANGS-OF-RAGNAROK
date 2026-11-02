# サウンドとエフェクトの設計の要件

新エンジン(`README.md` / `ClassDiagram.drawio`の既存12ページ)に、サウンドとエフェクトの設計を追加する。
既存設計(更新順・JobSystem・スレッド構成・リソース・描画・入力・カメラ・物理・アニメーション)と矛盾しないこと。

## 前提(既存設計より)

- ソウルライク。対象はXbox One SのUWP Dev Mode(Jaguar、専用スレッドはMain / Render / IOだけ、Worker 4本、RAM 5GB)
- 共有コア(Core4-5)は低優先のWorkerとIOThreadが使う
- 描画はRenderThreadがコマンドを実行する(ゲームはRenderContextに触らない)。パイプラインはIRenderPassの並び(Shadow / GBuffer / Lighting / Forward / PostEffect / UI)。ビューは複数(メイン・3Dプレビュー・将来の画面分割)
- アニメーション(12ページ目):
  - Animatorの瞬間イベント(`Footstep`など、params付き)はAdvance(4')で確定する
  - 骨の位置はPostAnimation(8''、Main)以降で今フレームの値になる
  - ヒットストップは`Animator::SetTimeScale`
  - WeaponHitDetectorはPostAnimationで当たりを検出する
- 既存API:
  - 振動は`InputManager::SetVibration(low, high, seconds)`(どこからでも予約)
  - カメラの揺れは`CameraManager::AddModifier(slot, ShakeModifier)`
  - 物理のクエリ結果は`HitResult`(position / normal / distance / owner / layer / body)。Step中以外はジョブからも呼べる
- リソースは`ResourceManager::Load<T>`。`SoundResource`の名前だけ既にある
- ローカルマルチの継ぎ目として`LocalPlayer`がある(今は1人)

## ユーザーが決めたこと

- **サウンドはXAudio2で自作**する(ミドルウェアは使わない)
- サウンドの機能: 3D音響(距離減衰・定位)、BGMの切り替え(**クロスフェード**)、地面の材質で足音を変える、残響と遮蔽
- (試作の結果で変更)**エフェクトは記録方式**:Unity / Unrealで再生したエフェクトを記録してエンジンで再生する(`docs/design/pilots/effects-pilot-result.md`)。当初はEffekseer(1.8系)の予定だった。
- エフェクトの使い道: ヒット時の火花・血、骨への追従(口元の息・爪の軌跡など)、環境(焔・霧など、長く残る)、画面全体の演出
- **画面全体の演出はポストエフェクト**で作る(エフェクトでは作らない)
- アニメのイベントから音・エフェクトを出す対応は**JSONの対応表**に書く(コードを触らず調整する)
- **演出の束**: 「音・エフェクト・ヒットストップ・画面の揺れ・振動・画面演出」を1つの名前でJSONに定義し、ゲームは名前で呼ぶだけにする

## 成功条件

- 攻撃が当たったときの手ごたえ(音・火花・ヒットストップ・揺れ・振動)を、JSONの編集だけで調整できる
- 足音が地面の材質で変わる
- XAudio2とエフェクトの再生に触るスレッドが決まっていて、ジョブやParallelGameplayから音・エフェクトを要求しても安全

## 設計の方針

- 設計に書くのは後から変えにくいもの(スレッドの持ち主、APIの境界、データ形式、既存設計との矛盾)だけ。端のケースや性能の細部は実装中に問題が出てから直す

## やらないこと

- 音声ミドルウェア(FMOD / Wwise)、HRTF
- BGMの小節同期・パートの重ね合わせ
- 台詞・口パク
- 画面分割時の複数リスナー(構造はLocalPlayerごとにするが、今は1人分)
- エフェクトエディタの自作、Unity / Unrealのエフェクト変換
- デカール(足跡・血痕を地面に貼る)
