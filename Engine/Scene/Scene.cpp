/**
 * @file Scene.cpp
 * @brief Scene の入れ物（席・世代付きハンドル・生成と破棄・Transform 階層）。
 */
#include "Pch.h"
#include "Scene/Scene.h"
#include "Collision/CollisionShapes.h"
#include "Collision/CollisionWorld.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Scene/SceneLog.h"


FANG_DEFINE_LOG_CATEGORY(Scene);


namespace fang
{
	namespace
	{
		/**
		 * @brief モデル空間の箱と world 行列から、箱を包む外接球を作る。
		 * @details MakeOBBFromAabb / MakeCapsuleFromAabb と対になる、Sphere 用の導出。半径は対角の半分
		 *          （最も遠い頂点までの距離）なので、回転がどの向きでも箱をすべて包む。
		 */
		[[nodiscard]] Sphere MakeSphereFromAabb(const Aabb& localBounds, const Matrix4x4& world)
		{
			const Vector3 localCenter = (localBounds.min + localBounds.max) * 0.5f;
			const Vector3 halfExtents = (localBounds.max - localBounds.min) * 0.5f;

			return Sphere{
				.center = TransformPoint(localCenter, world),
				.radius = Length(halfExtents),
			};
		}
	} // namespace


	const char* GetSceneModuleName()
	{
		return "Scene";
	}


	Scene::~Scene()
	{
		Shutdown();
	}


	bool Scene::Initialize(IAllocator& allocator, const SceneDesc& desc)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (desc.maxObjectCount == 0)
		{
			FANG_LOG_ERROR(Scene, "Scene の maxObjectCount が 0");
			return false;
		}

		m_generations           = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_isActive              = NewArray<bool>(allocator, desc.maxObjectCount);
		m_pendingDestroy        = NewArray<bool>(allocator, desc.maxObjectCount);
		m_freeIndices           = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_pendingDestroyIndices = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_localMatrices         = NewArray<Matrix4x4>(allocator, desc.maxObjectCount);
		m_worldMatrices         = NewArray<Matrix4x4>(allocator, desc.maxObjectCount);
		m_parentIndices         = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_firstChildIndices     = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_nextSiblingIndices    = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_indexStack            = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_skinningMatricesSpans = NewArray<std::span<const Matrix4x4>>(allocator, desc.maxObjectCount);

		m_meshRendererComponents             = NewArray<MeshRendererComponent>(allocator, desc.maxObjectCount);
		m_meshRendererComponentOwners        = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_meshRendererComponentIndexByObject = NewArray<uint32_t>(allocator, desc.maxObjectCount);

		m_colliderComponents             = NewArray<ColliderComponent>(allocator, desc.maxObjectCount);
		m_colliderComponentOwners        = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_colliderComponentIndexByObject = NewArray<uint32_t>(allocator, desc.maxObjectCount);

		const bool hasCapacityForBehaviors = desc.maxBehaviorCount > 0;
		m_behaviorBlocks =
			hasCapacityForBehaviors
				? static_cast<std::byte*>(allocator.Allocate(BEHAVIOR_BLOCK_SIZE * desc.maxBehaviorCount))
				: nullptr;
		m_freeBehaviorBlockIndices = NewArray<uint32_t>(allocator, desc.maxBehaviorCount);
		m_behaviorRecords          = NewArray<BehaviorRecord>(allocator, desc.maxBehaviorCount);

