/**
 * @file CommandList.cpp
 * @brief 描画コマンドの記録の実装。
 */
#include "Pch.h"
#include "RHI/CommandList.h"
#include "Core/Log/Assert.h"
#include "RHI/GraphicsDevice.h"


namespace fang::rhi
{
	namespace
	{
		D3D12_RESOURCE_STATES ToD3D12ResourceState(EnResourceState state)
		{
			switch (state)
			{
				case EnResourceState::Present:             return D3D12_RESOURCE_STATE_PRESENT;
				case EnResourceState::RenderTarget:        return D3D12_RESOURCE_STATE_RENDER_TARGET;
				case EnResourceState::DepthWrite:          return D3D12_RESOURCE_STATE_DEPTH_WRITE;
				case EnResourceState::PixelShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			}

			return D3D12_RESOURCE_STATE_COMMON;
		}
	} // namespace


	void CommandList::TransitionBackBuffer(EnResourceState before, EnResourceState after)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = m_device->m_swapChain.GetCurrentBackBuffer();
		barrier.Transition.StateBefore = ToD3D12ResourceState(before);
		barrier.Transition.StateAfter  = ToD3D12ResourceState(after);
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
	}


	void CommandList::SetRenderTargetToBackBuffer(bool withDepth)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_device->m_swapChain.GetCurrentRenderTargetView();
		const D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = m_device->m_depthBuffer.GetDepthStencilView();

		commandList->OMSetRenderTargets(1, &renderTargetView, FALSE, withDepth ? &depthStencilView : nullptr);
	}


	void CommandList::ClearRenderTarget(const ClearColor& color)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		// D3D12 は 0.0〜1.0 の RGBA を並びで読む。
		const float clearValues[4] = { color.red, color.green, color.blue, color.alpha };
		commandList->ClearRenderTargetView(m_device->m_swapChain.GetCurrentRenderTargetView(), clearValues, 0, nullptr);
	}


	void CommandList::ClearDepth()
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		// 一番奥の 1.0 で埋める。PSO の DepthFunc が LESS なので、手前の面だけが残る。
		commandList->ClearDepthStencilView(
			m_device->m_depthBuffer.GetDepthStencilView(),
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f,
			0,
			0,
			nullptr
		);
	}


	void CommandList::SetViewport(uint32_t width, uint32_t height)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		D3D12_VIEWPORT viewport{};
		viewport.Width    = static_cast<float>(width);
		viewport.Height   = static_cast<float>(height);
		viewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewport);

		SetScissor(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
	}


	void CommandList::SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const D3D12_RECT scissorRect{ left, top, right, bottom };
		commandList->RSSetScissorRects(1, &scissorRect);
	}


	void CommandList::SetPipeline(PipelineHandle pipeline)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const PipelinePool::Entry& entry = m_device->m_pipelines.Get(pipeline);

		// 番号は差した相手ごとに違う。Set* が引く先をここで入れ替える。
		m_boundRootParameters = entry.rootParameters;

		commandList->SetGraphicsRootSignature(entry.rootSignature.Get());
		commandList->SetPipelineState(entry.pipelineState.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}


	void CommandList::SetVertexBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const BufferPool::Entry& entry = m_device->m_buffers.Get(buffer);

		commandList->IASetVertexBuffers(0, 1, &entry.vertexBufferView);
	}


	void CommandList::SetIndexBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const BufferPool::Entry& entry = m_device->m_buffers.Get(buffer);

		commandList->IASetIndexBuffer(&entry.indexBufferView);
	}


	void CommandList::SetRootConstants(const void* values, uint32_t count32BitValues)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const uint32_t parameterIndex = m_boundRootParameters.rootConstants;
		FANG_ASSERT(parameterIndex != RootParameterLayout::UNUSED, "ルート定数を持たないパイプラインにルート定数を積んでいる");

		commandList->SetGraphicsRoot32BitConstants(parameterIndex, count32BitValues, values, 0);
	}


	void CommandList::SetObjectConstantBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const uint32_t parameterIndex = m_boundRootParameters.objectConstantBuffer;
		FANG_ASSERT(parameterIndex != RootParameterLayout::UNUSED, "b0 の定数バッファを持たないパイプラインに定数バッファを差している");

		const BufferPool::Entry& entry = m_device->m_buffers.Get(buffer);
		FANG_ASSERT(entry.kind == EnBufferKind::Constant, "定数バッファとして作られていないバッファを差している");

		commandList->SetGraphicsRootConstantBufferView(parameterIndex, entry.resource->GetGPUVirtualAddress());
	}


	void CommandList::SetFrameConstantBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const uint32_t parameterIndex = m_boundRootParameters.frameConstantBuffer;
		FANG_ASSERT(parameterIndex != RootParameterLayout::UNUSED, "b1 の定数バッファを持たないパイプラインに定数バッファを差している");

		const BufferPool::Entry& entry = m_device->m_buffers.Get(buffer);
		FANG_ASSERT(entry.kind == EnBufferKind::Constant, "定数バッファとして作られていないバッファを差している");

		commandList->SetGraphicsRootConstantBufferView(parameterIndex, entry.resource->GetGPUVirtualAddress());
	}


	void CommandList::SetSkinningConstantBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const uint32_t parameterIndex = m_boundRootParameters.skinningConstantBuffer;
		FANG_ASSERT(parameterIndex != RootParameterLayout::UNUSED, "b2 の定数バッファを持たないパイプラインに定数バッファを差している");

		const BufferPool::Entry& entry = m_device->m_buffers.Get(buffer);
		FANG_ASSERT(entry.kind == EnBufferKind::Constant, "定数バッファとして作られていないバッファを差している");

		commandList->SetGraphicsRootConstantBufferView(parameterIndex, entry.resource->GetGPUVirtualAddress());
	}


	void CommandList::SetTexture(TextureHandle texture)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const uint32_t parameterIndex = m_boundRootParameters.texture;
		FANG_ASSERT(parameterIndex != RootParameterLayout::UNUSED, "テクスチャを持たないパイプラインにテクスチャを差している");

		const TexturePool::Entry&         entry = m_device->m_textures.Get(texture);
		const D3D12_GPU_DESCRIPTOR_HANDLE descriptor =
			m_device->m_shaderVisibleHeap.GetGPUHandle(entry.descriptorIndex);

		commandList->SetGraphicsRootDescriptorTable(parameterIndex, descriptor);
	}


	void CommandList::Draw(uint32_t vertexCount)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		commandList->DrawInstanced(vertexCount, 1, 0, 0);
	}


	void CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		commandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
	}
} // namespace fang::rhi
