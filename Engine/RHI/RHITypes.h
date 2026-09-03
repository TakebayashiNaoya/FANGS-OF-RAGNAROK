/**
 * @file RHITypes.h
 * @brief RHI の生成条件と頂点まわりの型。
 * @details GraphicsDevice と各台帳の両方が使う。GraphicsDevice.h に置くと
 *          GraphicsDevice.h ⇄ 台帳のヘッダで include が循環するので切り出してある。
 */
#pragma once

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
		UByte4,           /**< 0〜255 を整数のまま読む。関節の番号に使う。 */
	};

	/** @brief バッファの用途。 */
	enum class EnBufferKind : uint8_t
	{
		Vertex,
		Index,
		Constant, /**< シェーダの定数バッファ。大きさは 256 バイト境界へ切り上げる。 */
	};

	/**
	 * @brief テクスチャのピクセル形式。DXGI へは RHI の内側で変換する。
	 * @details Srgb 付きは「読むときに GPU がリニアへ直す」形式。色として見せるもの（ベースカラー）は
	 *          Srgb 付き、数値として読むもの（フォントのマスクや、いずれ足す法線マップ）は無印を使う。
	 */
	enum class EnTextureFormat : uint8_t
	{
		RGBA8,     /**< 8 bit × 4。行のバイト数は 幅 × 4。 */
		RGBA8Srgb, /**< 同じ並びで、サンプル時にリニアへ直される。 */
		BC7,       /**< ブロック圧縮。4×4 テクセルが 16 バイト ➡ 1 テクセルあたり 1 バイト。 */
		BC7Srgb,   /**< 同上。ベースカラーはこれで焼く。 */
	};

	/**
	 * @brief ミップ 1 段ぶんの中身。
	 * @details BC 系の「1 行」は 4 テクセル行ぶんのブロック列を指す。rowPitch と sizeInBytes は
	 *          その数え方でのバイト数。
	 */
	struct TextureMipLevel
	{
		const void* pixels      = nullptr; /**< 左上から右へ、行間の詰め物なし。 */
		uint32_t    width       = 0;       /**< この段のテクセル数。 */
		uint32_t    height      = 0;
		uint32_t    rowPitch    = 0; /**< 1 行のバイト数。 */
		uint32_t    sizeInBytes = 0; /**< この段の総バイト数。rowPitch × 行数と一致すること。 */
	};

	/**
	 * @brief テクスチャの生成条件。
	 * @details mipLevels の 0 番が最大で、以降は半分ずつ小さくなっている並びであること。
	 *          1 段だけ渡せばミップ無しのテクスチャになる。
	 */
	struct TextureSource
	{
		std::span<const TextureMipLevel> mipLevels;
		EnTextureFormat                  format = EnTextureFormat::RGBA8;
	};

	/**
	 * @brief パイプラインが持つルートパラメータの番号。
	 * @details 番号は作った順で決まるので、GraphicsPipelineDesc の組み合わせごとに変わる。
	 *          コマンドを積む側に数字を直接書くと、パラメータを 1 つ増やした日に絵だけが黙って壊れる。
	 *          ➡ パイプラインを差したときにこの束を持ち回り、番号は必ずここから引く。
	 */
	struct RootParameterLayout
	{
		/** @brief そのパラメータを持っていないことを表す番号。 */
		static constexpr uint32_t UNUSED = 0xFFFFFFFFu;

		uint32_t rootConstants        = UNUSED; /**< b0 のルート定数。 */
		uint32_t objectConstantBuffer = UNUSED; /**< b0 のルート CBV。ルート定数とは排他。 */
		uint32_t constantBuffer       = UNUSED; /**< b1 のルート CBV。 */
		uint32_t texture              = UNUSED; /**< t0 のディスクリプタテーブル。 */
	};

	/** @brief 頂点属性 1 つ。 */
	struct VertexAttribute
	{
		const char*    semanticName  = nullptr;                /**< HLSL 側の名前。"POSITION" など。 */
		uint32_t       semanticIndex = 0;                      /**< 同名が複数あるときの番号。TEXCOORD0 / 1 など。 */
		EnVertexFormat format        = EnVertexFormat::Float3; /**< この属性 1 個の型。 */
		uint32_t       offsetInBytes = 0;                      /**< 頂点構造体の先頭からの位置。 */
	};

	/** @brief パイプラインの生成条件。 */
	struct GraphicsPipelineDesc
	{
		std::span<const uint8_t> vertexShaderBytecode; /**< コンパイル済み頂点シェーダ。ShaderCompiler の出力を渡す。 */
		std::span<const uint8_t> pixelShaderBytecode;  /**< コンパイル済みピクセルシェーダ。 */
		std::span<const VertexAttribute> vertexLayout; /**< 頂点構造体の並び。 */

		/** @brief b0 に置くルート定数の数（32 bit 単位）。0 なら作らない。hasObjectConstantBuffer と排他。 */
		uint32_t rootConstantCount = 0;

		/**
		 * @brief b0 に定数バッファを 1 本差すか（VS と PS の両方から見える）。rootConstantCount と排他。
		 * @details 16 DWORD を超える定数の口。実機（Xbox One）のドライバは大きなルート定数の
		 *          パイプライン生成でデバイスロストするので、大きな定数は必ずこちらで渡す。
		 */
		bool hasObjectConstantBuffer = false;

		/** @brief t0 にテクスチャを 1 枚差すか。差すならサンプラ s0 も付く。 */
		bool hasTexture = false;

		/**
		 * @brief b1 に定数バッファを 1 本差すか。
		 * @details ルート定数に載らない大きさのものを渡す口。ディスクリプタを持たないルート CBV なので
		 *          ヒープの管理が増えない。骨のスキニング行列がこれで渡る。
		 */
		bool hasConstantBuffer = false;

		bool isAlphaBlendEnabled = false; /**< 半透明合成をするか。有効にすると裏面も描く。 */
		bool isDepthTestEnabled  = false; /**< 深度テストと深度書き込みをするか。3D の物を描くときに立てる。 */
	};

	/** @brief デバイスの生成条件。 */
	struct GraphicsDeviceDesc
	{
		void*    windowHandle        = nullptr; /**< Windows なら HWND、UWP なら CoreWindow の IUnknown*。 */
		uint32_t width               = 0;       /**< バックバッファの幅（ピクセル）。 */
		uint32_t height              = 0;       /**< バックバッファの高さ（ピクセル）。 */
		bool     isDebugLayerEnabled = false;   /**< D3D12 のデバッグレイヤーを有効にするか。Debug 構成だけ。 */
	};

	/** @brief 画面をクリアする色。 */
	struct ClearColor
	{
		float red   = 0.0f; /**< 0.0〜1.0。 */
		float green = 0.0f; /**< 0.0〜1.0。 */
		float blue  = 0.0f; /**< 0.0〜1.0。 */
		float alpha = 1.0f; /**< 0.0〜1.0。不透明が 1.0。 */
	};
} // namespace fang::rhi
