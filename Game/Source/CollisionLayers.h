/**
 * @file CollisionLayers.h
 * @brief 当たり判定の種別ビット。ビットの意味は Collision の外（ここ）で決める。
 */
#pragma once

#include <cstdint>


namespace fang::game
{
	constexpr uint32_t COLLISION_LAYER_CHARACTER = 1u << 0; /**< 狼などのキャラクター。 */
	constexpr uint32_t COLLISION_LAYER_PROP      = 1u << 1; /**< ステージの置き物・壁。 */
} // namespace fang::game
