/**
 * @file Broadphase.cpp
 * @brief 種類を選んで Broadphase の実体を作る・壊す。
 */
#include "Pch.h"
#include "Collision/Broadphase.h"
#include "Collision/CollisionLog.h"
#include "Collision/DynamicAabbTreeBroadphase.h"
#include "Collision/SweepAndPruneBroadphase.h"
#include "Collision/UniformGridBroadphase.h"
#include "Core/Memory/Allocator.h"


namespace fang
{
	IBroadphase* CreateBroadphase(IAllocator& allocator, EnBroadphaseType type)
	{
		switch (type)
		{
			case EnBroadphaseType::SweepAndPrune: return New<SweepAndPruneBroadphase>(allocator);
			case EnBroadphaseType::UniformGrid: return New<UniformGridBroadphase>(allocator);
			case EnBroadphaseType::DynamicAabbTree: return New<DynamicAabbTreeBroadphase>(allocator);
		}

		FANG_ASSERT(false, "未知の Broadphase の種類");
		return nullptr;
	}


	void DestroyBroadphase(IAllocator& allocator, IBroadphase* broadphase)
	{
		Delete(allocator, broadphase);
	}
} // namespace fang
