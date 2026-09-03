/**
 * @file GraphicsDevice.h
 * @brief DirectX 12 のデバイス・キュー・スワップチェーンをまとめた入口。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/BufferPool.h"
#include "RHI/CommandList.h"
#include "RHI/D3D12Common.h"
#include "RHI/DepthBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/GPUFence.h"
#include "RHI/PipelinePool.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include "RHI/SwapChain.h"
#include "RHI/TexturePool.h"
#include <cstdint>
#include <span>


namespace fang::rhi
{
	/**
	 * @brief DirectX 12 のデバイス。
	 * @details 部品（SwapChain / DepthBuffer / DescriptorHeap / GPUFence / 各台帳）を持ち、
	 *          フレームの開始と終了を仕切る。
	 *          公開する操作は public、D3D12 の実体は private に置く。
	 * @threading Initialize / Shutdown / BeginFrame / AcquireCommandList / EndFrame / Resize はメインスレッドのみ。
	 */
	class GraphicsDevice
	{
	public:
		FANG_NON_COPYABLE(GraphicsDevice);

		/** @brief 1 フレームで貸せるコマンドリストの本数。並列記録の人数の上限でもある。 */
		static constexpr uint32_t MAX_COMMAND_LIST_COUNT = 8;

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
		 * @threading メインスレッドと記録ジョブから呼べる。台帳を読んでマップ済み領域へ写すだけで、
		 *            生成も解放もしないため。ただし同じバッファへ同時に書かないことは呼び出し側の約束。
		 */
		void UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes);

		/** @brief バッファを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyBuffer(BufferHandle handle);

		/**
		 * @brief テクスチャを作って全ミップを転送する。転送が終わるまでこの中で待つので、起動時やロード時に呼ぶ。
		 * @param source 形式と段ごとの中身。この呼び出しの間だけ読む。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle CreateTexture2D(const TextureSource& source);

		/**
		 * @brief ミップ無し RGBA8 のテクスチャを作る。上の TextureSource 版の薄い包み。
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
		 * @brief フレームを開始する。
		 * @details このフレームぶんの記録メモリを巻き戻して、貸出の帳簿を空にするだけ。バリアもクリアも
		 *          描画先の設定もしない ➡ どの描画先に何を積むかは呼び出し側が決める。
		 */
		void BeginFrame();

		/**
		 * @brief 記録できる状態のコマンドリストを 1 本借りる。
		 * @details 借りた本は空で、描画先もビューポートも差さっていない。返した後の片付けは EndFrame がやる。
		 * @return コマンドリスト。EndFrame まで有効で、解放は不要。
		 *         失敗したら nullptr（主にデバイスロスト。そのフレームは描かずに畳む）。
		 */
		[[nodiscard]] CommandList* AcquireCommandList();

		/**
		 * @brief 積んだコマンドを送って Present し、GPU の完了を待つ。
		 * @param commandLists 実行するコマンドリスト。渡した順に GPU が処理する。
		 *                     借りた本を渡さなくてもよく、空なら Present とフェンス待ちだけを行う。
		 */
		void EndFrame(std::span<CommandList* const> commandLists);


	private:
		friend class CommandList;

		/** @brief デバイス削除（ロスト）の理由をログに残す。記録の準備が失敗したときの診断用。 */
		void LogDeviceRemovedReason() const;

		ComPtr<IDXGIFactory6>      m_factory;      /**< アダプタ列挙とスワップチェーン生成の入口。 */
		ComPtr<ID3D12Device>       m_device;       /**< D3D12 の本体。全リソースの生成元。 */
		ComPtr<ID3D12CommandQueue> m_commandQueue; /**< コマンドを GPU に流す唯一の列。 */

		SwapChain      m_swapChain;         /**< バックバッファの束と RTV。画面の大きさもここが持つ。 */
		DepthBuffer    m_depthBuffer;       /**< 深度バッファと DSV。SwapChain と同じ大きさで作り直す。 */
		DescriptorHeap m_shaderVisibleHeap; /**< シェーダから見える SRV の置き場。 */
		GPUFence       m_fence;             /**< GPU の進み具合を知るカウンタ。WaitForGPU で使う。 */

		PipelinePool m_pipelines; /**< PipelineHandle で引く台帳。 */
		BufferPool   m_buffers;   /**< BufferHandle で引く台帳。 */
		TexturePool  m_textures;  /**< TextureHandle で引く台帳。 */

		/**
		 * @brief コマンドの記録メモリ。
		 * @details 本ごとに分けるのは、記録中の本が同じアロケータを共有できないため。バックバッファぶん持つのは、
		 *          GPU がまだ読んでいるメモリを巻き戻さないため。
		 */
		ComPtr<ID3D12CommandAllocator> m_commandAllocators[MAX_COMMAND_LIST_COUNT][BACK_BUFFER_COUNT];

		ComPtr<ID3D12GraphicsCommandList> m_commandLists[MAX_COMMAND_LIST_COUNT]; /**< コマンドの記録口。 */

		CommandList m_commandListWrappers[MAX_COMMAND_LIST_COUNT]; /**< AcquireCommandList が貸し出す公開型。 */

		uint32_t m_acquiredCommandListCount = 0; /**< このフレームで貸した本数。BeginFrame が 0 に戻す。 */

		/** @brief Initialize に入った時点で立つ。途中で失敗しても Shutdown が片付けられるようにするため。 */
		bool m_isInitialized = false;

		bool m_isFrameOpen = false; /**< BeginFrame と EndFrame の間なら true。 */

		/** @brief このフレームの記録メモリを巻き戻せたか。倒れていると AcquireCommandList が貸さない。 */
		bool m_isFrameRecordable = false;
	};
} // namespace fang::rhi
