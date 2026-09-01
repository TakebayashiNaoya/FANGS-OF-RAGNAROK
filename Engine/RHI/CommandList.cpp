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

		commandList->SetGraphicsRoot32BitConstants(0, count32BitValues, values, 0);
	}

	void CommandList::SetTexture(TextureHandle texture)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const TexturePool::Entry&         entry = m_device->m_textures.Get(texture);
		const D3D12_GPU_DESCRIPTOR_HANDLE descriptor =
			m_device->m_shaderVisibleHeap.GetGPUHandle(entry.descriptorIndex);

		// ルート定数がある構成ではテーブルは 2 番目に来る。
		commandList->SetGraphicsRootDescriptorTable(1, descriptor);
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
