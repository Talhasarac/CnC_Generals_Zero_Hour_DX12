#include "native_d3d12_pipeline_cache.h"
#include "native_d3d12_shaders.h"
#include "native_d3d12_diagnostics.h"

using Microsoft::WRL::ComPtr;
using NativeD3D12Internal::CheckHr;

namespace {
	D3D12_BLEND AlphaBlendFactor(D3D12_BLEND blend)
	{
		switch (blend) {
		case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
		default: return blend;
		}
	}

}

bool NativeD3D12PipelineCache::CreateBasic(ID3D12Device* device, const NativeD3D12PipelineSettings& settings, UINT colorOffset, UINT normalOffset, UINT specularOffset)
{
	if (!device || (m_device && m_device.Get() != device)) return false;
	if (!m_device) m_device = device;
	PipelineKey key = GetPipelineKey(settings, false);
	key[20] = colorOffset;
	key[27] = normalOffset;
	key[28] = specularOffset;
	const auto cached = m_pipelineCache.find(key);
	if (cached != m_pipelineCache.end()) { m_basicPipeline = cached->second; return true; }
	const std::string& shaderSource = NativeD3D12Shaders::Basic();

	auto& vertexShader = m_shaderCache[0];
	auto& pixelShader = m_shaderCache[1];
	HRESULT hr = S_OK;
	if (!vertexShader || !pixelShader) {
	ComPtr<ID3DBlob> errors;
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	hr = D3DCompile(shaderSource.data(), shaderSource.size(), "native_d3d12_basic.hlsl",
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainVS", "vs_5_0", compileFlags, 0,
		&vertexShader, &errors);
	if (FAILED(hr))
	{
		if (errors != nullptr)
			OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		return CheckHr(hr, "Compile native D3D12 vertex shader");
	}
	hr = D3DCompile(shaderSource.data(), shaderSource.size(), "native_d3d12_basic.hlsl",
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainPS", "ps_5_0", compileFlags, 0,
		&pixelShader, &errors);
	if (FAILED(hr))
	{
		if (errors != nullptr)
			OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		return CheckHr(hr, "Compile native D3D12 pixel shader");
	}

	}

	if (m_rootSignature == nullptr)
	{
		D3D12_DESCRIPTOR_RANGE textureRange = {};
		textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.NumDescriptors = 1;
		textureRange.BaseShaderRegister = 0;
		textureRange.OffsetInDescriptorsFromTableStart = 0;
		D3D12_DESCRIPTOR_RANGE samplerRange = {};
		samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		samplerRange.NumDescriptors = 1;
		samplerRange.BaseShaderRegister = 0;
		samplerRange.OffsetInDescriptorsFromTableStart = 0;
		D3D12_ROOT_PARAMETER rootParameters[11] = {};
		D3D12_DESCRIPTOR_RANGE extraRanges[6] = {};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[0].Descriptor.ShaderRegister = 0;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[2].Constants.ShaderRegister = 1;
		rootParameters[2].Constants.Num32BitValues = 11;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[3].DescriptorTable.pDescriptorRanges = &samplerRange;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		for (UINT stage=1; stage<4; ++stage) {
			for (UINT kind=0; kind<2; ++kind) {
				const UINT i=(stage-1)*2+kind, root=4+i;
				extraRanges[i] = kind ? samplerRange : textureRange;
				extraRanges[i].BaseShaderRegister = stage;
				rootParameters[root] = rootParameters[kind ? 3 : 1];
				rootParameters[root].DescriptorTable.pDescriptorRanges = &extraRanges[i];
			}
		}
		rootParameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[10].Descriptor.ShaderRegister = 2;
		rootParameters[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rootDescription = {};
		rootDescription.NumParameters = 11;
		rootDescription.pParameters = rootParameters;
		rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> serializedRoot;
		ComPtr<ID3DBlob> rootErrors;
		hr = D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
			&serializedRoot, &rootErrors);
		if (FAILED(hr))
			return CheckHr(hr, "Serialize native D3D12 root signature");
		if (!CheckHr(device->CreateRootSignature(0, serializedRoot->GetBufferPointer(),
			serializedRoot->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
			"Create native D3D12 root signature"))
			return false;
	}

	D3D12_INPUT_ELEMENT_DESC inputs[4] = {};
	inputs[0].SemanticName = "POSITION";
	inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputs[0].InputSlot = 0;
	inputs[0].AlignedByteOffset = 0;
	inputs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[1].SemanticName = "COLOR";
	inputs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	inputs[1].InputSlot = 0;
	inputs[1].AlignedByteOffset = colorOffset == UINT_MAX ? 0 : colorOffset;
	inputs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[2] = inputs[0];
	inputs[2].SemanticName = "NORMAL";
	inputs[2].AlignedByteOffset = normalOffset == UINT_MAX ? 0 : normalOffset;
	inputs[3] = inputs[1];
	inputs[3].SemanticIndex = 1;
	inputs[3].AlignedByteOffset = specularOffset == UINT_MAX ? 0 : specularOffset;

	D3D12_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = settings.fillMode;
	rasterizer.CullMode = settings.cullMode;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.DepthBias = settings.depthBias;
	D3D12_BLEND_DESC blend = {};
	blend.RenderTarget[0].BlendEnable = settings.blendEnable ? TRUE : FALSE;
	blend.RenderTarget[0].SrcBlend = settings.sourceBlend;
	blend.RenderTarget[0].DestBlend = settings.destinationBlend;
	blend.RenderTarget[0].BlendOp = settings.blendOp;
	blend.RenderTarget[0].SrcBlendAlpha = AlphaBlendFactor(settings.sourceBlend);
	blend.RenderTarget[0].DestBlendAlpha = AlphaBlendFactor(settings.destinationBlend);
	blend.RenderTarget[0].BlendOpAlpha = settings.blendOp;
	blend.RenderTarget[0].RenderTargetWriteMask = settings.renderTargetWriteMask;
	D3D12_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = (settings.useDefaultDepth && settings.depthEnable) ? TRUE : FALSE;
	depth.DepthWriteMask = settings.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = settings.depthFunc;
	depth.StencilEnable = (settings.useDefaultDepth && settings.stencilEnable) ? TRUE : FALSE;
	depth.StencilReadMask = settings.stencilReadMask;
	depth.StencilWriteMask = settings.stencilWriteMask;
	depth.FrontFace.StencilFunc = settings.stencilFunc;
	depth.FrontFace.StencilFailOp = settings.stencilFail;
	depth.FrontFace.StencilDepthFailOp = settings.stencilDepthFail;
	depth.FrontFace.StencilPassOp = settings.stencilPass;
	depth.BackFace = depth.FrontFace;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
	description.pRootSignature = m_rootSignature.Get();
	description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	description.BlendState = blend;
	description.SampleMask = UINT_MAX;
	description.RasterizerState = rasterizer;
	description.DepthStencilState = depth;
	description.InputLayout = {inputs, 4};
	description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	description.NumRenderTargets = 1;
	description.RTVFormats[0] = settings.targetFormat;
	description.DSVFormat = settings.useDefaultDepth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
	description.SampleDesc.Count = 1;
	if (!CheckHr(device->CreateGraphicsPipelineState(&description,
		IID_PPV_ARGS(&m_basicPipeline)), "Create native D3D12 basic pipeline"))
		return false;
	m_pipelineCache.emplace(key, m_basicPipeline);
	return true;
}

bool NativeD3D12PipelineCache::CreateTextured(ID3D12Device* device, const NativeD3D12PipelineSettings& settings, const std::array<UINT,4>& texcoordOffsets, UINT colorOffset,
	UINT normalOffset, UINT specularOffset, UINT materialVariant, UINT treeSwayOffset)
{
	if (!device || materialVariant >= m_materialPixelShaders.size() ||
		(m_device && m_device.Get() != device)) return false;
	if (!m_rootSignature && !CreateBasic(device, settings, UINT_MAX)) return false;
	PipelineKey key = GetPipelineKey(settings, true);
	key[25] = materialVariant;
	key[20] = colorOffset;
	for (UINT stage=0;stage<4;++stage) key[21+stage] = texcoordOffsets[stage];
	key[26] = treeSwayOffset;
	key[27] = normalOffset;
	key[28] = specularOffset;
	const auto cached = m_pipelineCache.find(key);
	if (cached != m_pipelineCache.end()) { m_texturedPipeline = cached->second; return true; }
	const std::string& shaderSource = NativeD3D12Shaders::Textured();

	auto& vertexShader = m_shaderCache[2];
	auto& pixelShader = m_materialPixelShaders[materialVariant];
	HRESULT hr = S_OK;
	if (!vertexShader || !pixelShader) {
	ComPtr<ID3DBlob> errors;
	UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	char variantValue[2] = {static_cast<char>('0'+materialVariant),0};
	const D3D_SHADER_MACRO defines[] = {{"MATERIAL_VARIANT",variantValue},{nullptr,nullptr}};
	if (!vertexShader) {
	hr = D3DCompile(shaderSource.data(), shaderSource.size(),
		"native_d3d12_textured.hlsl", defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"mainVS", "vs_5_0", compileFlags, 0, &vertexShader, &errors);
	if (FAILED(hr))
		return CheckHr(hr, "Compile native D3D12 textured vertex shader");
	}
	if (!pixelShader) {
	hr = D3DCompile(shaderSource.data(), shaderSource.size(),
		"native_d3d12_textured.hlsl", defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"mainPS", "ps_5_0", compileFlags, 0, &pixelShader, &errors);
	if (FAILED(hr))
		return CheckHr(hr, "Compile native D3D12 textured pixel shader");
	}

	}

	D3D12_INPUT_ELEMENT_DESC inputs[9] = {};
	inputs[0].SemanticName = "POSITION";
	inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[1].SemanticName = "COLOR";
	inputs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	inputs[1].AlignedByteOffset = colorOffset == UINT_MAX ? 0 : colorOffset;
	inputs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[2].SemanticName = "TEXCOORD";
	inputs[2].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputs[2].AlignedByteOffset = texcoordOffsets[0];
	inputs[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	for (UINT stage = 1; stage < 4; ++stage) {
		inputs[2+stage] = inputs[2];
		inputs[2+stage].SemanticIndex = stage;
		inputs[2+stage].AlignedByteOffset = texcoordOffsets[stage];
	}
	inputs[6] = inputs[0];
	inputs[6].SemanticName = "TEXCOORD";
	inputs[6].SemanticIndex = 4;
	inputs[6].AlignedByteOffset = treeSwayOffset == UINT_MAX ? 0 : treeSwayOffset;
	inputs[7] = inputs[0];
	inputs[7].SemanticName = "NORMAL";
	inputs[7].AlignedByteOffset = normalOffset == UINT_MAX ? 0 : normalOffset;
	inputs[8] = inputs[1];
	inputs[8].SemanticIndex = 1;
	inputs[8].AlignedByteOffset = specularOffset == UINT_MAX ? 0 : specularOffset;
	D3D12_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = settings.fillMode;
	rasterizer.CullMode = settings.cullMode;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.DepthBias = settings.depthBias;
	D3D12_BLEND_DESC blend = {};
	blend.RenderTarget[0].BlendEnable = settings.blendEnable ? TRUE : FALSE;
	blend.RenderTarget[0].SrcBlend = settings.sourceBlend;
	blend.RenderTarget[0].DestBlend = settings.destinationBlend;
	blend.RenderTarget[0].BlendOp = settings.blendOp;
	blend.RenderTarget[0].SrcBlendAlpha = AlphaBlendFactor(settings.sourceBlend);
	blend.RenderTarget[0].DestBlendAlpha = AlphaBlendFactor(settings.destinationBlend);
	blend.RenderTarget[0].BlendOpAlpha = settings.blendOp;
	blend.RenderTarget[0].RenderTargetWriteMask = settings.renderTargetWriteMask;
	D3D12_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = (settings.useDefaultDepth && settings.depthEnable) ? TRUE : FALSE;
	depth.DepthWriteMask = settings.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = settings.depthFunc;
	depth.StencilEnable = (settings.useDefaultDepth && settings.stencilEnable) ? TRUE : FALSE;
	depth.StencilReadMask = settings.stencilReadMask;
	depth.StencilWriteMask = settings.stencilWriteMask;
	depth.FrontFace.StencilFunc = settings.stencilFunc;
	depth.FrontFace.StencilFailOp = settings.stencilFail;
	depth.FrontFace.StencilDepthFailOp = settings.stencilDepthFail;
	depth.FrontFace.StencilPassOp = settings.stencilPass;
	depth.BackFace = depth.FrontFace;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
	description.pRootSignature = m_rootSignature.Get();
	description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	description.BlendState = blend;
	description.SampleMask = UINT_MAX;
	description.RasterizerState = rasterizer;
	description.DepthStencilState = depth;
	description.InputLayout = {inputs, 9};
	description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	description.NumRenderTargets = 1;
	description.RTVFormats[0] = settings.targetFormat;
	description.DSVFormat = settings.useDefaultDepth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
	description.SampleDesc.Count = 1;
	if (!CheckHr(device->CreateGraphicsPipelineState(&description,
		IID_PPV_ARGS(&m_texturedPipeline)), "Create native D3D12 textured pipeline"))
		return false;
	m_pipelineCache.emplace(key, m_texturedPipeline);
	return true;
}

NativeD3D12PipelineCache::PipelineKey NativeD3D12PipelineCache::GetPipelineKey(const NativeD3D12PipelineSettings& settings, bool textured)
{
	PipelineKey key = {UINT(textured), UINT(settings.cullMode), UINT(settings.depthEnable), UINT(settings.depthWrite),
		UINT(settings.depthFunc), UINT(settings.blendEnable), UINT(settings.sourceBlend), UINT(settings.destinationBlend),
		UINT(settings.blendOp), settings.renderTargetWriteMask, UINT(settings.stencilEnable), UINT(settings.stencilFunc),
		settings.stencilReadMask, settings.stencilWriteMask, UINT(settings.stencilFail), UINT(settings.stencilDepthFail),
		UINT(settings.stencilPass), UINT(settings.targetFormat), UINT(settings.useDefaultDepth), UINT(settings.depthBias)};
	key[29]=UINT(settings.fillMode);
	return key;
}


void NativeD3D12PipelineCache::Reset()
{
	m_pipelineCache.clear();
	for (auto& shader : m_shaderCache) shader.Reset();
	for (auto& shader : m_materialPixelShaders) shader.Reset();
	m_basicPipeline.Reset();
	m_texturedPipeline.Reset();
	m_rootSignature.Reset();
	m_device.Reset();
}
