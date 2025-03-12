#pragma once
#include "d3dx12.h"

struct ProcessCommandPass
{
    const int NUM_MESH_PER_GROUP = 64;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    void Init(
        ID3D12Device* in_device,
        std::wstring in_assetPath)
    {
        // Create the root signature
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

            // Create the root signature
            // slot 0: root constant, b0
            // slot 1: SRV, t0
            // slot 2: UAV, u0

            CD3DX12_ROOT_PARAMETER1 computeRootParameters[3];
            computeRootParameters[0].InitAsConstants( 1, 0, 0 );
            computeRootParameters[1].InitAsShaderResourceView( 0, 0 );
            computeRootParameters[2].InitAsUnorderedAccessView( 0, 0 );

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
            computeRootSignatureDesc.Init_1_1( _countof( computeRootParameters ), computeRootParameters );

            ComPtr<ID3DBlob> signature, error;
            ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &computeRootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
            ThrowIfFailed( in_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) );
        }

        // Create the pipeline state
        {
            std::wstring c_csFilename = in_assetPath + L"CS.cso";

            struct
            {
                byte* data;
                uint32_t size;
            } cshader;

            ReadDataFromFile( c_csFilename.c_str(), &cshader.data, &cshader.size );

            D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
            computePsoDesc.pRootSignature = m_rootSignature.Get();
            computePsoDesc.CS = { cshader.data, cshader.size };

            ThrowIfFailed( in_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
        }
    }

    void RecordDispatch(
        ID3D12GraphicsCommandList* in_commandList,
        D3D12_GPU_VIRTUAL_ADDRESS in_command_buffer,
        D3D12_GPU_VIRTUAL_ADDRESS in_proccsed_command_buffer,
        UINT in_numMeshes
    )
    {
        in_commandList->SetPipelineState( m_pipelineState.Get() );
        in_commandList->SetComputeRootSignature( m_rootSignature.Get() );
        in_commandList->SetComputeRoot32BitConstant( 0, in_numMeshes, 0);
        in_commandList->SetComputeRootShaderResourceView( 1, in_command_buffer );
        in_commandList->SetComputeRootUnorderedAccessView( 2, in_proccsed_command_buffer );
        int numGroups = (in_numMeshes - 1) / NUM_MESH_PER_GROUP + 1;
        in_commandList->Dispatch( numGroups, 1, 1 );
    }
};