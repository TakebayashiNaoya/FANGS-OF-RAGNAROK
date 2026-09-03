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
				case EnVertexFormat::Float2:           return DXGI_FORMAT_R32G32_FLOAT;
				case EnVertexFormat::Float3:           return DXGI_FORMAT_R32G32B32_FLOAT;
				case EnVertexFormat::Float4:           return DXGI_FORMAT_R32G32B32A32_FLOAT;
				case EnVertexFormat::UByte4Normalized: return DXGI_FORMAT_R8G8B8A8_UNORM;
				case EnVertexFormat::UByte4:           return DXGI_FORMAT_R8G8B8A8_UINT;
				case EnVertexFormat::Half2:            return DXGI_FORMAT_R16G16_FLOAT;
				case EnVertexFormat::SByte4Normalized: return DXGI_FORMAT_R8G8B8A8_SNORM;
			}

			return DXGI_FORMAT_UNKNOWN;
		}


		D3D12_PRIMITIVE_TOPOLOGY_TYPE ToPrimitiveTopologyType(EnPrimitiveTopology topology)
		{
			switch (topology)
			{
				case EnPrimitiveTopology::TriangleList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				case EnPrimitiveTopology::LineList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			}

			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
		}
	} // namespace


	PipelineHandle PipelinePool::Create(ID3D12Device& device, const GraphicsPipelineDesc& desc)
	{
		//------------------------------------------------------------------------
		// 1. ルートパラメータの構築(b0 ➡ b1 ➡ b2 ➡ t0 ➡ t1 の並びと、desc のフラグでどれが付くか)
		// 　シェーダから見えるレジスタごとにルートパラメータを 1 個ずつ積む。どれを積むかは
		// 　GraphicsPipelineDesc のフラグで決まり、並びは b0(rootConstants か objectConstantBuffer)➡
		// 　b1(frameConstantBuffer) ➡ b2(skinningConstantBuffer) ➡ t0(texture) ➡ t1(shadowMap) の順。
		//------------------------------------------------------------------------
		D3D12_DESCRIPTOR_RANGE textureRange{};
		textureRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.NumDescriptors     = 1;
		textureRange.BaseShaderRegister = 0;

		D3D12_DESCRIPTOR_RANGE shadowMapRange{};
		shadowMapRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		shadowMapRange.NumDescriptors     = 1;
		shadowMapRange.BaseShaderRegister = 1;

		D3D12_ROOT_PARAMETER rootParameters[5]{};
		uint32_t             rootParameterCount = 0;

		Entry entry;
		entry.topology = desc.topology;

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

		if (desc.hasFrameConstantBuffer)
		{
			entry.rootParameters.frameConstantBuffer = rootParameterCount;

			// VS がビュー射影行列、PS がカメラ位置と光を同じ b1 から読むので、両方から見えるようにする。
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_CBV;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_ALL;

			parameter.Descriptor.ShaderRegister = 1;
			++rootParameterCount;
		}

		if (desc.hasSkinningConstantBuffer)
		{
			entry.rootParameters.skinningConstantBuffer = rootParameterCount;

			// ディスクリプタを作らず GPU アドレスを直接渡すルート CBV。ヒープのスロットを消費しない。
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_CBV;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_VERTEX;

			parameter.Descriptor.ShaderRegister = 2;
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

		if (desc.hasShadowMap)
		{
			entry.rootParameters.shadowMap = rootParameterCount;

			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_PIXEL;

			parameter.DescriptorTable.NumDescriptorRanges = 1;
			parameter.DescriptorTable.pDescriptorRanges   = &shadowMapRange;
			++rootParameterCount;
		}

		//------------------------------------------------------------------------
		// 2. 静的サンプラ
		// 　テクスチャを読むときのフィルタ・アドレスモードなどの設定。s0 は色を読むふつうのサンプラ、
		// 　s1 は深度を「奥か手前か」で比べる比較サンプラ。要る側だけをルートシグネチャに付ける。
		//------------------------------------------------------------------------
		// サンプラは種類ごとに 1 個で足りるので静的サンプラで済ませる。
		D3D12_STATIC_SAMPLER_DESC staticSamplers[2]{};
		uint32_t                  staticSamplerCount = 0;

		if (desc.hasTexture)
		{
			D3D12_STATIC_SAMPLER_DESC& sampler = staticSamplers[staticSamplerCount];
			sampler.Filter                     = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU                   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.AddressV                   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.AddressW                   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.ComparisonFunc             = D3D12_COMPARISON_FUNC_ALWAYS;
			sampler.ShaderRegister             = 0;
			sampler.ShaderVisibility           = D3D12_SHADER_VISIBILITY_PIXEL;

			// ゼロ初期化のままだと MaxLOD が 0 になり、LOD が 0 に切り詰められて先頭のミップしか読まれない
			// ➡ ミップを持っていても縮小時のちらつきが消えない。上限を外して全段を使わせる。
			sampler.MinLOD = 0.0f;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			++staticSamplerCount;
		}

		if (desc.hasShadowMap)
		{
			// 比べた結果（遮られていれば 0、いなければ 1）を 4 テクセルで線形に混ぜるフィルタ。
			// 1 回のサンプルで影の境目が滑らかになる。
			D3D12_STATIC_SAMPLER_DESC& sampler = staticSamplers[staticSamplerCount];
			sampler.Filter                     = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			sampler.AddressU                   = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressV                   = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressW                   = D3D12_TEXTURE_ADDRESS_MODE_BORDER;

			// マップの外は「一番奥まで何も無い」= 影なしとして扱いたいので、境界色を白にする。
			sampler.BorderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

			sampler.ShaderRegister   = 1;
			sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

			sampler.MinLOD = 0.0f;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			++staticSamplerCount;
		}

		//------------------------------------------------------------------------
		// 3. ルートシグネチャのシリアライズと生成
		// 　組んだルートパラメータと静的サンプラを 1 本のバイナリへシリアライズし、それを渡して
		// 　ID3D12RootSignature を生成する。
		//------------------------------------------------------------------------
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.NumParameters     = rootParameterCount;
		rootSignatureDesc.pParameters       = rootParameterCount > 0 ? rootParameters : nullptr;
		rootSignatureDesc.NumStaticSamplers = staticSamplerCount;
		rootSignatureDesc.pStaticSamplers   = staticSamplerCount > 0 ? staticSamplers : nullptr;
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

		//------------------------------------------------------------------------
		// 4. PSO 記述(シェーダ・頂点レイアウト・各ステート)
		// 　頂点レイアウト・シェーダのバイトコード・ラスタライザ/ブレンド/深度ステンシルなど、
		// 　パイプラインステート 1 個ぶんの記述を desc の中身から埋める。
		//------------------------------------------------------------------------
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

		// PS が無いということは色を出さないということ ➡ 描画先も要らない。
		const bool isDepthOnly = desc.pixelShaderBytecode.empty();

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = entry.rootSignature.Get();
		pipelineDesc.VS             = { desc.vertexShaderBytecode.data(), desc.vertexShaderBytecode.size() };

		if (!isDepthOnly)
		{
			pipelineDesc.PS = { desc.pixelShaderBytecode.data(), desc.pixelShaderBytecode.size() };

			pipelineDesc.NumRenderTargets = 1;
			pipelineDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		pipelineDesc.InputLayout           = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
		pipelineDesc.PrimitiveTopologyType = ToPrimitiveTopologyType(desc.topology);

		pipelineDesc.SampleDesc.Count = 1;
		pipelineDesc.SampleMask       = UINT_MAX;

		pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineDesc.RasterizerState.CullMode = desc.isAlphaBlendEnabled ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
		pipelineDesc.RasterizerState.DepthClipEnable = TRUE;

		// 深度の押し込みはラスタライザがやる。クランプは掛けない（上限を設ける理由が無い）。
		pipelineDesc.RasterizerState.DepthBias            = desc.depthBias;
		pipelineDesc.RasterizerState.DepthBiasClamp       = 0.0f;
		pipelineDesc.RasterizerState.SlopeScaledDepthBias = desc.slopeScaledDepthBias;

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

		// 深度を使わないパイプラインでも、描画先に DSV が差さっていることがある。
		// UNKNOWN のままだと差した DSV と食い違い、デバッグレイヤーに叱られるので形式は常に合わせておく。
		pipelineDesc.DSVFormat = DepthBuffer::DEPTH_FORMAT;

		D3D12_DEPTH_STENCIL_DESC& depthStencil = pipelineDesc.DepthStencilState;
		if (desc.isDepthTestEnabled)
		{
			depthStencil.DepthEnable = TRUE;

			// デバッグ線のように「隠れてはほしいが深度を汚したくない」描画は書き込みだけ切る。
			depthStencil.DepthWriteMask =
				desc.isDepthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;

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

		//------------------------------------------------------------------------
		// 5. 生成と台帳への登録
		// 　CreateGraphicsPipelineState で PSO を作り、空いている台帳の枠があればそこへ詰め、
		// 　無ければ末尾に追加してハンドルを返す。
		//------------------------------------------------------------------------
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
