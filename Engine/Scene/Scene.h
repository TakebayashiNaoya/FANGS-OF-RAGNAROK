/**
 * @file Scene.h
 * @brief 実行中にオブジェクトを作って壊せる入れ物。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Scene/Components.h"
#include "Scene/GameObjectHandle.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>


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
		uint32_t maxObjectCount   = 512; /**< 同時に存在できるオブジェクトの数。 */
		uint32_t maxBehaviorCount = 64;  /**< 同時に存在できる振る舞い（IComponent）の数。1 体が複数持ってよい。 */
	};

	/**
	 * @brief 実行中に生成・破棄できるオブジェクトの入れ物。
	 * @details オブジェクト 1 つが必ず持つのは Transform だけで、あとは足したコンポーネント次第。
	 *          生成は即座に有効になり、破棄は Update の中で反映されるまで遅延する。
	 * @threading Initialize / Shutdown はメインスレッドのみ。生成・破棄・Update は更新ジョブ（1 本）だけ。
	 */
	class Scene
	{
	public:
		FANG_NON_COPYABLE(Scene);
		FANG_NON_MOVABLE(Scene);

		/**
		 * @brief 振る舞い 1 個ぶんのブロックサイズ（バイト）。
		 * @details AddBehavior<T> がこれを超える型を static_assert で弾く。型ごとのプール登録が要らない
		 *          代わりに、全部の振る舞いがこの大きさの箱に収まる前提になる。
		 */
		static constexpr size_t BEHAVIOR_BLOCK_SIZE = 192;

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
		 * @details スキニング行列の破棄 ➡ 振る舞いの Update ➡ オブジェクトの破棄反映 ➡ ワールド行列の
		 *          組み立て、の順に進む。
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

		/**
		 * @brief MeshRendererComponent を足す。1 オブジェクトにつき 1 個まで。
		 * @return handle が無効、または既に持っていれば false（何もしない）。
		 */
		[[nodiscard]] bool AddMeshRendererComponent(GameObjectHandle handle, const MeshRendererComponent& component);

		/** @brief MeshRendererComponent を読み書きする。持っていなければ nullptr。 */
		[[nodiscard]] MeshRendererComponent* GetMeshRendererComponent(GameObjectHandle handle);

		/** @brief 読み取り専用版。 */
		[[nodiscard]] const MeshRendererComponent* GetMeshRendererComponent(GameObjectHandle handle) const;

		/**
		 * @brief ColliderComponent を足す。1 オブジェクトにつき 1 個まで。
		 * @return handle が無効、または既に持っていれば false（何もしない）。
		 */
		[[nodiscard]] bool AddColliderComponent(GameObjectHandle handle, const ColliderComponent& component);

		/** @brief ColliderComponent を読み書きする。持っていなければ nullptr。 */
		[[nodiscard]] ColliderComponent* GetColliderComponent(GameObjectHandle handle);

		/** @brief 読み取り専用版。 */
		[[nodiscard]] const ColliderComponent* GetColliderComponent(GameObjectHandle handle) const;

		/**
		 * @brief 振る舞い（IComponent）を 1 個足す。固定長ブロックのプールから配る。
		 * @tparam T IComponent を継承した型。BEHAVIOR_BLOCK_SIZE に収まること。
		 * @return handle が無効、または上限に達していれば nullptr（FANG_LOG_WARNING を 1 回出す）。
		 * @details 戻したポインタの寿命は Scene が持つ。呼び出し側は解放しない
		 *          （DestroyObject が反映されたときに Scene がデストラクタを呼ぶ）。
		 */
		template <typename T, typename... Args> [[nodiscard]] T* AddBehavior(GameObjectHandle handle, Args&&... args)
		{
			static_assert(std::is_base_of_v<IComponent, T>, "IComponent を継承していない");
			static_assert(sizeof(T) <= BEHAVIOR_BLOCK_SIZE, "振る舞いのサイズが BEHAVIOR_BLOCK_SIZE を超えている");
			static_assert(
				alignof(T) <= alignof(std::max_align_t),
				"振る舞いのアラインメントがブロックの前提を超えている"
			);

			uint32_t blockIndex = 0;
			void*    block      = AllocateBehaviorBlock(handle, &blockIndex);
			if (block == nullptr)
			{
				return nullptr;
			}

			T* behavior = ::new (block) T(std::forward<Args>(args)...);
			RegisterBehavior(handle, behavior, blockIndex);
			return behavior;
		}

		/**
		 * @brief このフレームだけのスキニング行列を預ける。
		 * @param matrices フレームメモリを指す span。このフレームの間だけ読まれる。
		 * @return handle が無効なら false（何もしない）。
		 * @details 書かなければ次の Update の入口で空になり、MeshRenderer がバインドポーズで描く。
		 */
		[[nodiscard]] bool SetSkinningMatrices(GameObjectHandle handle, std::span<const Matrix4x4> matrices);

		/** @brief SetSkinningMatrices で預けたもの。無効なハンドル、または未設定なら空の span。 */
		[[nodiscard]] std::span<const Matrix4x4> GetSkinningMatrices(GameObjectHandle handle) const;


	private:
		/** @brief index を今の親の子リストから外す。親を持たなければ何もしない。 */
		void RemoveFromParentChildList(uint32_t index);

		/**
		 * @brief ブロックプールから 1 個借りる。
		 * @param outBlockIndex 借りた番号。RegisterBehavior に渡す。
		 * @return handle が無効、または空きがなければ nullptr。
		 */
		[[nodiscard]] void* AllocateBehaviorBlock(GameObjectHandle handle, uint32_t* outBlockIndex);

		/** @brief 構築済みの振る舞いを記録に加える。AddBehavior の placement new の直後に呼ぶ。 */
		void RegisterBehavior(GameObjectHandle handle, IComponent* instance, uint32_t blockIndex);

		/** @brief index が持つ振る舞いを全部デストラクトし、ブロックを返す。 */
		void RemoveBehaviorsOwnedBy(uint32_t index);

		/** @brief index が MeshRendererComponent を持っていれば、詰めた配列から取り除く。 */
		void RemoveMeshRendererComponentIfPresent(uint32_t index);

		/** @brief index が ColliderComponent を持っていれば、詰めた配列から取り除く。 */
		void RemoveColliderComponentIfPresent(uint32_t index);


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

		/** @brief このフレームだけ有効なスキニング行列。フレームメモリを指すので、Update の入口で毎回捨てる。 */
		std::span<const Matrix4x4>* m_skinningMatricesSpans = nullptr;

		//------------------------------------------------------------------------
		// 汎用コンポーネント（詰めた配列）。オブジェクト番号 ➡ 詰めた配列の添字の対応表を持つ。
		//------------------------------------------------------------------------
		MeshRendererComponent* m_meshRendererComponents             = nullptr; /**< 詰めた配列。 */
		uint32_t*              m_meshRendererComponentOwners        = nullptr; /**< 添字 ➡ 持ち主のオブジェクト番号。 */
		uint32_t*              m_meshRendererComponentIndexByObject = nullptr; /**< オブジェクト番号 ➡ 添字。 */
		uint32_t               m_meshRendererComponentCount         = 0;

		ColliderComponent* m_colliderComponents             = nullptr;
		uint32_t*          m_colliderComponentOwners        = nullptr;
		uint32_t*          m_colliderComponentIndexByObject = nullptr;
		uint32_t           m_colliderComponentCount         = 0;

		//------------------------------------------------------------------------
		// 振る舞い（IComponent）。固定長ブロックのプールから配る。
		//------------------------------------------------------------------------
		/** @brief 振る舞い 1 個の記録。ownerIndex は破棄反映での一括デストラクトに使う。 */
		struct BehaviorRecord
		{
			uint32_t    ownerIndex = GameObjectHandle::INVALID_INDEX;
			IComponent* instance   = nullptr;
			uint32_t    blockIndex = 0;
		};

		std::byte* m_behaviorBlocks = nullptr; /**< BEHAVIOR_BLOCK_SIZE * maxBehaviorCount バイトの生の置き場。 */

		uint32_t* m_freeBehaviorBlockIndices = nullptr;
		uint32_t  m_freeBehaviorBlockCount   = 0;

		BehaviorRecord* m_behaviorRecords     = nullptr; /**< 詰めた配列。壊れたらスワップして詰め直す。 */
		uint32_t        m_behaviorRecordCount = 0;
		uint32_t        m_maxBehaviorCount    = 0;
	};
} // namespace fang
