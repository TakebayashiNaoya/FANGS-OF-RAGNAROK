/**
 * @file GPUFence.cpp
 * @brief GPU の進み具合を測るフェンスの実装。
 */
#include "Pch.h"
#include "RHI/GPUFence.h"


namespace fang::rhi
{
	bool GPUFence::Initialize(ID3D12Device& device)
	{
		if (!CheckHresult(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "フェンスの生成"))
		{
			return false;
		}

		m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			FANG_LOG_ERROR(RHI, "フェンス用のイベントを作れなかった");
			return false;
		}

		return true;
	}

	void GPUFence::Shutdown()
	{
		if (m_fenceEvent != nullptr)
		{
			::CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}

		m_fence.Reset();
	}

	void GPUFence::WaitForGPU(ID3D12CommandQueue& commandQueue)
	{
		// 初期化が途中で失敗した状態で Shutdown から呼ばれても落ちないための守り。
		if (m_fence == nullptr)
		{
			return;
		}

		// フェンスの値はフレーム番号ごとに分けず、単調増加の 1 本にする。
		// バックバッファごとに別々の値を積むと、値が前後して「もう完了している」と誤判定する。
		const uint64_t valueToWait = m_nextFenceValue;

		// Signal は「ここまでの仕事を全部終えたら fence に valueToWait を書け」という
		// 注文をキューの末尾に積む。キューは先入れ先出しなので、
		// 「fence に値が書かれた ⇔ それより前の仕事が全部終わった」が成立する。
		if (!CheckHresult(commandQueue.Signal(m_fence.Get(), valueToWait), "フェンスの Signal"))
		{
			return;
		}

		++m_nextFenceValue;

		// まず現在値を覗くだけ（待たない）。もう届いていれば何もせず帰る。
		if (m_fence->GetCompletedValue() < valueToWait)
		{
			// まだなら「fence が valueToWait に達したらこのイベントを点灯して」と予約し、
			// スレッドを OS に預けて眠る。ビジーループで CPU を焼かないための作法。
			FANG_VERIFY(SUCCEEDED(m_fence->SetEventOnCompletion(valueToWait, m_fenceEvent)));
			::WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}
} // namespace fang::rhi
