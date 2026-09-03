/**
 * @file PipelinePool.cpp
 * @brief パイプライン（ルートシグネチャ + PSO）の台帳の実装。
 */
#include "Pch.h"
#include "RHI/PipelinePool.h"
#include "Core/Log/Assert.h"
#include "RHI/DepthBuffer.h"


namespace fang::rhi
{
	namespace
	{
		DXGI_FORMAT ToDxgiFormat(EnVertexFormat format)
		{
			switch (format)
			{
				case EnVertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
				case EnVertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
				case EnVertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
				case EnVertexFormat::UByte4Normalized: return DXGI_FORMAT_R8G8B8A8_UNORM;
				case EnVertexFormat::UByte4: return DXGI_FORMAT_R8G8B8A8_UINT;
				case EnVertexFormat::Half2: return DXGI_FORMAT_R16G16_FLOAT;
				case EnVertexFormat::SByte4Normalized: return DXGI_FORMAT_R8G8B8A8_SNORM;
			}

			return DXGI_FORMAT_UNKNOWN;
		}
	} // namespace


	PipelineHandle PipelinePool::Create(ID3D12Device& device, const GraphicsPipelineDesc& desc)
	{
		D3D12_DESCRIPTOR_RANGE textureRange{};
		textureRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.NumDescriptors     = 1;
		textureRange.BaseShaderRegister = 0;

		D3D12_ROOT_PARAMETER rootParameters[3]{};
		uint32_t             rootParameterCount = 0;

		Entry entry;

		FANG_ASSERT(
			desc.rootConstantCount == 0 || !desc.hasObjectConstantBuffer,
			"b0 はルート定数かルート CBV のどちらか片方しか置けない"
		);

		if (desc.rootConstantCount > 0)
		{
			entry.rootParameters.rootConstants = rootParameterCount;

			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_VERTEX;

			parameter.Constants.Num32BitValues = desc.rootConstantCount;
			parameter.Constants.ShaderRegister = 0;
			++rootParameterCount;
		}

		if (desc.hasObjectConstantBuffer)
		{
			entry.rootParameters.objectConstantBuffer = rootParameterCount;

			// VS がワールド行列、PS がライトとマテリアルを同じ b0 から読むので、両方から見えるようにする。
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_CBV;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_ALL;

			parameter.Descriptor.ShaderRegister = 0;
			++rootParameterCount;
		}

		if (desc.hasConstantBuffer)
		{
			entry.rootParameters.constantBuffer = rootParameterCount;

			// ディスクリプタを作らず GPU アドレスを直接渡すルート CBV。ヒープのスロットを消費しない。
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_CBV;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_VERTEX;

			parameter.Descriptor.ShaderRegister = 1;
			++rootParameterCount;
		}

		if (desc.hasTexture)
		{
			entry.rootParameters.texture = rootParameterCount;

			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_PIXEL;

			parameter.DescriptorTable.NumDescriptorRanges = 1;
			parameter.DescriptorTable.pDescriptorRanges   = &textureRange;
			++rootParameterCount;
		}

		// サンプラは 1 種類しか要らないので静的サンプラで済ませる。
		D3D12_STATIC_SAMPLER_DESC staticSampler{};
		staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// ゼロ初期化のままだと MaxLOD が 0 になり、LOD が 0 に切り詰められて先頭のミップしか読まれない
		// ➡ ミップを持っていても縮小時のちらつきが消えない。上限を外して全段を使わせる。
		staticSampler.MinLOD = 0.0f;
		staticSampler.MaxLOD = D3D12_FLOAT32_MAX;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.NumParameters     = rootParameterCount;
		rootSignatureDesc.pParameters       = rootParameterCount > 0 ? rootParameters : nullptr;
		rootSignatureDesc.NumStaticSamplers = desc.hasTexture ? 1u : 0u;
		rootSignatureDesc.pStaticSamplers   = desc.hasTexture ? &staticSampler : nullptr;
		rootSignatureDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> serializedRootSignature;
		ComPtr<ID3DBlob> errors;
		if (!CheckHresult(
				::D3D12SerializeRootSignature(
					&rootSignatureDesc,
					D3D_ROOT_SIGNATURE_VERSION_1,
					&serializedRootSignature,
					&errors
				),
				"ルートシグネチャのシリアライズ"
			))
		{
			return PipelineHandle{};
		}

		if (!CheckHresult(
				device.CreateRootSignature(
					0,
					serializedRootSignature->GetBufferPointer(),
					serializedRootSignature->GetBufferSize(),
					IID_PPV_ARGS(&entry.rootSignature)
				),
				"ルートシグネチャの生成"
			))
		{
			return PipelineHandle{};
		}

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		inputElements.reserve(desc.vertexLayout.size());
		for (const VertexAttribute& attribute : desc.vertexLayout)
		{
			D3D12_INPUT_ELEMENT_DESC element{};
			element.SemanticName      = attribute.semanticName;
			element.SemanticIndex     = attribute.semanticIndex;
			element.Format            = ToDxgiFormat(attribute.format);
			element.AlignedByteOffset = attribute.offsetInBytes;
			element.InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			inputElements.push_back(element);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = entry.rootSignature.Get();
		pipelineDesc.VS             = { desc.vertexShaderBytecode.data(), desc.vertexShaderBytecode.size() };
		pipelineDesc.PS             = { desc.pixelShaderBytecode.data(), desc.pixelShaderBytecode.size() };

		pipelineDesc.InputLayout           = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
		pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipelineDesc.SampleDesc.Count = 1;
		pipelineDesc.SampleMask       = UINT_MAX;

		pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineDesc.RasterizerState.CullMode = desc.isAlphaBlendEnabled ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
		pipelineDesc.RasterizerState.DepthClipEnable = TRUE;

		D3D12_RENDER_TARGET_BLEND_DESC& blend = pipelineDesc.BlendState.RenderTarget[0];
		blend.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
		if (desc.isAlphaBlendEnabled)
		{
			blend.BlendEnable    = TRUE;
			blend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
			blend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOp        = D3D12_BLEND_OP_ADD;
			blend.SrcBlendAlpha  = D3D12_BLEND_ONE;
			blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
		}

		// 深度を使わないパイプラインにも BeginFrame が DSV を差すので、形式は常に合わせておく。
		// UNKNOWN のままだと差した DSV と食い違い、デバッグレイヤーに叱られる。
		pipelineDesc.DSVFormat = DepthBuffer::DEPTH_FORMAT;

		D3D12_DEPTH_STENCIL_DESC& depthStencil = pipelineDesc.DepthStencilState;
		if (desc.isDepthTestEnabled)
		{
			depthStencil.DepthEnable    = TRUE;
			depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

			// 深度は手前ほど小さいので、既に書かれている値より小さいものだけ通す。
			depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		}
		else
		{
			depthStencil.DepthEnable    = FALSE;
			depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		}

		// ステンシルは使わない。
		depthStencil.StencilEnable = FALSE;

		if (!CheckHresult(
				device.CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&entry.pipelineState)),
				"パイプラインステートの生成"
			))
		{
			return PipelineHandle{};
		}

