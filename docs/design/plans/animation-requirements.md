# アニメーション設計の要件

新エンジン(`README.md` / `ClassDiagram.drawio`の既存11ページ)に、アニメーションの設計を追加する。
既存設計(GameObject・更新順・JobSystem・リソース・物理・描画)と矛盾しないこと。

## 前提(既存設計より)

- ソウルライク。対象はXbox One S(Jaguar 1.75GHz、Worker 4本、RAM 5GB)
- ECSは使わずIGameObject継承。部品(ModelRender / PhysicsBody / CharacterMover)はGameObjectが「持つだけ」
- 更新順はUpdateGroup(PreUpdate / Gameplay / ParallelGameplay / PostPhysics / Camera / Late)。ParallelGameplayは自分だけ書き換え可
- 物理はJolt、60Hz固定ステップ。クエリはStep中以外ならジョブから同時に呼べる
- 描画はRenderScene(保持・ダブルバッファ)。スキニングはコンピュートで1フレーム1回
- リソースはResourceManager::Load<T>、依存が全部Readyで親もReady。SkeletonResource / AnimationClipResourceの名前だけ既にある
- CharacterはCharacterCommand(意図)だけを読むステートマシン(CharacterStateMachine)を持つ。RagdollBody(DriveFromAnimation / GoLimp / WriteToSkeleton)、WeaponHitDetector(攻撃判定中のフレームだけ前→今フレームのカプセル掃引)がすでに設計済み

## ユーザーが決めたこと

- 主人公は狼(四足)。敵には人型もいる
- ルートモーション: 攻撃・回避・被弾など一部のアクションだけ。歩き・走りはコードで速度を決める
- ブレンド: クロスフェード、移動のブレンドスペース(ロックオン中の全方向移動を含む)、骨マスク付きレイヤー(狼は首・頭、人型は上半身)
- IK: 4本足の接地、坂での体の傾き、頭の注視(ロックオン対象)
- 素材: FBXをコンバーターで独自バイナリに変換。攻撃判定ON/OFFなどのイベントはクリップごとのJSONに書く
- 同時に動くスキンメッシュは最大50体程度

## 成功条件

- 攻撃判定・無敵・キャンセル受付などの「時間窓」をアニメーションに合わせて正しく開閉でき、キャンセル時に残らない
- 50体でもCPU予算内(目安: アニメーション全体でWorker合計2ms以内)。遠い/見えないキャラクターを間引いても、ゲームの結果(イベント・ルートモーション)は変わらない
- 並列グループ(ParallelGameplay)の敵からも安全に使える
- 既存のラグドール・攻撃判定・スキニングの設計につながる

## やらないこと

- アニメーショングラフのエディタ・データ駆動のステートマシン
- リターゲット(骨格の違うキャラクター間でクリップを共有する)
- ラグドールから起き上がりへのブレンド
- フェイシャル、布・髪の物理
