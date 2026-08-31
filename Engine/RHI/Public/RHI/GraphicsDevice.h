/**
 * @file GraphicsDevice.h
 * @brief DirectX 12 のデバイス・キュー・スワップチェーンをまとめた入口。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/CommandList.h"
#include "RHI/RHIHandles.h"
#include <cstdint>
#include <span>


namespace fang::rhi
{
	/** @brief 頂点属性の型。 */
	enum class EnVertexFormat : uint8_t
	{
		Float2,
		Float3,
		Float4,
		UByte4Normalized, /**< 0〜255 を 0.0〜1.0 として読む。頂点カラーに使う。 */
	};

	/** @brief バッファの用途。 */
	enum class EnBufferKind : uint8_t
	{
		Vertex,
		Index,
	};

	/** @brief 頂点属性 1 つ。 */
	struct VertexAttribute
	{
		const char* semanticName = nullptr;                /**< HLSL 側の名前。"POSITION" など。 */
		uint32_t semanticIndex   = 0;                      /**< 同名が複数あるときの番号。TEXCOORD0 / 1 など。 */
		EnVertexFormat format    = EnVertexFormat::Float3;
		uint32_t offsetInBytes   = 0;                      /**< 頂点構造体の先頭からの位置。 */
	};

	/** @brief パイプラインの生成条件。 */
	struct GraphicsPipelineDesc
	{
		std::span<const uint8_t> vertexShaderBytecode;
		std::span<const uint8_t> pixelShaderBytecode;
		std::span<const VertexAttribute> vertexLayout;

		/** @brief b0 に置くルート定数の数（32 bit 単位）。0 なら作らない。 */
		uint32_t rootConstantCount = 0;

		/** @brief t0 にテクスチャを 1 枚差すか。差すならサンプラ s0 も付く。 */
		bool hasTexture = false;

		bool isAlphaBlendEnabled = false; /**< 半透明合成をするか。有効にすると裏面も描く。 */
	};

	/** @brief デバイスの生成条件。 */
	struct GraphicsDeviceDesc
	{
		void* windowHandle       = nullptr; /**< Windows なら HWND、UWP なら CoreWindow の IUnknown*。 */
		uint32_t width           = 0;
		uint32_t height          = 0;
		bool isDebugLayerEnabled = false;   /**< D3D12 のデバッグレイヤーを有効にするか。Debug 構成だけ。 */
	};

	/** @brief 画面をクリアする色。 */
	struct ClearColor
	{
		float red   = 0.0f;
		float green = 0.0f;
		float blue  = 0.0f;
		float alpha = 1.0f;
	};

	/**
	 * @brief DirectX 12 のデバイス。
	 * @details d3d12.h を知っているのは RHI の Private だけなので、中身は pimpl で隠す。
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
		 * @param data 書き込む中身。sizeInBytes 分読んだらもう参照しないので、呼び出し後は破棄してよい。
		 * @param sizeInBytes data の大きさ（バイト）。
		 * @param strideInBytes Vertex なら頂点 1 個の大きさ。Index ならインデックス 1 個の大きさ（2 か 4）。
		 * @param kind 頂点バッファかインデックスバッファか。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle CreateBuffer(const void* data,
												uint32_t sizeInBytes,
												uint32_t strideInBytes,
												EnBufferKind kind);

		/**
		 * @brief 毎フレーム書き換えるバッファを作る。中身は空で、UpdateBuffer で書き込む。
		 * @param capacityInBytes 確保する大きさ（バイト）。後から増やせないので最大量で確保する。
		 * @param strideInBytes CreateBuffer と同じ。
		 * @param kind 頂点バッファかインデックスバッファか。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle CreateDynamicBuffer(uint32_t capacityInBytes,
													   uint32_t strideInBytes,
													   EnBufferKind kind);

		/**
		 * @brief バッファの先頭から data を書き込む。
		 * @param handle 書き込み先。CreateBuffer / CreateDynamicBuffer が返したもの。
		 * @param data 書き込む中身。
		 * @param sizeInBytes data の大きさ（バイト）。作ったときの容量を超えるとアサートに掛かる。
		 */
		void UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes);

		/** @brief バッファを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyBuffer(BufferHandle handle);

		/**
		 * @brief テクスチャを作って中身を転送する。転送が終わるまでこの中で待つので、起動時やロード時に呼ぶ。
		 * @param pixels RGBA 各 8 bit のピクセル列。左上から右へ、行間の詰め物なし（1 行 = width * 4 バイト）。
		 * @param width 横のピクセル数。
		 * @param height 縦のピクセル数。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle CreateTexture2D(const void* pixels, uint32_t width, uint32_t height);

		/** @brief テクスチャを解放する。無効・解放済みのハンドルなら何もしない。 */
		void DestroyTexture(TextureHandle handle);

		/**
		 * @brief バックバッファを作り直す。ウィンドウのサイズが変わったときに呼ぶ。
		 * @param width 新しい横幅（ピクセル）。0 や前回と同じ値なら何もしない。
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

		class Impl;
		Impl* m_impl = nullptr;
	};
} // namespace fang::rhi