		const bool hasAllBuffers =
			m_generations != nullptr && m_isActive != nullptr && m_pendingDestroy != nullptr &&
			m_freeIndices != nullptr && m_pendingDestroyIndices != nullptr && m_localMatrices != nullptr &&
			m_worldMatrices != nullptr && m_parentIndices != nullptr && m_firstChildIndices != nullptr &&
			m_nextSiblingIndices != nullptr && m_indexStack != nullptr && m_skinningMatricesSpans != nullptr &&
			m_meshRendererComponents != nullptr && m_meshRendererComponentOwners != nullptr &&
			m_meshRendererComponentIndexByObject != nullptr && m_colliderComponents != nullptr &&
			m_colliderComponentOwners != nullptr && m_colliderComponentIndexByObject != nullptr &&
			(!hasCapacityForBehaviors || m_behaviorBlocks != nullptr) && m_freeBehaviorBlockIndices != nullptr &&
			m_behaviorRecords != nullptr;
		if (!hasAllBuffers)
		{
			FANG_LOG_ERROR(Scene, "Scene の入れ物を確保できなかった");

			DeleteArray(allocator, m_behaviorRecords, desc.maxBehaviorCount);
			DeleteArray(allocator, m_freeBehaviorBlockIndices, desc.maxBehaviorCount);
			allocator.Deallocate(m_behaviorBlocks);
			DeleteArray(allocator, m_colliderComponentIndexByObject, desc.maxObjectCount);
			DeleteArray(allocator, m_colliderComponentOwners, desc.maxObjectCount);
			DeleteArray(allocator, m_colliderComponents, desc.maxObjectCount);
			DeleteArray(allocator, m_meshRendererComponentIndexByObject, desc.maxObjectCount);
			DeleteArray(allocator, m_meshRendererComponentOwners, desc.maxObjectCount);
			DeleteArray(allocator, m_meshRendererComponents, desc.maxObjectCount);
			DeleteArray(allocator, m_skinningMatricesSpans, desc.maxObjectCount);
			DeleteArray(allocator, m_indexStack, desc.maxObjectCount);
			DeleteArray(allocator, m_nextSiblingIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_firstChildIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_parentIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_worldMatrices, desc.maxObjectCount);
			DeleteArray(allocator, m_localMatrices, desc.maxObjectCount);
			DeleteArray(allocator, m_pendingDestroyIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_freeIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_pendingDestroy, desc.maxObjectCount);
			DeleteArray(allocator, m_isActive, desc.maxObjectCount);
			DeleteArray(allocator, m_generations, desc.maxObjectCount);

			m_behaviorRecords                    = nullptr;
			m_freeBehaviorBlockIndices           = nullptr;
			m_behaviorBlocks                     = nullptr;
			m_colliderComponentIndexByObject     = nullptr;
			m_colliderComponentOwners            = nullptr;
			m_colliderComponents                 = nullptr;
			m_meshRendererComponentIndexByObject = nullptr;
			m_meshRendererComponentOwners        = nullptr;
			m_meshRendererComponents             = nullptr;
			m_skinningMatricesSpans              = nullptr;
			m_indexStack                         = nullptr;
			m_nextSiblingIndices                 = nullptr;
			m_firstChildIndices                  = nullptr;
			m_parentIndices                      = nullptr;
			m_worldMatrices                      = nullptr;
			m_localMatrices                      = nullptr;
			m_pendingDestroyIndices              = nullptr;
			m_freeIndices                        = nullptr;
			m_pendingDestroy                     = nullptr;
			m_isActive                           = nullptr;
			m_generations                        = nullptr;
			return false;
		}

		m_allocator        = &allocator;
		m_maxObjectCount   = desc.maxObjectCount;
		m_maxBehaviorCount = desc.maxBehaviorCount;

