# サウンドとエフェクトの設計の要件

新エンジン(`README.md` / `ClassDiagram.drawio` の既存 12 ページ)に、サウンドとエフェクトの設計を追加する。
既存設計(更新順・JobSystem・スレッド構成・リソース・描画・入力・カメラ・物理・アニメーション)と矛盾しないこと。

## 前提(既存設計より)

- ソウルライク。対象は Xbox One S の UWP Dev Mode(Jaguar、専用スレッドは Main / Render / IO だけ、Worker 4 本、RAM 5GB)
- 共有コア(Core4-5)は低優先の Worker と IOThread が使う
- 描画は RenderThread がコマンドを実行する(ゲームは RenderContext に触らない)。パイプラインは IRenderPass の並び(Shadow / GBuffer / Lighting / Forward / PostEffect / UI)。ビューは複数(メイン・3D プレビュー・将来の画面分割)
- アニメーション(12 ページ目):
  - Animator の瞬間イベント(`Footstep` など、params 付き)は Advance(4')で確定する
  - 骨の位置は PostAnimation(8''、Main)以降で今フレームの値になる
  - ヒットストップは `Animator::SetTimeScale`
  - WeaponHitDetector は PostAnimation で当たりを検出する
- 既存 API:
  - 振動は `InputManager::SetVibration(low, high, seconds)`(どこからでも予約)
  - カメラの揺れは `CameraManager::AddModifier(slot, ShakeModifier)`
  - 物理のクエリ結果は `HitResult`(position / normal / distance / owner / layer / body)。Step 中以外はジョブからも呼べる
- リソースは `ResourceManager::Load<T>`。`SoundResource` の名前だけ既にある
- ローカルマルチの継ぎ目として `LocalPlayer` がある(今は 1 人)

## ユーザーが決めたこと

- **サウンドは XAudio2 で自作**する(ミドルウェアは使わない)
- サウンドの機能: 3D 音響(距離減衰・定位)、BGM の切り替え(**クロスフェード**)、地面の材質で足音を変える、残響と遮蔽
- (試作の結果で変更)**エフェクトは記録方式**:Unity / Unreal で再生したエフェクトを記録してエンジンで再生する(`docs/design/pilots/effects-pilot-result.md`)。当初は Effekseer(1.8 系)の予定だった。
- エフェクトの使い道: ヒット時の火花・血、骨への追従(口元の息・爪の軌跡など)、環境(焔・霧など、長く残る)、画面全体の演出
- **画面全体の演出はポストエフェクト**で作る(エフェクトでは作らない)
- アニメのイベントから音・エフェクトを出す対応は **JSON の対応表**に書く(コードを触らず調整する)
- **演出の束**: 「音・エフェクト・ヒットストップ・画面の揺れ・振動・画面演出」を 1 つの名前で JSON に定義し、ゲームは名前で呼ぶだけにする

## 成功条件

- 攻撃が当たったときの手ごたえ(音・火花・ヒットストップ・揺れ・振動)を、JSON の編集だけで調整できる
- 足音が地面の材質で変わる
- XAudio2 とエフェクトの再生に触るスレッドが決まっていて、ジョブや ParallelGameplay から音・エフェクトを要求しても安全

## 設計の方針

- 設計に書くのは後から変えにくいもの(スレッドの持ち主、API の境界、データ形式、既存設計との矛盾)だけ。端のケースや性能の細部は実装中に問題が出てから直す

## やらないこと

- 音声ミドルウェア(FMOD / Wwise)、HRTF
- BGM の小節同期・パートの重ね合わせ
- 台詞・口パク
- 画面分割時の複数リスナー(構造は LocalPlayer ごとにするが、今は 1 人分)
- エフェクトエディタの自作、Unity / Unreal のエフェクト変換
- デカール(足跡・血痕を地面に貼る)
