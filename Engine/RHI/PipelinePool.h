/**
 * @file PipelinePool.h
 * @brief パイプライン（ルートシグネチャ + PSO）の台帳。
 */
#pragma once

#include "RHI/D3D12Common.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include <span>
#include <vector>


namespace fang::rhi
{
	/**
	 * @brief PipelineHandle で引くパイプラインの置き場。
	 * @details 解放したスロットは世代を進めて再利用する。古いハンドルは世代違いで弾ける。
	 * @threading メインスレッドのみ。
	 */
	class PipelinePool
	{
	public:
		/** @brief 1 本のパイプラインが持てる頂点属性の上限。今の最大はスキンメッシュの 5 個。 */
		static constexpr uint32_t MAX_VERTEX_ATTRIBUTE_COUNT = 8;

		struct Entry
		{
			ComPtr<ID3D12RootSignature> rootSignature; /**< シェーダに渡す資源（ルート定数・テクスチャ）の並び。 */
			ComPtr<ID3D12PipelineState> pipelineState; /**< シェーダとステート一式を焼き固めたもの。 */

			RootParameterLayout rootParameters; /**< 資源ごとのルートパラメータ番号。 */

			/** @brief SetPipeline が IASetPrimitiveTopology の出し分けに使う。 */
			EnPrimitiveTopology topology = EnPrimitiveTopology::TriangleList;

#if FANG_ENABLE_HOT_RELOAD
			/**
			 * @brief 作り直すときの入力。
			 * @details bytecode と vertexLayout は控えない（どちらも呼び出し側の寿命に縛られるため）。
			 *          bytecode は作り直すたびに新しくコンパイルしたものを差し、頂点レイアウトは
			 *          下の写しから組み直す。
			 */
			GraphicsPipelineDesc recipe;

			/** @brief recipe.vertexLayout が指していた中身の写し。 */
			VertexAttribute vertexAttributes[MAX_VERTEX_ATTRIBUTE_COUNT]{};

			uint32_t vertexAttributeCount = 0; /**< 上の配列の有効な数。 */
#endif

			uint32_t generation = 0;     /**< ハンドルの世代と突き合わせる。 */
			bool     isAlive    = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		/** @brief ハンドルから中身を引く。無効・解放済みならアサートに掛かる。 */
		[[nodiscard]] const Entry& Get(PipelineHandle handle) const;

		/** @brief 台帳の枠の数。作り直しの対象を探すときに端から見るために要る。 */
		[[nodiscard]] uint32_t GetEntryCount() const { return static_cast<uint32_t>(m_entries.size()); }

		/** @brief 枠を番号で引く。空きスロットも返るので、使う前に isAlive を見ること。 */
		[[nodiscard]] const Entry& GetByIndex(uint32_t index) const;


	public:
		/**
		 * @brief パイプラインを作って台帳に登録する。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] PipelineHandle Create(ID3D12Device& device, const GraphicsPipelineDesc& desc);

#if FANG_ENABLE_HOT_RELOAD
		/**
		 * @brief 控えた生成条件と、渡し直されたバイトコードで枠の中身を作り直す。
		 * @details ルートシグネチャと PSO の両方がそろってから差し替えるので、途中で失敗しても
		 *          今映っている画は変わらない。ハンドルも世代も変わらないので、持っている側の配線は要らない。
		 * @param index          作り直す枠の番号。生きていない枠なら何もせず false。
		 * @param vertexBytecode 新しい頂点シェーダ。差し替えが終わるまで生きていること。
		 * @param pixelBytecode  新しいピクセルシェーダ。元が深度専用なら空を渡す。
		 * @return 作り直せたら true。
		 */
		[[nodiscard]] bool Recreate(
			ID3D12Device&            device,
			uint32_t                 index,
			std::span<const uint8_t> vertexBytecode,
			std::span<const uint8_t> pixelBytecode
		);
#endif

		/** @brief スロットを空きに戻す。無効・解放済みのハンドルなら何もしない。 */
		void Destroy(PipelineHandle handle);

		/** @brief 台帳ごと捨てる。二重に呼んでも安全。 */
		void Shutdown();


	private:
		/**
		 * @brief desc からルートシグネチャと PSO を作って outEntry へ入れる。
		 * @details 台帳には触らないので、新しい枠を作るときも作り直すときも同じ手順を通せる。
		 * @return どちらかの生成に失敗したら false。outEntry は書きかけのまま捨ててよい。
		 */
		[[nodiscard]] static bool BuildEntry(ID3D12Device& device, const GraphicsPipelineDesc& desc, Entry* outEntry);


	private:
		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<Entry> m_entries; /**< PipelineHandle.index で引く台帳。 */
	};
} // namespace fang::rhi
