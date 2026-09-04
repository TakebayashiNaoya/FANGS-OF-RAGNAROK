/**
 * @file DummyTexture.h
 * @brief テクスチャが無いときに差す 1×1 のダミー。
 */
#pragma once

#include "RHI/RHIHandles.h"


namespace fang::rhi
{
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	/**
	 * @brief 接線空間で真上を向く（0, 0, 1）1×1 の法線マップを作る。
	 * @return 失敗したら無効なハンドル。
	 * @details 法線マップを持たないマテリアルにこれを差すと、シェーダから「テクスチャがあるか」の分岐が消える。
	 *          RGBA8 で焼くが、シェーダは RG しか読まないので BC5 の法線マップと同じ 1 本のコードを通る。
	 *          メッシュと地形の 2 か所が呼ぶので、平坦法線の値を 2 か所に書かないためここに置いてある。
	 *          解放は呼び出し側（GraphicsDevice::DestroyTexture）の仕事。
	 * @threading メインスレッドのみ。
	 */
	[[nodiscard]] rhi::TextureHandle CreateDummyNormalMap(rhi::GraphicsDevice& device);
} // namespace fang