		// 空き番号は末尾から積む ➡ 先頭（0 番）から順に配られる。
		// 「無し」を表す番号（INVALID_INDEX）で埋めておく列がいくつかある。0 は有効な添字なので、
		// NewArray の既定値（0 埋め）のままにはできない。
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			m_freeIndices[index]                        = m_maxObjectCount - 1 - index;
			m_parentIndices[index]                      = GameObjectHandle::INVALID_INDEX;
			m_firstChildIndices[index]                  = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[index]                 = GameObjectHandle::INVALID_INDEX;
			m_meshRendererComponentIndexByObject[index] = GameObjectHandle::INVALID_INDEX;
			m_colliderComponentIndexByObject[index]     = GameObjectHandle::INVALID_INDEX;
		}
		m_freeIndexCount = m_maxObjectCount;

		// 振る舞いのブロックも末尾から積む。
		for (uint32_t blockIndex = 0; blockIndex < m_maxBehaviorCount; ++blockIndex)
		{
			m_freeBehaviorBlockIndices[blockIndex] = m_maxBehaviorCount - 1 - blockIndex;
		}
		m_freeBehaviorBlockCount = m_maxBehaviorCount;

		FANG_LOG_INFO(
			Scene,
			"Scene を作った: オブジェクト上限 {} / 振る舞い上限 {}",
			m_maxObjectCount,
			m_maxBehaviorCount
		);

		return true;
	}


	void Scene::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		// 生き残っている振る舞いをデストラクトしてから、器そのものを返す。
		for (uint32_t recordIndex = 0; recordIndex < m_behaviorRecordCount; ++recordIndex)
		{
			m_behaviorRecords[recordIndex].instance->~IComponent();
		}

		DeleteArray(*m_allocator, m_behaviorRecords, m_maxBehaviorCount);
		DeleteArray(*m_allocator, m_freeBehaviorBlockIndices, m_maxBehaviorCount);
		m_allocator->Deallocate(m_behaviorBlocks);
		DeleteArray(*m_allocator, m_colliderComponentIndexByObject, m_maxObjectCount);
		DeleteArray(*m_allocator, m_colliderComponentOwners, m_maxObjectCount);
		DeleteArray(*m_allocator, m_colliderComponents, m_maxObjectCount);
		DeleteArray(*m_allocator, m_meshRendererComponentIndexByObject, m_maxObjectCount);
		DeleteArray(*m_allocator, m_meshRendererComponentOwners, m_maxObjectCount);
		DeleteArray(*m_allocator, m_meshRendererComponents, m_maxObjectCount);
		DeleteArray(*m_allocator, m_skinningMatricesSpans, m_maxObjectCount);
		DeleteArray(*m_allocator, m_indexStack, m_maxObjectCount);
		DeleteArray(*m_allocator, m_nextSiblingIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_firstChildIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_parentIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_worldMatrices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_localMatrices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_pendingDestroyIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_freeIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_pendingDestroy, m_maxObjectCount);
		DeleteArray(*m_allocator, m_isActive, m_maxObjectCount);
		DeleteArray(*m_allocator, m_generations, m_maxObjectCount);

		m_behaviorRecords                    = nullptr;
		m_freeBehaviorBlockIndices           = nullptr;
		m_behaviorBlocks                     = nullptr;
		m_colliderComponentIndexByObject     = nullptr;
		m_colliderComponentOwners            = nullptr;
		m_colliderComponents                 = nullptr;
		m_meshRendererComponentIndexByObject = nullptr;
		m_meshRendererComponentOwners        = nullptr;
		m_meshRendererComponents             = nullptr;
		m_skinningMatricesSpans              = nullptr;
		m_indexStack                         = nullptr;
		m_nextSiblingIndices                 = nullptr;
		m_firstChildIndices                  = nullptr;
		m_parentIndices                      = nullptr;
		m_worldMatrices                      = nullptr;
		m_localMatrices                      = nullptr;
		m_pendingDestroyIndices              = nullptr;
		m_freeIndices                        = nullptr;
		m_pendingDestroy                     = nullptr;
		m_isActive                           = nullptr;
		m_generations                        = nullptr;

		m_maxObjectCount             = 0;
		m_freeIndexCount             = 0;
		m_pendingDestroyCount        = 0;
		m_meshRendererComponentCount = 0;
		m_colliderComponentCount     = 0;
		m_freeBehaviorBlockCount     = 0;
		m_behaviorRecordCount        = 0;
		m_maxBehaviorCount           = 0;
		m_allocator                  = nullptr;
	}


	GameObjectHandle Scene::CreateObject()
	{
		if (m_freeIndexCount == 0)
		{
			FANG_LOG_WARNING(Scene, "オブジェクトの上限（{}）に達したので作れなかった", m_maxObjectCount);
			return GameObjectHandle{};
		}

		const uint32_t index = m_freeIndices[--m_freeIndexCount];

		m_isActive[index]       = true;
		m_pendingDestroy[index] = false;

		return GameObjectHandle{ index, m_generations[index] };
	}


	void Scene::DestroyObject(GameObjectHandle handle)
	{
		if (!IsValid(handle) || m_pendingDestroy[handle.index])
		{
			return;
		}

		// 子を含めて丸ごと破棄予約する（明示スタック。再帰もヒープ確保もしない）。
		uint32_t stackTop      = 0;
		m_indexStack[stackTop] = handle.index;
		++stackTop;

		while (stackTop > 0)
		{
			--stackTop;
			const uint32_t index = m_indexStack[stackTop];

			m_pendingDestroy[index]                        = true;
			m_pendingDestroyIndices[m_pendingDestroyCount] = index;
			++m_pendingDestroyCount;

			for (uint32_t child = m_firstChildIndices[index]; child != GameObjectHandle::INVALID_INDEX;
				 child          = m_nextSiblingIndices[child])
			{
				m_indexStack[stackTop] = child;
				++stackTop;
			}
		}
	}


	bool Scene::IsValid(GameObjectHandle handle) const
	{
		return handle.IsValid() && handle.index < m_maxObjectCount && m_isActive[handle.index] &&
			   m_generations[handle.index] == handle.generation;
	}


	void Scene::Update(float deltaTimeSeconds)
	{
		// 1. 前フレームのスキニング行列の span を捨てる（フレームメモリを指しているため）。
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			m_skinningMatricesSpans[index] = std::span<const Matrix4x4>{};
		}

		// 2. 振る舞いを回す。回す本数は入口で固定し、破棄予約の立ったものは飛ばす
		//    ➡ 更新中に足したものは次の周から。壊したものはその周でもう回らない。
		const uint32_t behaviorCountAtEntry = m_behaviorRecordCount;
		for (uint32_t recordIndex = 0; recordIndex < behaviorCountAtEntry; ++recordIndex)
		{
			const BehaviorRecord& record = m_behaviorRecords[recordIndex];
			if (m_pendingDestroy[record.ownerIndex])
			{
				continue;
			}

			const GameObjectHandle ownerHandle{ record.ownerIndex, m_generations[record.ownerIndex] };
			record.instance->Update(deltaTimeSeconds, ownerHandle, *this);
		}

		// 3. 破棄の予約を反映する（子リストから外し、コンポーネントと振る舞いを畳み、世代を進める）。
		for (uint32_t pendingIndex = 0; pendingIndex < m_pendingDestroyCount; ++pendingIndex)
		{
			const uint32_t index = m_pendingDestroyIndices[pendingIndex];

			RemoveFromParentChildList(index);
			RemoveMeshRendererComponentIfPresent(index);
			RemoveColliderComponentIfPresent(index);
			RemoveBehaviorsOwnedBy(index);

			m_isActive[index]       = false;
			m_pendingDestroy[index] = false;
			++m_generations[index];

			m_parentIndices[index]      = GameObjectHandle::INVALID_INDEX;
			m_firstChildIndices[index]  = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[index] = GameObjectHandle::INVALID_INDEX;
			m_localMatrices[index]      = Matrix4x4{};

			m_freeIndices[m_freeIndexCount] = index;
			++m_freeIndexCount;
		}

		m_pendingDestroyCount = 0;

		// 4. 根から順にワールド行列を作る（明示スタック。再帰もヒープ確保もしない）。
		uint32_t stackTop = 0;
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			if (m_isActive[index] && m_parentIndices[index] == GameObjectHandle::INVALID_INDEX)
			{
				m_indexStack[stackTop] = index;
				++stackTop;
			}
		}

		while (stackTop > 0)
		{
			--stackTop;
			const uint32_t index       = m_indexStack[stackTop];
			const uint32_t parentIndex = m_parentIndices[index];

			m_worldMatrices[index] = (parentIndex == GameObjectHandle::INVALID_INDEX)
										 ? m_localMatrices[index]
										 : Multiply(m_localMatrices[index], m_worldMatrices[parentIndex]);

			for (uint32_t child = m_firstChildIndices[index]; child != GameObjectHandle::INVALID_INDEX;
				 child          = m_nextSiblingIndices[child])
			{
				m_indexStack[stackTop] = child;
				++stackTop;
			}
		}
	}


	bool Scene::SetLocalMatrix(GameObjectHandle handle, const Matrix4x4& localMatrix)
	{
		if (!IsValid(handle))
		{
			return false;
		}

		m_localMatrices[handle.index] = localMatrix;
		return true;
	}


	bool Scene::SetLocalTransform(GameObjectHandle handle, const Vector3& position, float rotationYRadians)
	{
		Matrix4x4 localMatrix = MakeRotationYMatrix(rotationYRadians);
		localMatrix.m[3][0]   = position.x;
		localMatrix.m[3][1]   = position.y;
		localMatrix.m[3][2]   = position.z;

		return SetLocalMatrix(handle, localMatrix);
	}


	Matrix4x4 Scene::GetLocalMatrix(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return Matrix4x4{};
		}

		return m_localMatrices[handle.index];
	}


	Matrix4x4 Scene::GetWorldMatrix(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return Matrix4x4{};
		}

		return m_worldMatrices[handle.index];
	}


	bool Scene::SetParent(GameObjectHandle handle, GameObjectHandle parent)
	{
		if (!IsValid(handle))
		{
			return false;
		}

		const bool hasNewParent = parent.IsValid();
		if (hasNewParent)
		{
			if (!IsValid(parent) || parent.index == handle.index)
			{
				return false;
			}

			// 先祖をたどって自分に出会ったら輪ができる。
			for (uint32_t ancestor = m_parentIndices[parent.index]; ancestor != GameObjectHandle::INVALID_INDEX;
				 ancestor          = m_parentIndices[ancestor])
			{
				if (ancestor == handle.index)
				{
					return false;
				}
			}
		}

		RemoveFromParentChildList(handle.index);

		if (hasNewParent)
		{
			m_parentIndices[handle.index]      = parent.index;
			m_nextSiblingIndices[handle.index] = m_firstChildIndices[parent.index];
			m_firstChildIndices[parent.index]  = handle.index;
		}
		else
		{
			m_parentIndices[handle.index]      = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[handle.index] = GameObjectHandle::INVALID_INDEX;
		}

		return true;
	}


	GameObjectHandle Scene::GetParent(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return GameObjectHandle{};
		}

		const uint32_t parentIndex = m_parentIndices[handle.index];
		if (parentIndex == GameObjectHandle::INVALID_INDEX)
		{
			return GameObjectHandle{};
		}

		return GameObjectHandle{ parentIndex, m_generations[parentIndex] };
	}


	void Scene::RemoveFromParentChildList(uint32_t index)
	{
		const uint32_t parentIndex = m_parentIndices[index];
		if (parentIndex == GameObjectHandle::INVALID_INDEX)
		{
			return;
		}

		uint32_t* link = &m_firstChildIndices[parentIndex];
		while (*link != GameObjectHandle::INVALID_INDEX)
		{
			if (*link == index)
			{
				*link = m_nextSiblingIndices[index];
				return;
			}

			link = &m_nextSiblingIndices[*link];
		}
	}


	bool Scene::AddMeshRendererComponent(GameObjectHandle handle, const MeshRendererComponent& component)
	{
		if (!IsValid(handle) || m_meshRendererComponentIndexByObject[handle.index] != GameObjectHandle::INVALID_INDEX)
		{
			return false;
		}

		const uint32_t denseIndex                          = m_meshRendererComponentCount;
		m_meshRendererComponents[denseIndex]               = component;
		m_meshRendererComponentOwners[denseIndex]          = handle.index;
		m_meshRendererComponentIndexByObject[handle.index] = denseIndex;
		++m_meshRendererComponentCount;

		return true;
	}


	MeshRendererComponent* Scene::GetMeshRendererComponent(GameObjectHandle handle)
	{
		if (!IsValid(handle))
		{
			return nullptr;
		}

		const uint32_t denseIndex = m_meshRendererComponentIndexByObject[handle.index];
		return (denseIndex == GameObjectHandle::INVALID_INDEX) ? nullptr : &m_meshRendererComponents[denseIndex];
	}


	const MeshRendererComponent* Scene::GetMeshRendererComponent(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return nullptr;
		}

		const uint32_t denseIndex = m_meshRendererComponentIndexByObject[handle.index];
		return (denseIndex == GameObjectHandle::INVALID_INDEX) ? nullptr : &m_meshRendererComponents[denseIndex];
	}


	bool Scene::AddColliderComponent(GameObjectHandle handle, const ColliderComponent& component)
	{
		if (!IsValid(handle) || m_colliderComponentIndexByObject[handle.index] != GameObjectHandle::INVALID_INDEX)
		{
			return false;
		}

		const uint32_t denseIndex                      = m_colliderComponentCount;
		m_colliderComponents[denseIndex]               = component;
		m_colliderComponentOwners[denseIndex]          = handle.index;
		m_colliderComponentIndexByObject[handle.index] = denseIndex;
		++m_colliderComponentCount;

		return true;
	}


	ColliderComponent* Scene::GetColliderComponent(GameObjectHandle handle)
	{
		if (!IsValid(handle))
		{
			return nullptr;
		}

		const uint32_t denseIndex = m_colliderComponentIndexByObject[handle.index];
		return (denseIndex == GameObjectHandle::INVALID_INDEX) ? nullptr : &m_colliderComponents[denseIndex];
	}


	const ColliderComponent* Scene::GetColliderComponent(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return nullptr;
		}

		const uint32_t denseIndex = m_colliderComponentIndexByObject[handle.index];
		return (denseIndex == GameObjectHandle::INVALID_INDEX) ? nullptr : &m_colliderComponents[denseIndex];
	}


	void* Scene::AllocateBehaviorBlock(GameObjectHandle handle, uint32_t* outBlockIndex)
	{
		if (!IsValid(handle))
		{
			return nullptr;
		}

		if (m_freeBehaviorBlockCount == 0)
		{
			FANG_LOG_WARNING(Scene, "振る舞いの上限（{}）に達したので作れなかった", m_maxBehaviorCount);
			return nullptr;
		}

		const uint32_t blockIndex = m_freeBehaviorBlockIndices[--m_freeBehaviorBlockCount];
		*outBlockIndex            = blockIndex;

		return m_behaviorBlocks + blockIndex * BEHAVIOR_BLOCK_SIZE;
	}


	void Scene::RegisterBehavior(GameObjectHandle handle, IComponent* instance, uint32_t blockIndex)
	{
		m_behaviorRecords[m_behaviorRecordCount] = BehaviorRecord{
			.ownerIndex = handle.index,
			.instance   = instance,
			.blockIndex = blockIndex,
		};
		++m_behaviorRecordCount;
	}


	void Scene::RemoveBehaviorsOwnedBy(uint32_t index)
	{
		for (uint32_t recordIndex = 0; recordIndex < m_behaviorRecordCount;)
		{
			BehaviorRecord& record = m_behaviorRecords[recordIndex];
			if (record.ownerIndex != index)
			{
				++recordIndex;
				continue;
			}

			record.instance->~IComponent();

			m_freeBehaviorBlockIndices[m_freeBehaviorBlockCount] = record.blockIndex;
			++m_freeBehaviorBlockCount;

			--m_behaviorRecordCount;
			record = m_behaviorRecords[m_behaviorRecordCount]; // スワップして詰める。recordIndex は進めない。
		}
	}


	void Scene::RemoveMeshRendererComponentIfPresent(uint32_t index)
	{
		const uint32_t denseIndex = m_meshRendererComponentIndexByObject[index];
		if (denseIndex == GameObjectHandle::INVALID_INDEX)
		{
			return;
		}

		const uint32_t lastIndex = m_meshRendererComponentCount - 1;

		m_meshRendererComponents[denseIndex]      = m_meshRendererComponents[lastIndex];
		m_meshRendererComponentOwners[denseIndex] = m_meshRendererComponentOwners[lastIndex];
		m_meshRendererComponentIndexByObject[m_meshRendererComponentOwners[denseIndex]] = denseIndex;

		m_meshRendererComponentIndexByObject[index] = GameObjectHandle::INVALID_INDEX;
		--m_meshRendererComponentCount;
	}


	void Scene::RemoveColliderComponentIfPresent(uint32_t index)
	{
		const uint32_t denseIndex = m_colliderComponentIndexByObject[index];
		if (denseIndex == GameObjectHandle::INVALID_INDEX)
		{
			return;
		}

		const uint32_t lastIndex = m_colliderComponentCount - 1;

		m_colliderComponents[denseIndex]                                        = m_colliderComponents[lastIndex];
		m_colliderComponentOwners[denseIndex]                                   = m_colliderComponentOwners[lastIndex];
		m_colliderComponentIndexByObject[m_colliderComponentOwners[denseIndex]] = denseIndex;

		m_colliderComponentIndexByObject[index] = GameObjectHandle::INVALID_INDEX;
		--m_colliderComponentCount;
	}


	bool Scene::SetSkinningMatrices(GameObjectHandle handle, std::span<const Matrix4x4> matrices)
	{
		if (!IsValid(handle))
		{
			return false;
		}

		m_skinningMatricesSpans[handle.index] = matrices;
		return true;
	}


	std::span<const Matrix4x4> Scene::GetSkinningMatrices(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return std::span<const Matrix4x4>{};
		}

		return m_skinningMatricesSpans[handle.index];
	}


	std::span<const RenderItem> Scene::BuildRenderItems(FrameAllocator& allocator) const
	{
		if (m_meshRendererComponentCount == 0)
		{
			return {};
		}

		void* memory = allocator.Allocate(sizeof(RenderItem) * m_meshRendererComponentCount, alignof(RenderItem));
		if (memory == nullptr)
		{
			FANG_LOG_ERROR(Scene, "RenderItem の確保に失敗した（{} 個ぶん）", m_meshRendererComponentCount);
			return {};
		}

		RenderItem* items        = static_cast<RenderItem*>(memory);
		uint32_t    writtenCount = 0;

		for (uint32_t denseIndex = 0; denseIndex < m_meshRendererComponentCount; ++denseIndex)
		{
			const MeshRendererComponent& component = m_meshRendererComponents[denseIndex];
			if (!component.isVisible)
			{
				continue;
			}

			const uint32_t   ownerIndex = m_meshRendererComponentOwners[denseIndex];
			const Matrix4x4& world      = m_worldMatrices[ownerIndex];

			::new (&items[writtenCount]) RenderItem{
				.mesh   = component.mesh,
				.world  = world,
				.bounds = component.localBounds.IsValid() ? TransformAabb(component.localBounds, world) : Aabb{},
				.skinningMatrices = m_skinningMatricesSpans[ownerIndex],
				.baseColor        = component.baseColor,
				.normalMap        = component.normalMap,
				.metallicFactor   = component.materialParams.metallicFactor,
				.roughnessFactor  = component.materialParams.roughnessFactor,
				.normalScale      = component.materialParams.normalScale,
				.castsShadow      = component.castsShadow,
			};
			++writtenCount;
		}

		return std::span<const RenderItem>(items, writtenCount);
	}


	std::span<const ColliderProxy> Scene::BuildColliderProxies(FrameAllocator& allocator) const
	{
		if (m_colliderComponentCount == 0)
		{
			return {};
		}

		void* memory = allocator.Allocate(sizeof(ColliderProxy) * m_colliderComponentCount, alignof(ColliderProxy));
		if (memory == nullptr)
		{
			FANG_LOG_ERROR(Scene, "ColliderProxy の確保に失敗した（{} 個ぶん）", m_colliderComponentCount);
			return {};
		}

		ColliderProxy* proxies      = static_cast<ColliderProxy*>(memory);
		uint32_t       writtenCount = 0;

		for (uint32_t denseIndex = 0; denseIndex < m_colliderComponentCount; ++denseIndex)
		{
			const ColliderComponent& component = m_colliderComponents[denseIndex];
			if (!component.isEnabled || !component.localBounds.IsValid())
			{
				continue;
			}

			const uint32_t   ownerIndex = m_colliderComponentOwners[denseIndex];
			const Matrix4x4& world      = m_worldMatrices[ownerIndex];

			ColliderShape shape;
			switch (component.shapeType)
			{
				case EnShapeType::Capsule:
					shape = MakeColliderShape(MakeCapsuleFromAabb(component.localBounds, world));
					break;

				case EnShapeType::OBB: shape = MakeColliderShape(MakeOBBFromAabb(component.localBounds, world)); break;

				case EnShapeType::Sphere:
					shape = MakeColliderShape(MakeSphereFromAabb(component.localBounds, world));
					break;
			}

			::new (&proxies[writtenCount]) ColliderProxy{
				.shape     = shape,
				.userIndex = ownerIndex,
			};
			++writtenCount;
		}

		return std::span<const ColliderProxy>(proxies, writtenCount);
	}
} // namespace fang
