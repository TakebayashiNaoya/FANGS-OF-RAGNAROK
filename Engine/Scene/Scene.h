/**
 * @file Scene.h
 * @brief 実行中にオブジェクトを作って壊せる入れ物。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Scene/GameObjectHandle.h"
#include <cstdint>


namespace fang
{
	class IAllocator;

	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetSceneModuleName();

	/**
	 * @brief Scene の生成条件。
	 * @details 上限は起動時に決め切る。実行中のヒープ確保を 0 にするため。
	 *          MeshRenderer::MAX_ITEM_COUNT や CollisionWorldDesc::maxColliderCount とは別の値
	 *          （描ける数・判定できる数と、持てる数は別）。
	 */
	struct SceneDesc
	{
		uint32_t maxObjectCount = 512; /**< 同時に存在できるオブジェクトの数。 */
	};

	/**
	 * @brief 実行中に生成・破棄できるオブジェクトの入れ物。
	 * @details オブジェクト 1 つが必ず持つのは Transform だけで、あとは足したコンポーネント次第
	 *          （階層は Scene 4、コンポーネントは Scene 5 で足す）。生成は即座に有効になり、破棄は
	 *          Update の中で反映されるまで遅延する。
	 * @threading Initialize / Shutdown はメインスレッドのみ。生成・破棄・Update は更新ジョブ（1 本）だけ。
	 */
	class Scene
	{
	public:
		FANG_NON_COPYABLE(Scene);
		FANG_NON_MOVABLE(Scene);

		Scene() = default;
		~Scene();

		/** @brief 今生きているオブジェクトの数。テストと監視用。 */
		[[nodiscard]] uint32_t GetActiveObjectCount() const { return m_maxObjectCount - m_freeIndexCount; }


	public:
		/**
		 * @brief 席とその管理データを確保し切る。
		 * @param allocator Shutdown まで生きていること。ロード時のヒープを想定している。
		 * @param desc      上限。0 なら失敗する。
		 * @return 確保できなければ false。そのときは何も抱えていない。
		 */
		[[nodiscard]] bool Initialize(IAllocator& allocator, const SceneDesc& desc);

		/** @brief 入れ物を返す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief オブジェクトを 1 個作る。
		 * @return 上限に達していたら無効なハンドル（FANG_LOG_WARNING を 1 回出す）。
		 * @details 作った時点でそのフレームの配列組み立てに乗る。
		 */
		[[nodiscard]] GameObjectHandle CreateObject();

		/**
		 * @brief オブジェクトを破棄する。
		 * @details 実際に席が空くのは次の Update の破棄反映。無効なハンドルや二重の呼び出しは何もしない。
		 */
		void DestroyObject(GameObjectHandle handle);

		/** @brief handle が今も生きているオブジェクトを指しているか。 */
		[[nodiscard]] bool IsValid(GameObjectHandle handle) const;

		/**
		 * @brief 1 フレームぶんの更新を進める。
		 * @details 破棄の反映のあと、根から順にワールド行列を作り直す。コンポーネントの更新（Scene 5）は
		 *          後続のタスクで積む。
		 */
		void Update(float deltaTimeSeconds);

		/**
		 * @brief ローカル行列を直接書く。
		 * @return 無効なハンドルなら false（何もしない）。
		 */
		[[nodiscard]] bool SetLocalMatrix(GameObjectHandle handle, const Matrix4x4& localMatrix);

		/**
		 * @brief 位置と Y 軸回りの向きだけでローカル行列を組み立てて書く。
		 * @details 置き物のように位置と向きだけで配置したいものに使う。
		 * @return 無効なハンドルなら false（何もしない）。
		 */
		[[nodiscard]] bool SetLocalTransform(GameObjectHandle handle, const Vector3& position, float rotationYRadians);

		/** @brief ローカル行列を読む。無効なハンドルなら単位行列。 */
		[[nodiscard]] Matrix4x4 GetLocalMatrix(GameObjectHandle handle) const;

		/** @brief 直近の Update が作ったワールド行列を読む。無効なハンドルなら単位行列。 */
		[[nodiscard]] Matrix4x4 GetWorldMatrix(GameObjectHandle handle) const;

		/**
		 * @brief 親を付け替える。
		 * @param parent 無効なハンドルを渡すと親なし（ルート）に戻す。
		 * @return handle が無効、parent が（無効ハンドル以外で）生きていない、または parent が handle の
		 *         子孫（輪ができる）なら false（何もしない）。
		 */
		[[nodiscard]] bool SetParent(GameObjectHandle handle, GameObjectHandle parent);

		/** @brief 親を読む。無効なハンドル、または親を持たなければ無効なハンドル。 */
		[[nodiscard]] GameObjectHandle GetParent(GameObjectHandle handle) const;


	private:
		/** @brief index を今の親の子リストから外す。親を持たなければ何もしない。 */
		void RemoveFromParentChildList(uint32_t index);


		IAllocator* m_allocator      = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */
		uint32_t    m_maxObjectCount = 0;

		uint32_t* m_generations = nullptr; /**< スロットごとの世代。破棄のたびに 1 進む。 */
		bool*     m_isActive    = nullptr; /**< スロットが今オブジェクトを持っているか。 */

		/** @brief 破棄の予約。Update の破棄反映まで true のまま残る。 */
		bool* m_pendingDestroy = nullptr;

		/** @brief 空いているスロット番号のスタック。末尾から積み、末尾から配る。 */
		uint32_t* m_freeIndices    = nullptr;
		uint32_t  m_freeIndexCount = 0;

		/** @brief 破棄を予約されたスロット番号。Update の破棄反映で走査する分だけに絞るための列。 */
		uint32_t* m_pendingDestroyIndices = nullptr;
		uint32_t  m_pendingDestroyCount   = 0;

		Matrix4x4* m_localMatrices = nullptr; /**< Transform の実体。ワールド行列は毎フレーム作り直す。 */
		Matrix4x4* m_worldMatrices = nullptr; /**< 直近の Update が作った、根からの積算。 */

		/** @brief 親のスロット番号。GameObjectHandle::INVALID_INDEX ならルート。 */
		uint32_t* m_parentIndices = nullptr;

		/** @brief 子リストの先頭。単方向リストで、兄弟は m_nextSiblingIndices をたどる。 */
		uint32_t* m_firstChildIndices = nullptr;

		uint32_t* m_nextSiblingIndices = nullptr;

		/**
		 * @brief 明示スタックの一時置き場（要素数は maxObjectCount）。再帰もヒープ確保もしない。
		 * @details ワールド行列の組み立て（Update）と、破棄の子孫収集（DestroyObject）が使い回す。
		 *          どちらも Scene を触れるのは更新ジョブ 1 本だけなので、同時に使われることはない。
		 */
		uint32_t* m_indexStack = nullptr;
	};
} // namespace fang
