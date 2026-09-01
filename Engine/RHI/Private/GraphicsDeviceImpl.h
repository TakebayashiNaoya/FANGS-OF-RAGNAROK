/**
 * @file GraphicsDeviceImpl.h
 * @brief GraphicsDevice::Impl の定義。RHI の Private だけが見る。
 */
#pragma once

#include "RHI/GraphicsDevice.h"
#include "BufferPool.h"
#include "D3D12Common.h"
#include "DescriptorHeap.h"
#include "GPUFence.h"
#include "PipelinePool.h"
#include "SwapChain.h"
#include "TexturePool.h"


namespace fang::rhi
{
	/**
	 * @brief GraphicsDevice の中身。
	 * @details Public のヘッダから d3d12.h を追い出すためだけの入れ物。
	 *          Pimpl イディオムの実装側で、ヘッダには前方宣言とポインタしかない。
	 *          狙いと代償は GraphicsDevice.h の m_impl のコメントにまとめてある。
	 *          機能ごとの中身は部品側（SwapChain / DescriptorHeap / GPUFence / 各 Pool）にあり、
	 *          ここはそれを並べるだけ。CommandList.cpp からも見るのでヘッダに置く。
	 */
	class GraphicsDevice::Impl
	{
	public:
		ComPtr<IDXGIFactory6>      factory;      /**< アダプタ列挙とスワップチェーン生成の入口。 */
		ComPtr<ID3D12Device>       device;       /**< D3D12 の本体。全リソースの生成元。 */
		ComPtr<ID3D12CommandQueue> commandQueue; /**< コマンドを GPU に流す唯一の列。 */

		SwapChain      swapChain;         /**< バックバッファの束と RTV。画面の大きさもここが持つ。 */
		DescriptorHeap shaderVisibleHeap; /**< シェーダから見える SRV の置き場。 */
		GPUFence       fence;             /**< GPU の進み具合を知るカウンタ。WaitForGPU で使う。 */

		PipelinePool pipelines; /**< PipelineHandle で引く台帳。 */
		BufferPool   buffers;   /**< BufferHandle で引く台帳。 */
		TexturePool  textures;  /**< TextureHandle で引く台帳。 */

		ComPtr<ID3D12CommandAllocator>    commandAllocators[BACK_BUFFER_COUNT]; /**< コマンドの記録メモリ。 */
		ComPtr<ID3D12GraphicsCommandList> commandList; /**< コマンドの記録口。毎フレーム Reset する。 */

		bool isFrameOpen = false; /**< BeginFrame と EndFrame の間なら true。 */

		CommandList commandListWrapper; /**< BeginFrame が返す公開型。中身は commandList を指す。 */
	};
} // namespace fang::rhi
