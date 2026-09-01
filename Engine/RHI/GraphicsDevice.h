/**
 * @file GraphicsDevice.h
 * @brief DirectX 12 のデバイス・キュー・スワップチェーンをまとめた入口。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/BufferPool.h"
#include "RHI/CommandList.h"
#include "RHI/D3D12Common.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/GPUFence.h"
#include "RHI/PipelinePool.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include "RHI/SwapChain.h"
#include "RHI/TexturePool.h"
#include <cstdint>


namespace fang::rhi
{
	/**
	 * @brief DirectX 12 のデバイス。
	 * @details 部品（SwapChain / DescriptorHeap / GPUFence / 各台帳）を持ち、フレームの開始と終了を仕切る。
	 *          公開する操作は public、D3D12 の実体は private に置く。
	 * @threading Initialize / Shutdown / BeginFrame / EndFrame はメインスレッドのみ。
	 */
	class GraphicsDevice
	{
	public:
		FANG_NON_COPYABLE(GraphicsDevice);

		GraphicsDevice();
		~GraphicsDevice();

		/**
		 * @brief デバイス・キュー・スワップチェーンを作る。
		 * @param desc 生成条件。windowHandle は必須。width / height はバックバッファの大きさ（ピクセル）。
		 * @return 失敗したら false。途中で失敗しても Shutdown を呼べば片付く。
		 */
		[[nodiscard]] bool Initialize(const GraphicsDeviceDesc& desc);

		/** @brief GPU の完了を待ってから全部を解放する。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief パイプラインを作る。
		 * @param desc 生成条件。シェーダのバイトコードと頂点レイアウトはこの呼び出しの間だけ参照する。
		 * @return 失敗したら無効なハンドル（IsValid() が false）。
		 */
		[[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc);

		/** @brief パイプラインを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyPipeline(PipelineHandle handle);

		/**
		 * @brief 中身を変えないバッファを作って data を書き込む。
		 * @param data          書き込む中身。sizeInBytes 分読んだらもう参照しないので、呼び出し後は破棄してよい。
		 * @param sizeInBytes   data の大きさ（バイト）。
		 * @param strideInBytes Vertex なら頂点 1 個の大きさ。Index ならインデックス 1 個の大きさ（2 か 4）。
		 * @param kind          頂点バッファかインデックスバッファか。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle CreateBuffer(
			const void*  data,
			uint32_t     sizeInBytes,
			uint32_t     strideInBytes,
			EnBufferKind kind
		);

		/**
		 * @brief 毎フレーム書き換えるバッファを作る。中身は空で、UpdateBuffer で書き込む。
		 * @param capacityInBytes 確保する大きさ（バイト）。後から増やせないので最大量で確保する。
		 * @param strideInBytes   CreateBuffer と同じ。
		 * @param kind            頂点バッファかインデックスバッファか。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle CreateDynamicBuffer(
			uint32_t     capacityInBytes,
			uint32_t     strideInBytes,
			EnBufferKind kind
		);

		/**
		 * @brief バッファの先頭から data を書き込む。
		 * @param handle      書き込み先。CreateBuffer / CreateDynamicBuffer が返したもの。
		 * @param data        書き込む中身。
		 * @param sizeInBytes data の大きさ（バイト）。作ったときの容量を超えるとアサートに掛かる。
		 */
		void UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes);

		/** @brief バッファを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyBuffer(BufferHandle handle);

		/**
		 * @brief テクスチャを作って中身を転送する。転送が終わるまでこの中で待つので、起動時やロード時に呼ぶ。
		 * @param pixels RGBA 各 8 bit のピクセル列。左上から右へ、行間の詰め物なし（1 行 = width * 4 バイト）。
		 * @param width  横のピクセル数。
		 * @param height 縦のピクセル数。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle CreateTexture2D(const void* pixels, uint32_t width, uint32_t height);

		/** @brief テクスチャを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyTexture(TextureHandle handle);

		/**
		 * @brief バックバッファを作り直す。ウィンドウのサイズが変わったときに呼ぶ。
		 * @param width  新しい横幅（ピクセル）。0 や前回と同じ値なら何もしない。
		 * @param height 新しい高さ（ピクセル）。
		 * @details BeginFrame と EndFrame の間では呼べない。
		 */
		void Resize(uint32_t width, uint32_t height);

		/**
		 * @brief フレームを開始し、クリア済みのバックバッファに積めるコマンドリストを返す。
		 * @param clearColor 画面を塗りつぶす色。
		 * @return コマンドリスト。EndFrame まで有効で、解放は不要。失敗したら nullptr（そのフレームは描かずに飛ばす）。
		 */
		[[nodiscard]] CommandList* BeginFrame(const ClearColor& clearColor);

		/** @brief 積んだコマンドを送って Present し、GPU の完了を待つ。 */
		void EndFrame();


	private:
		friend class CommandList;

		ComPtr<IDXGIFactory6>      m_factory;      /**< アダプタ列挙とスワップチェーン生成の入口。 */
		ComPtr<ID3D12Device>       m_device;       /**< D3D12 の本体。全リソースの生成元。 */
		ComPtr<ID3D12CommandQueue> m_commandQueue; /**< コマンドを GPU に流す唯一の列。 */

		SwapChain      m_swapChain;                /**< バックバッファの束と RTV。画面の大きさもここが持つ。 */
		DescriptorHeap m_shaderVisibleHeap;        /**< シェーダから見える SRV の置き場。 */
		GPUFence       m_fence;                    /**< GPU の進み具合を知るカウンタ。WaitForGPU で使う。 */

		PipelinePool m_pipelines;                  /**< PipelineHandle で引く台帳。 */
		BufferPool   m_buffers;                    /**< BufferHandle で引く台帳。 */
		TexturePool  m_textures;                   /**< TextureHandle で引く台帳。 */

		ComPtr<ID3D12CommandAllocator>    m_commandAllocators[BACK_BUFFER_COUNT]; /**< コマンドの記録メモリ。 */
		ComPtr<ID3D12GraphicsCommandList> m_commandList; /**< コマンドの記録口。毎フレーム Reset する。 */

		CommandList m_commandListWrapper;                /**< BeginFrame が返す公開型。中身は m_commandList を指す。 */

		/** @brief Initialize に入った時点で立つ。途中で失敗しても Shutdown が片付けられるようにするため。 */
		bool m_isInitialized = false;

		bool m_isFrameOpen = false; /**< BeginFrame と EndFrame の間なら true。 */
	};
} // namespace fang::rhi
