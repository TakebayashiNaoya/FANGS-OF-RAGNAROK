/**
 * @file RHITypes.h
 * @brief RHI の生成条件と頂点まわりの型。
 * @details GraphicsDevice と各台帳の両方が使う。GraphicsDevice.h に置くと
 *          GraphicsDevice.h ⇄ 台帳のヘッダで include が循環するので切り出してある。
 */
#pragma once

#include "Core/CoreMacros.h"
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
		Half2,            /**< 16 bit 浮動小数 × 2。UV に使う。 */
		SByte4Normalized, /**< -127〜127 を -1.0〜1.0 として読む（-128 は -1.0 に飽和）。法線に使う。 */
	};

	/** @brief バッファの用途。 */
	enum class EnBufferKind : uint8_t
	{
		Vertex,
		Index,
		Constant, /**< シェーダの定数バッファ。大きさは 256 バイト境界へ切り上げる。 */
	};

	/**
	 * @brief リソースの用途。
	 * @details D3D12 は「今この資源を何に使っているか」を追跡していて、用途を変えるにはバリアで宣言し直す
	 *          必要がある。使う組み合わせだけを並べてあり、増えたらここに足す。
	 */
	enum class EnResourceState : uint8_t
	{
		Present,             /**< 画面に出す。バックバッファの初期状態でもある。 */
		RenderTarget,        /**< 色を書き込む。 */
		DepthWrite,          /**< 深度を書き込む。 */
		PixelShaderResource, /**< ピクセルシェーダから読む。 */
	};

	/**
	 * @brief テクスチャのピクセル形式。DXGI へは RHI の内側で変換する。
	 * @details Srgb 付きは「読むときに GPU がリニアへ直す」形式。色として見せるもの（ベースカラー）は
	 *          Srgb 付き、数値として読むもの（フォントのマスクや法線マップ）は無印を使う。
	 */
	enum class EnTextureFormat : uint8_t
	{
		RGBA8,     /**< 8 bit × 4。行のバイト数は 幅 × 4。 */
		RGBA8Srgb, /**< 同じ並びで、サンプル時にリニアへ直される。 */
		BC7,       /**< ブロック圧縮。4×4 テクセルが 16 バイト ➡ 1 テクセルあたり 1 バイト。 */
		BC7Srgb,   /**< 同上。ベースカラーはこれで焼く。 */
		BC5,       /**< ブロック圧縮の 2 チャンネル（赤と緑）。BC7 と同じく 4×4 テクセルが 16 バイト。法線マップに使う。 */
		R16,       /**< 16 bit × 1 の UNORM。行のバイト数は 幅 × 2。ハイトマップに使う。 */
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

		uint32_t rootConstants          = UNUSED; /**< b0 のルート定数。 */
		uint32_t objectConstantBuffer   = UNUSED; /**< b0 のルート CBV。ルート定数とは排他。 */
		uint32_t frameConstantBuffer    = UNUSED; /**< b1 のルート CBV。 */
		uint32_t skinningConstantBuffer = UNUSED; /**< b2 のルート CBV。 */

		/**
		 * @brief t0 のディスクリプタテーブル。
		 * @details テクスチャの枠は 1 枠 1 テーブルで、t0 から枠の数だけ連続した番号を占める
		 *          （枠ごとのハンドルがヒープ上で連続している保証が無いため、1 本のテーブルにまとめられない）。
		 *          スロット n のテーブル番号は texture + n。
		 */
		uint32_t texture = UNUSED;

		uint32_t textureCount = 0;      /**< テクスチャの枠の数。SetTexture のスロット番号の上限。 */
		uint32_t shadowMap    = UNUSED; /**< シャドウマップのディスクリプタテーブル。t はテクスチャの枠の次の番号。 */
	};

	/** @brief 頂点属性 1 つ。 */
	struct VertexAttribute
	{
		const char*    semanticName  = nullptr;                /**< HLSL 側の名前。"POSITION" など。 */
		uint32_t       semanticIndex = 0;                      /**< 同名が複数あるときの番号。TEXCOORD0 / 1 など。 */
		EnVertexFormat format        = EnVertexFormat::Float3; /**< この属性 1 個の型。 */
		uint32_t       offsetInBytes = 0;                      /**< 頂点構造体の先頭からの位置。 */
	};

	/** @brief プリミティブトポロジの種類。 */
	enum class EnPrimitiveTopology : uint8_t
	{
		TriangleList,
		LineList, /**< デバッグ線描画のように、頂点 2 個ずつを独立した線分として描く。 */
	};

	/** @brief サンプラ s0 が UV の範囲外をどう読むか。 */
	enum class EnSamplerAddressMode : uint8_t
	{
		Clamp, /**< 端のテクセルを引き延ばす。1 枚を 1 回だけ貼るものに使う。 */
		Wrap,  /**< 繰り返す。地形のレイヤのようにタイリングするものに使う。 */
	};

	/**
	 * @brief PSO に渡すシェーダ 1 本。
	 * @details PSO を作るのは常に bytecode。出どころはホットリロードで .hlsl を読み直すときだけ使うので、
	 *          切ってある構成では 2 つのメンバごと消える。
	 */
	struct ShaderSource
	{
		std::span<const uint8_t> bytecode; /**< ビルド時に FXC がヘッダ化したもの。 */

#if FANG_ENABLE_HOT_RELOAD
		/**
		 * @brief ソースツリーの根からの .hlsl の相対パス。
		 * @details nullptr なら作り直しの対象外。文字列は寿命の管理をしないので、リテラルを渡すこと。
		 */
		const char* sourceRelativePath = nullptr;

		const char* entryPointName = nullptr; /**< ビルド時の EntryPointName と同じ文字列。 */
#endif
	};

	/**
	 * @brief ShaderSource を組み立てる。
	 * @details 出どころの 2 つはホットリロードを切ると構造体から消える ➡ 呼び出し側に #if を書かせないために包む。
	 * @param bytecode           ビルド時に FXC がヘッダ化したもの。
	 * @param sourceRelativePath ソースツリーの根からの .hlsl の相対パス。文字列リテラルを渡すこと。
	 * @param entryPointName     ビルド時の EntryPointName と同じ文字列。
	 */
	[[nodiscard]] inline ShaderSource MakeShaderSource(
		std::span<const uint8_t> bytecode,
		const char*              sourceRelativePath,
		const char*              entryPointName
	)
	{
		ShaderSource shaderSource;
		shaderSource.bytecode = bytecode;

#if FANG_ENABLE_HOT_RELOAD
		shaderSource.sourceRelativePath = sourceRelativePath;
		shaderSource.entryPointName     = entryPointName;
#else
		FANG_UNUSED(sourceRelativePath);
		FANG_UNUSED(entryPointName);
#endif

		return shaderSource;
	}

	/** @brief パイプラインの生成条件。 */
	struct GraphicsPipelineDesc
	{
		ShaderSource vertexShader; /**< 頂点シェーダ。bytecode は必須。 */

		/**
		 * @brief ピクセルシェーダ。
		 * @details bytecode が空なら描画先 0 本の深度専用パイプラインになる。色を出さずに深度だけ埋めるパスは
		 *          ピクセルシェーダも描画先も要らないため。
		 */
		ShaderSource pixelShader;

		std::span<const VertexAttribute> vertexLayout; /**< 頂点構造体の並び。 */

		/** @brief 描くプリミティブの形。既定は三角形リスト。 */
		EnPrimitiveTopology topology = EnPrimitiveTopology::TriangleList;

		/** @brief b0 に置くルート定数の数（32 bit 単位）。0 なら作らない。hasObjectConstantBuffer と排他。 */
		uint32_t rootConstantCount = 0;

		/**
		 * @brief b0 に定数バッファを 1 本差すか（VS と PS の両方から見える）。rootConstantCount と排他。
		 * @details 16 DWORD を超える定数の口。実機（Xbox One）のドライバは大きなルート定数の
		 *          パイプライン生成でデバイスロストするので、大きな定数は必ずこちらで渡す。
		 */
		bool hasObjectConstantBuffer = false;

		/**
		 * @brief t0 から並べられるテクスチャの枠の上限。
		 * @details 一番食うのは地形で、スプラット 1 枚 + レイヤ 3 枚 ×（アルベド + 法線）の 7 枠。
		 *          枠 1 つがルートパラメータ 1 個になるので、上げるとルートシグネチャの DWORD 数も増える。
		 */
		static constexpr uint32_t MAX_TEXTURE_COUNT = 8;

		/**
		 * @brief t0 から連続で差すテクスチャの枚数。1 枚でも差すならサンプラ s0 も付く。
		 * @details 差す相手は CommandList::SetTexture がスロット番号で選ぶ。MAX_TEXTURE_COUNT まで。
		 */
		uint32_t textureCount = 0;

		/** @brief s0 が UV の範囲外をどう読むか。textureCount が 0 なら意味を持たない。 */
		EnSamplerAddressMode samplerAddressMode = EnSamplerAddressMode::Clamp;

		/**
		 * @brief 深度テクスチャを 1 枚差すか。差すなら比較サンプラ s1 も付く。
		 * @details t の番号は textureCount の次の枠（テクスチャ 1 枚なら t1、4 枚なら t4）。
		 */
		bool hasShadowMap = false;

		/**
		 * @brief b1 に定数バッファを 1 本差すか（VS と PS の両方から見える）。
		 * @details 1 フレームの間ずっと同じ値を置く口。視点・光のようにフレーム内で変わらないものを分けておくと、
		 *          描画物ごとに積み直すのは b0 だけで済む。
		 */
		bool hasFrameConstantBuffer = false;

		/**
		 * @brief b2 に骨のスキニング行列を差すか。
		 * @details ルート定数に載らない大きさのものを渡す口。ディスクリプタを持たないルート CBV なので
		 *          ヒープの管理が増えない。
		 */
		bool hasSkinningConstantBuffer = false;

		bool isAlphaBlendEnabled = false; /**< 半透明合成をするか。有効にすると裏面も描く。 */
		bool isDepthTestEnabled  = false; /**< 深度テストと深度書き込みをするか。3D の物を描くときに立てる。 */

		/**
		 * @brief 深度テストを通ったピクセルの深度を書き込むか。isDepthTestEnabled が true のときだけ効く。
		 * @details デバッグ線のように「メッシュに隠れてほしいが、深度を汚して他の描画物を隠したくない」ものは
		 *          false にする。
		 */
		bool isDepthWriteEnabled = true;

		/**
		 * @brief 書き込む深度を一律で奥へずらす量（深度バッファの最小刻み単位）。
		 * @details 深度専用パスで書いた値をそのまま比べると、同じ面が自分自身を遮っていると誤判定される。
		 *          ➡ 書き込み側を少し奥へ逃がして誤判定を消す。
		 */
		int32_t depthBias = 0;

		/**
		 * @brief 面の傾きに比例して深度を奥へずらす量。
		 * @details 光に対して斜めな面ほど 1 テクセルの中の深度差が大きく、一律のずらしでは足りないため。
		 */
		float slopeScaledDepthBias = 0.0f;
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
