/**
 * @file RenderGraph.h
 * @brief 1 フレームぶんの描画パスの宣言と、そこから導いた記録手順。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include <cstdint>
#include <span>
#include <string_view>


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	class JobSystem;

	/**
	 * @brief RenderGraph が配ったリソースの番号。
	 * @details 1 フレーム限りの通し番号。毎フレーム全部を登録し直すのでスロットの再利用が起きず、
	 *          世代を持つ必要がない。
	 */
	struct RenderGraphResourceId
	{
		static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

		uint32_t index = INVALID_INDEX; /**< 既定構築と登録失敗では INVALID_INDEX のまま。 */

		/** @brief 登録に成功した番号なら true。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const { return index != INVALID_INDEX; }
	};

	/** @brief パスの記録をどのスレッドで行うか。 */
	enum class EnPassRecordThread : uint8_t
	{
		Job,  /**< ワーカースレッド。ほかのパスと同時に走る。 */
		Main, /**< メインスレッド。ImGui のようにスレッドを選べないものを積むパスで使う。 */
	};

	/** @brief 描画先の中身をパスの先頭でどう扱うか。 */
	enum class EnLoadOperation : uint8_t
	{
		Load,  /**< 前のパスが書いたものを残す。 */
		Clear, /**< 指定の値で塗りつぶす。 */
	};

	/**
	 * @brief パスの記録関数。
	 * @param userData    宣言に添えた値。
	 * @param commandList そのパス専用のコマンドリスト。ほかのパスの本には書かない。
	 * @details バリア・描画先の設定・クリアはグラフが積むので、この中では呼ばない。
	 *          ワーカーで走りうるので、中でヒープ確保・ロック・I/O をしない。
	 */
	using RenderPassRecordFunction = void (*)(void* userData, rhi::CommandList& commandList);

	/** @brief パス 1 つの宣言。 */
	struct RenderGraphPassDesc
	{
		/** @brief 1 つのパスが読めるリソースの数。 */
		static constexpr uint32_t MAX_READ_RESOURCE_COUNT = 4;

		/** @brief デバッグ用の名前。指す先は Execute が終わるまで生かしておくこと。 */
		std::string_view name;

		EnPassRecordThread recordThread = EnPassRecordThread::Job;

		RenderGraphResourceId colorTarget; /**< 無効なら色を書かないパス。 */
		EnLoadOperation       colorLoadOperation = EnLoadOperation::Load;
		rhi::ClearColor       clearColor; /**< colorLoadOperation が Clear のときだけ使う。 */

		RenderGraphResourceId depthTarget; /**< 無効なら深度を使わないパス。 */
		EnLoadOperation       depthLoadOperation = EnLoadOperation::Load;

		RenderGraphResourceId readResources[MAX_READ_RESOURCE_COUNT]; /**< シェーダから読むリソース。 */
		uint32_t              readResourceCount = 0;

		RenderPassRecordFunction record   = nullptr; /**< nullptr なら前置と後置だけを積む。 */
		void*                    userData = nullptr; /**< 指す先は Execute が終わるまで生かしておくこと。 */
	};

	/** @brief Compile が導いたバリア 1 本。 */
	struct RenderGraphBarrier
	{
		RenderGraphResourceId resource;
		rhi::EnResourceState  before = rhi::EnResourceState::Present;
		rhi::EnResourceState  after  = rhi::EnResourceState::Present;
	};

	/**
	 * @brief パス 1 つぶんの記録手順。
	 * @details Compile の出力。GPU が無くても中身を確かめられるように POD にしてある。
	 */
	struct CompiledRenderPass
	{
		/** @brief 1 つのパスが積めるバリアの数。リソース 1 つにつき最大 1 本なのでリソースの上限と同じ。 */
		static constexpr uint32_t MAX_BARRIER_COUNT = 8;

		RenderGraphBarrier beginBarriers[MAX_BARRIER_COUNT]; /**< 記録関数の前に積む。 */
		uint32_t           beginBarrierCount = 0;

		RenderGraphBarrier endBarriers[MAX_BARRIER_COUNT]; /**< 記録関数の後に積む。最終状態へ戻す遷移。 */
		uint32_t           endBarrierCount = 0;

		rhi::ClearColor clearColor; /**< isColorCleared が true のときだけ使う。 */
		bool            isColorCleared = false;
		bool            isDepthCleared = false;
	};

	/**
	 * @brief タイムスタンプ 2 つの差をミリ秒へ直す。
	 * @param beginTicks     区間の頭の tick。
	 * @param endTicks       区間の末尾の tick。
	 * @param ticksPerSecond 1 秒あたりの tick 数（GraphicsDevice::GetTimestampFrequency）。
	 * @return endTicks が beginTicks より前（書き込み前の枠を読んだとき）か ticksPerSecond が 0 なら 0。
	 */
	[[nodiscard]] inline float ConvertTimestampTicksToMilliseconds(
		uint64_t beginTicks,
		uint64_t endTicks,
		uint64_t ticksPerSecond
	)
	{
		if (endTicks < beginTicks || ticksPerSecond == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(
			static_cast<double>(endTicks - beginTicks) * 1000.0 / static_cast<double>(ticksPerSecond)
		);
	}

#if FANG_ENABLE_PROFILER
	/** @brief パス 1 つの GPU 時間。 */
	struct RenderGraphPassGpuTime
	{
		std::string_view name; /**< 宣言の string_view そのもの。Execute が終わるまで有効。 */
		float            milliseconds = 0.0f;
	};
#endif

	/**
	 * @brief 1 フレームぶんの描画パスをまとめ、バリアとクリアを導いて実行する。
	 * @details パスは宣言順のまま実行する（並べ替えない）。毎フレーム Reset ➡ 宣言 ➡ Compile ➡ Execute と
	 *          組み直す前提で、持ち物は全部固定長 ➡ 定常状態のヒープ確保は 0。
	 * @threading 構築・Compile・Execute はメインスレッドのみ。記録は EnPassRecordThread::Job を指定した
	 *            パスだけワーカーで走り、どのパスも自分のコマンドリスト以外には書かない。
	 */
	class RenderGraph
	{
	public:
		FANG_NON_COPYABLE(RenderGraph);

		/** @brief 1 フレームに宣言できるパスの数。GraphicsDevice が貸せるコマンドリストの本数と揃えてある。 */
		static constexpr uint32_t MAX_PASS_COUNT = 8;

		/** @brief 1 フレームに登録できるリソースの数。 */
		static constexpr uint32_t MAX_RESOURCE_COUNT = 8;

		RenderGraph() = default;

		/** @brief 宣言と導出の結果と借りた本を全部捨てる。フレームの頭で呼ぶ。 */
		void Reset();

		/**
		 * @brief 今のバックバッファを外部リソースとして登録する。
		 * @return リソース番号。上限に達していたら無効な番号（IsValid() が false）。
		 * @details 初期状態も最終状態も Present。描画先へ移す遷移と Present へ戻す遷移は Compile が導く。
		 */
		[[nodiscard]] RenderGraphResourceId ImportBackBuffer();

		/**
		 * @brief 深度バッファを外部リソースとして登録する。
		 * @return リソース番号。上限に達していたら無効な番号。
		 * @details 初期状態も最終状態も DepthWrite。誰も読まないうちは遷移が 1 本も出ない。
		 */
		[[nodiscard]] RenderGraphResourceId ImportDepthBuffer();

		/**
		 * @brief オフスクリーンの深度テクスチャを外部リソースとして登録する。
		 * @param texture シャドウマップなど、TexturePool::CreateDepth で作った深度テクスチャ。
		 * @param width   このテクスチャのビューポートに使う横のテクセル数。
		 * @param height  縦のテクセル数。
		 * @return リソース番号。上限に達していたら無効な番号。
		 * @details 初期状態も最終状態も DepthWrite。バックバッファ・深度バッファと同じく毎フレーム
		 *          登録し直す前提で、途中の遷移は Compile が導く。
		 */
		[[nodiscard]] RenderGraphResourceId ImportDepthTexture(
			rhi::TextureHandle texture,
			uint32_t           width,
			uint32_t           height
		);

		/**
		 * @brief パスを 1 つ宣言する。
		 * @param desc 宣言の中身。中身はこの場で控えるので、戻った後に desc を壊してよい。
		 * @details 上限を超えたぶんは捨てる。
		 */
		void AddPass(const RenderGraphPassDesc& desc);

		/**
		 * @brief パスごとのバリアとクリアを導く。
		 * @details GPU に触らないので、結果は GetCompiledPass で読んで確かめられる。
		 */
		void Compile();

		/**
		 * @brief パスごとにコマンドリストを借りて記録する。
		 * @param device    コマンドリストの貸し元。BeginFrame の後、EndFrame の前に呼ぶこと。
		 * @param jobSystem 記録ジョブの積み先。Job 指定のパスが 1 つも無ければ使わない。
		 * @details EndFrame は呼ばない。記録し終えた本の列は GetCommandLists() から取って呼び出し側が渡す。
		 *          本を借りられなかったフレームは 1 本も記録せず、GetCommandLists() が空になる。
		 */
		void Execute(rhi::GraphicsDevice& device, JobSystem& jobSystem);

		/** @brief 宣言されているパスの数。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetPassCount() const { return m_passCount; }

		/**
		 * @brief Compile が出したパス 1 つぶんの記録手順。
		 * @param passIndex 宣言順のパス番号。GetPassCount() 未満であること。
		 */
		[[nodiscard]] const CompiledRenderPass& GetCompiledPass(uint32_t passIndex) const;

		/**
		 * @brief Execute が記録したコマンドリストの列。
		 * @return 宣言順に並んだ本。GraphicsDevice::EndFrame へそのまま渡す。記録できなかったフレームは空。
		 */
		[[nodiscard]] std::span<rhi::CommandList* const> GetCommandLists() const;

#if FANG_ENABLE_PROFILER
		/**
		 * @brief 直前のフレームのパス別 GPU 時間と、先頭パスの開始から末尾パスの終了までの合計を読む。
		 * @details EndFrame の後に呼ぶ。device が待ち終えたタイムスタンプとこのグラフのパス列が同じフレームのもの
		 *          であること（今は毎フレーム待ち切るので成立する。in-flight を増やすときはパス名も面ごとに控える）。
		 * @param device               タイムスタンプの読み元。
		 * @param outPassTimes         パス順に書く。長さがパス数より短ければ、入るぶんだけ書く。
		 * @param outFrameMilliseconds 先頭の開始から末尾の終了まで。本と本の隙間も含めた GPU の占有時間。
		 * @return 書いたパスの数。タイムスタンプが取れない環境は 0。
		 */
		uint32_t ReadPassGpuTimes(
			const rhi::GraphicsDevice&        device,
			std::span<RenderGraphPassGpuTime> outPassTimes,
			float*                            outFrameMilliseconds
		) const;
#endif


	private:
		/** @brief どのパスからも使われていないことを表す番号。 */
		static constexpr uint32_t INVALID_PASS_INDEX = 0xFFFFFFFFu;

		/** @brief 登録したリソース 1 つぶんの状態。 */
		struct Resource
		{
			rhi::EnResourceState initialState = rhi::EnResourceState::Present; /**< フレームの頭の用途。 */
			rhi::EnResourceState finalState   = rhi::EnResourceState::Present; /**< フレームの終わりに戻す用途。 */
			rhi::EnResourceState currentState = rhi::EnResourceState::Present; /**< Compile が追っている今の用途。 */

			uint32_t lastUsePassIndex = INVALID_PASS_INDEX; /**< 最後にこれを使うパスの番号。 */

			/**
			 * @brief テクスチャの裏付き。
			 * @details IsValid() ならバックバッファ・デバイス既定の深度バッファではなく、シャドウマップの
			 *          ようなテクスチャ 1 枚を指すリソース。遷移とビューポートの決め方がこれで変わる。
			 */
			rhi::TextureHandle texture;

			uint32_t width  = 0; /**< texture が有効なときのビューポート横幅。 */
			uint32_t height = 0; /**< texture が有効なときのビューポート縦幅。 */
		};

		/** @brief 記録ジョブ 1 件に渡す入力。ジョブの中で組み立てずに済むよう POD で揃える。 */
		struct RecordJobArguments
		{
			RenderGraph* graph     = nullptr;
			uint32_t     passIndex = 0;
		};

		/** @brief 記録ジョブの入口。JobSystem に渡せるよう関数ポインタの形にしてある。 */
		static void RecordPassJob(void* arguments, uint32_t workerIndex);

		/**
		 * @brief リソースを 1 つ登録する。上限に達していたら無効な番号を返す。
		 * @param texture テクスチャの裏付きがあるなら渡す。バックバッファ・デバイス既定の深度バッファは既定値のまま。
		 * @param width   texture を渡すときのビューポート横幅。
		 * @param height  縦幅。
		 */
		[[nodiscard]] RenderGraphResourceId AddResource(
			rhi::EnResourceState initialState,
			rhi::EnResourceState finalState,
			rhi::TextureHandle   texture = rhi::TextureHandle{},
			uint32_t             width   = 0,
			uint32_t             height  = 0
		);

		/** @brief そのリソースを最後に使うパスの番号を更新する。最終状態へ戻す遷移の置き場を決めるために使う。 */
		void MarkUse(uint32_t passIndex, RenderGraphResourceId resource);

		/** @brief 求める用途が今の用途と違えば、そのパスの先頭バリア列へ遷移を積む。 */
		void RequireState(uint32_t passIndex, RenderGraphResourceId resource, rhi::EnResourceState state);

		/** @brief そのパスで使い終わるリソースについて、最終状態へ戻す遷移を末尾バリア列へ積む。 */
		void AddFinalBarriers(uint32_t passIndex);

		/** @brief パス 1 つを自分のコマンドリストへ記録する。前置 ➡ 記録関数 ➡ 後置の順。 */
		void RecordPass(uint32_t passIndex);

		/** @brief 導いたバリアを 1 本積む。RHI に遷移の口があるリソースだけが対象。 */
		void ApplyBarrier(rhi::CommandList& commandList, const RenderGraphBarrier& barrier) const;

		RenderGraphPassDesc m_passes[MAX_PASS_COUNT];         /**< 宣言順。並べ替えない。 */
		CompiledRenderPass  m_compiledPasses[MAX_PASS_COUNT]; /**< Compile の出力。パスと同じ並び。 */

		Resource m_resources[MAX_RESOURCE_COUNT];

		rhi::CommandList* m_commandLists[MAX_PASS_COUNT] = {}; /**< Execute が借りた本。パスと同じ並び。 */

		/** @brief バックバッファのリソース番号。専用の遷移口(TransitionBackBuffer)を使う相手を見分けるために持つ。 */
		RenderGraphResourceId m_backBufferResourceId;

		RenderGraphResourceId m_depthResourceId;

		uint32_t m_passCount        = 0;
		uint32_t m_resourceCount    = 0;
		uint32_t m_commandListCount = 0; /**< Execute が借りた本数。借りられなかったフレームは 0。 */

		uint32_t m_backBufferWidth  = 0; /**< ビューポートに使う。Execute がデバイスから取る。 */
		uint32_t m_backBufferHeight = 0;
	};
} // namespace fang
