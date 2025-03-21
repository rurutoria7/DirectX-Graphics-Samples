#pragma once
#include <DirectXMath.h>
#include "d3dx12.h"
#include "DXSampleHelper.h"

struct CullInstancePass
{
    const int NUM_TRI_PER_GROUP = 64;
    const std::wstring c_shaderPath = L"CullInst.cso";
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    void Init(
        ID3D12Device* in_device,
        std::wstring in_assetPath )
    {
        // Create the root signature
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

            // Create the root signature
            // slot 0: root constant, b0
            // slot 1: SRV, t0, index_buffer
            // slot 2: UAV, u0, culled_index_buffer

            CD3DX12_ROOT_PARAMETER1 computeRootParameters[3] = {};
            computeRootParameters[0].InitAsConstants( 17, 0, 0 );
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
            std::wstring c_csFilename = in_assetPath + c_shaderPath;

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
        D3D12_GPU_VIRTUAL_ADDRESS in_inst_buffer,
        D3D12_GPU_VIRTUAL_ADDRESS in_proccessed_inst_buffer,
        UINT in_num_inst,
        DirectX::XMMATRIX in_vp
    )
    {
        in_commandList->SetPipelineState( m_pipelineState.Get() );
        in_commandList->SetComputeRootSignature( m_rootSignature.Get() );

        in_commandList->SetComputeRoot32BitConstant( 0, in_num_inst, 0 );
        DirectX::XMFLOAT4X4 vp_data;
        DirectX::XMStoreFloat4x4( &vp_data, XMMatrixTranspose( in_vp ) );
        in_commandList->SetComputeRoot32BitConstants( 0, 16, &vp_data, 1 );

        in_commandList->SetComputeRootShaderResourceView( 1, in_inst_buffer );
        in_commandList->SetComputeRootUnorderedAccessView( 2, in_proccessed_inst_buffer);
        int numGroups = (in_num_inst - 1) / NUM_TRI_PER_GROUP + 1;
        in_commandList->Dispatch( numGroups, 1, 1 );
    }
};