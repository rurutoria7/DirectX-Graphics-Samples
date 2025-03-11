#pragma once
#include "d3dx12.h"

struct ProcessCommandPass
{
    void Init(
        ID3D12Device* in_device,
        std::wstring in_assetPath)
    {
        // Describe and create the compute pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        //computePsoDesc.pRootSignature = m_computeRootSignature.Get();
        //computePsoDesc.CS = CD3DX12_SHADER_BYTECODE( computeShader.Get() );

        //ThrowIfFailed( in_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &m_computeState ) ) );
        //NAME_D3D12_OBJECT( m_computeState );
    }

    void Dispatch(
        ID3D12GraphicsCommandList* in_commandList
    )
    {

    }
};