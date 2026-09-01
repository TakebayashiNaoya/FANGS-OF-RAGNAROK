/**
 * @file PipelinePool.h
 * @brief パイプライン（ルートシグネチャ + PSO）の台帳。
 */
#pragma once

#include "RHI/D3D12Common.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
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
		struct Entry
		{
			ComPtr<ID3D12RootSignature> rootSignature;      /**< シェーダに渡す資源（ルート定数・テクスチャ）の並び。 */
			ComPtr<ID3D12PipelineState> pipelineState;      /**< シェーダとステート一式を焼き固めたもの。 */
			uint32_t                    generation = 0;     /**< ハンドルの世代と突き合わせる。 */
			bool                        isAlive    = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		/** @brief ハンドルから中身を引く。無効・解放済みならアサートに掛かる。 */
		[[nodiscard]] const Entry& Get(PipelineHandle handle) const;


	public:
		/**
		 * @brief パイプラインを作って台帳に登録する。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] PipelineHandle Create(ID3D12Device& device, const GraphicsPipelineDesc& desc);

		/** @brief スロットを空きに戻す。無効・解放済みのハンドルなら何もしない。 */
		void Destroy(PipelineHandle handle);

		/** @brief 台帳ごと捨てる。二重に呼んでも安全。 */
		void Shutdown();


	private:
		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<Entry> m_entries; /**< PipelineHandle.index で引く台帳。 */
	};
} // namespace fang::rhi
