/**
 * @file GpuProfiler.h
 * @brief GPUの処理時間を計測するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	/**
	 * @brief GPUの処理時間を計測するクラス
	 * @details GPUが書いたタイムスタンプの差で処理時間を測る。
	 *          読めるのは2フレーム前の値。
	 *          構成の切り替えの判定をRenderContextに書かなくていいようにこのクラスを作った。
	 */
	class GpuProfiler : private core::Noncopyable
	{
	public:
		/**
		 * @brief GPUの処理時間を計測するためのオブジェクトを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @param commandQueue GPUに命令を渡すためのID3D12CommandQueue
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue);

		/**
		 * @brief 計測を始める
		 * @param commandList 測る処理を積むコマンドリスト
		 * @param frameIndex 測る処理を積むフレームの番号
		 */
		void BeginFrame(ID3D12GraphicsCommandList* commandList, UINT frameIndex);

		/**
		 * @brief 計測を終わらせる
		 * @param commandList 測る処理を積むコマンドリスト
		 * @param frameIndex 測る処理を積むフレームの番号
		 */
		void EndFrame(ID3D12GraphicsCommandList* commandList, UINT frameIndex);

		/**
		 * @brief 計測した時間をミリ秒単位で返す
		 * @return 計測した時間(ミリ秒)
		 */
		inline float GetFrameMilliseconds() const { return m_frameMilliseconds; }


	private:
		/**
		 * @brief クエリヒープを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateQueryHeap(ID3D12Device* device);

		/**
		 * @brief 読み出し用のバッファを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateReadbackBuffer(ID3D12Device* device);


	private:
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap;      // GPUが時刻を書く入れ物
		Microsoft::WRL::ComPtr<ID3D12Resource>  m_readbackBuffer; // CPUが時刻を読むバッファ

		UINT64 m_timestampFrequency = 0;    // GPUの時計が1秒に進む数
		float  m_frameMilliseconds  = 0.0f; // 直前に読めたフレームのGPU処理の時間
	};
} // namespace fangEngine::render