		entry.isAlive = true;

		for (uint32_t index = 0; index < static_cast<uint32_t>(m_entries.size()); ++index)
		{
			if (!m_entries[index].isAlive)
			{
				entry.generation = m_entries[index].generation + 1;
				m_entries[index] = entry;
				return PipelineHandle{ index, entry.generation };
			}
		}

		m_entries.push_back(entry);
		return PipelineHandle{ static_cast<uint32_t>(m_entries.size() - 1), entry.generation };
	}


	void PipelinePool::Destroy(PipelineHandle handle)
	{
		if (!handle.IsValid() || handle.index >= m_entries.size())
		{
			return;
		}

		Entry& entry = m_entries[handle.index];
		if (entry.generation != handle.generation)
		{
			return;
		}

		entry.rootSignature.Reset();
		entry.pipelineState.Reset();
		entry.isAlive = false;
	}


	void PipelinePool::Shutdown()
	{
		m_entries.clear();
	}


	const PipelinePool::Entry& PipelinePool::Get(PipelineHandle handle) const
	{
		FANG_ASSERT(handle.IsValid() && handle.index < m_entries.size(), "無効なパイプラインハンドル");

		const Entry& entry = m_entries[handle.index];
		FANG_ASSERT(entry.isAlive && entry.generation == handle.generation, "解放済みのパイプラインハンドル");

		return entry;
	}
} // namespace fang::rhi
