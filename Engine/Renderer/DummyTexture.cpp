/**
 * @file DummyTexture.cpp
 * @brief ダミーテクスチャの生成の実装。
 */
#include "Pch.h"
#include "Renderer/DummyTexture.h"
#include "RHI/GraphicsDevice.h"
#include <span>


namespace fang
{
	namespace
	{
		/**
		 * @brief 平坦な法線を 0〜1 へ写した値。(0, 0, 1) が (128, 128, 255)。
		 * @details 数値なので sRGB では焼かない。alpha は読まれないが、RGBA8 の行のバイト数を
		 *          そろえるために持つ。
		 */
		constexpr uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	} // namespace


	rhi::TextureHandle CreateDummyNormalMap(rhi::GraphicsDevice& device)
	{
		const rhi::TextureMipLevel mipLevel{
			.pixels      = FLAT_NORMAL,
			.width       = 1,
			.height      = 1,
			.rowPitch    = 4,
			.sizeInBytes = 4,
		};

		const rhi::TextureSource source{
			.mipLevels = std::span<const rhi::TextureMipLevel>(&mipLevel, 1),
			.format    = rhi::EnTextureFormat::RGBA8,
		};

		return device.CreateTexture2D(source);
	}
} // namespace fang
