#pragma once
#include <DirectXMath.h>
#include "d3dx12.h"
#include "DXSampleHelper.h"
#include <iostream>

struct CullInstancePass
{
    struct CB {
        DirectX::XMFLOAT4 RTSize;
        float MaxMipLevel;
        float ActivateCulling;
        float MipBias;
        unsigned int NoofInstances;
        unsigned int NoofInstancesPowOf2;
        unsigned int NoofDrawcalls;
        unsigned int NoofGroups;
        float pad;
    } m_cb;

    static const int SCAN_BLOCK = 4;
    static const int NUM_THREADS_OF_KILL_INSTANCE = SCAN_BLOCK;
    static const int NUM_THREADS_OF_SCAN_GROUP = 1024;
    static const int NUM_THREADS_OF_SCAN_PREFIX = SCAN_BLOCK / 2;
    static const int NUM_THREADS_OF_COPY_INSTANCE_DATA = SCAN_BLOCK;

    ComPtr<ID3D12RootSignature> m_rs_kill_instance_pass;
    ComPtr<ID3D12PipelineState> m_pso_kill_instance_pass;

    ComPtr<ID3D12RootSignature> m_rs_scan_prefix_pass;
    ComPtr<ID3D12PipelineState> m_pso_scan_prefix_pass;

    ComPtr<ID3D12RootSignature> m_rs_scan_group_pass;
    ComPtr<ID3D12PipelineState> m_pso_scan_group_pass;

    ComPtr<ID3D12RootSignature> m_rs_copy_instance_pass;
    ComPtr<ID3D12PipelineState> m_pso_copy_instance_pass;

    ComPtr<ID3D12Resource> m_scanned_group_sum_buffer;
    ComPtr<ID3D12Resource> m_group_sum_buffer;


    static constexpr uint32_t ceil_div( uint32_t x, uint32_t y ) { return (x + y - 1) / y; }

    static uint32_t get_padded_size( uint32_t instance_count )
    {
        uint32_t res = (instance_count / 2 / NUM_THREADS_OF_SCAN_PREFIX + 1) * NUM_THREADS_OF_SCAN_PREFIX * 2;
        res = max( res, static_cast<uint32_t>(4096) );
        return res;
    }

    void init(
        ID3D12Device* in_device,
        std::wstring in_asset_path,
        unsigned in_num_inst )
    {
        auto create_rs = [in_device]( auto& rs )
            {
                D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

                // Create the root signature
                // slot 0: root constant, b0
                // slot 1: SRV, t0, index_buffer
                // slot 2: UAV, u0, culled_index_buffer

                CD3DX12_ROOT_PARAMETER1 computeRootParameters[4] = {};
                computeRootParameters[0].InitAsConstants( 20, 0, 0 );
                computeRootParameters[1].InitAsShaderResourceView( 0, 0 );
                computeRootParameters[2].InitAsUnorderedAccessView( 0, 0 );
                computeRootParameters[3].InitAsUnorderedAccessView( 1, 0 );

                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
                computeRootSignatureDesc.Init_1_1( _countof( computeRootParameters ), computeRootParameters );

                ComPtr<ID3DBlob> signature, error;
                ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &computeRootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
                ThrowIfFailed( in_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &rs ) ) );
            };
        auto create_rs_scan_prefix_pass = [in_device]( auto& rs )
            {
                D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

                /* Root signature of ScanInstnacesCS
                *
                * slot 0: constant <--> cbuffer OcclusionPassCB, b1
                * slot 1: root SRV <--> Buffer<bool> instancePredicatesIn, t0
                * slot 2: root UAV <--> RWBuffer<uint> groupSumArray, u0
                * slot 3: root UAV <--> RWBuffer<uint> scannedInstancePredicates, u1
                */

                CD3DX12_ROOT_PARAMETER1 computeRootParameters[4] = {};
                computeRootParameters[0].InitAsConstants( 12, 1, 0 );
                computeRootParameters[1].InitAsShaderResourceView( 0, 0 );
                computeRootParameters[2].InitAsUnorderedAccessView( 0, 0 );
                computeRootParameters[3].InitAsUnorderedAccessView( 1, 0 );

                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
                computeRootSignatureDesc.Init_1_1( _countof( computeRootParameters ), computeRootParameters );

                ComPtr<ID3DBlob> signature, error;
                ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &computeRootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
                ThrowIfFailed( in_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &rs ) ) );

            };
        auto create_rs_copy_instance_pass = [in_device](auto& rs)
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

            /* Root signature of CopyInstanceDataCS
            *
            * slot 0: constant <--> cbuffer OcclusionPassCB, b1
            * slot 1: root SRV <--> StructuredBuffer<Instance> instanceDataIn, t0
            * slot 2: root SRV <--> StructuredBuffer<my_uint> instancePredicatesIn, t1
            * slot 3: root SRV <--> StructuredBuffer<my_uint> groupSumArray, t2
            * slot 4: root SRV <--> StructuredBuffer<my_uint> scannedInstancePredicates, t3
            * slot 5: root UAV <--> RWStructuredBuffer<Instance> instanceDataOut, u0
            */

            CD3DX12_ROOT_PARAMETER1 computeRootParameters[6] = {};
            computeRootParameters[0].InitAsConstants(12, 1, 0);
            computeRootParameters[1].InitAsShaderResourceView(0, 0);
            computeRootParameters[2].InitAsShaderResourceView(1, 0);
            computeRootParameters[3].InitAsShaderResourceView(2, 0);
            computeRootParameters[4].InitAsShaderResourceView(3, 0);
            computeRootParameters[5].InitAsUnorderedAccessView(0, 0);

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
            computeRootSignatureDesc.Init_1_1(_countof(computeRootParameters), computeRootParameters);

            ComPtr<ID3DBlob> signature, error;
            ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error));
            ThrowIfFailed(in_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rs)));
        };
        auto create_rs_scan_group_pass = [in_device]( auto& rs )
            {
                D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
                /* Root signature of ScanGroupPass
                *
                * slot 0: constant <--> cbuffer OcclusionPassCB, b1
                * slot 1: root SRV <--> Buffer<bool> groupSumArrayIn, t0
                * slot 2: root UAV <--> RWBuffer<uint> groupSumArrayOut, u0
                */

                CD3DX12_ROOT_PARAMETER1 computeRootParameters[3] = {};
                computeRootParameters[0].InitAsConstants( 12, 1, 0 );
                computeRootParameters[1].InitAsShaderResourceView( 0, 0 );
                computeRootParameters[2].InitAsUnorderedAccessView( 0, 0 );
                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
                computeRootSignatureDesc.Init_1_1( _countof( computeRootParameters ), computeRootParameters );
                ComPtr<ID3DBlob> signature, error;
                ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &computeRootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
                ThrowIfFailed( in_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &rs ) ) );
            };
        auto create_pso = [in_device, in_asset_path]( std::wstring in_shaderPath, ComPtr<ID3D12RootSignature> in_rs, ComPtr<ID3D12PipelineState>& out_pso )
            {
                std::wstring c_csFilename = in_asset_path + in_shaderPath;
                struct
                {
                    byte* data;
                    uint32_t size;
                } cshader;
                ThrowIfFailed( ReadDataFromFile( c_csFilename.c_str(), &cshader.data, &cshader.size ) );
                D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
                computePsoDesc.pRootSignature = in_rs.Get();
                computePsoDesc.CS = { cshader.data, cshader.size };
                ThrowIfFailed( in_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &out_pso ) ) );
            };
        auto create_group_sum_buffer = [in_device, in_num_inst]( auto& res )
            {
                auto buffer_size = get_padded_size( in_num_inst ) * sizeof( unsigned );
                auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                ThrowIfFailed( in_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer( buffer_size, flags ),
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS( &res ) ) );
            };

        create_rs( m_rs_kill_instance_pass );
        create_pso( L"CullInst.cso", m_rs_kill_instance_pass, m_pso_kill_instance_pass );

        create_rs_scan_prefix_pass( m_rs_scan_prefix_pass );
        create_pso( L"ScanInstancesCS.cso", m_rs_scan_prefix_pass, m_pso_scan_prefix_pass );

        create_rs_scan_group_pass( m_rs_scan_group_pass );
        create_pso( L"ScanGroupsCS.cso", m_rs_scan_group_pass, m_pso_scan_group_pass );

        create_rs_copy_instance_pass( m_rs_copy_instance_pass );
        create_pso( L"CopyInstanceDataCS.cso", m_rs_copy_instance_pass, m_pso_copy_instance_pass );

        create_group_sum_buffer( m_group_sum_buffer );
        create_group_sum_buffer( m_scanned_group_sum_buffer );
    }

    void record_dispatch(
        ID3D12GraphicsCommandList* in_cmd_list,
        ID3D12Resource* in_inst_buffer,
        ID3D12Resource* out_is_inst_alive_buffer,
        ID3D12Resource* out_inst_newpos_buffer,
        ID3D12Resource* out_proccessed_inst_buffer,
        ID3D12Resource* out_command_buffer,
        UINT in_num_inst,
        DirectX::XMMATRIX in_vp_no_transpose
    )
    {
        auto fill_cb = [in_num_inst](CB &cb)
            {
                cb = {};
                UINT noofGroups = get_padded_size(in_num_inst) / (2 * NUM_THREADS_OF_SCAN_GROUP);
                noofGroups = (UINT) pow( 2, floor( log( noofGroups ) / log( 2 ) ) + 1 );
                cb.NoofGroups = noofGroups;
            };

        auto dispatch_kill_instance_pass = [in_cmd_list, this, in_num_inst, in_vp_no_transpose](
            auto in_inst_buffer,
            auto out_is_inst_alive_buffer,
            auto out_command_buffer)
            {
                in_cmd_list->SetPipelineState( m_pso_kill_instance_pass.Get() );
                in_cmd_list->SetComputeRootSignature( m_rs_kill_instance_pass.Get() );

                // Set int4 numInstance (use only x)
                in_cmd_list->SetComputeRoot32BitConstant( 0, in_num_inst, 0 );
                in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 1 );
                in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 2 );
                in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 3 );

                // Set float4x4 vp
                DirectX::XMFLOAT4X4 vp_data;
                DirectX::XMStoreFloat4x4( &vp_data, XMMatrixTranspose( in_vp_no_transpose ) );
                in_cmd_list->SetComputeRoot32BitConstants( 0, 16, &vp_data, 4 );

                in_cmd_list->SetComputeRootShaderResourceView( 1, in_inst_buffer );
                in_cmd_list->SetComputeRootUnorderedAccessView( 2, out_is_inst_alive_buffer );
                in_cmd_list->SetComputeRootUnorderedAccessView( 3, out_command_buffer );
                int num_groups = (in_num_inst - 1) / NUM_THREADS_OF_KILL_INSTANCE + 1;
                in_cmd_list->Dispatch( num_groups, 1, 1 );
            };

        auto dispatch_scan_prefix = [in_cmd_list, this, in_num_inst](
            auto in_is_inst_alive_buffer,
            auto out_inst_newpos_buffer,
            auto out_group_sum_buffer )
            {
                in_cmd_list->SetPipelineState( m_pso_scan_prefix_pass.Get() );
                in_cmd_list->SetComputeRootSignature( m_rs_scan_prefix_pass.Get() );

                // slot 0: constant <--> cbuffer OcclusionPassCB, b1
                in_cmd_list->SetComputeRoot32BitConstants( 0, 12, &m_cb, 0 );

                // slot 1: root SRV <--> Buffer<bool> instancePredicatesIn, t0
                in_cmd_list->SetComputeRootShaderResourceView( 1, in_is_inst_alive_buffer );

                // slot 2: root UAV <--> RWBuffer<uint> groupSumArray, u0
                in_cmd_list->SetComputeRootUnorderedAccessView( 2, out_group_sum_buffer );

                // slot 3: root UAV <--> RWBuffer<uint> scannedInstancePredicates, u1
                in_cmd_list->SetComputeRootUnorderedAccessView( 3, out_inst_newpos_buffer );

                unsigned groupX = (in_num_inst / 2 / NUM_THREADS_OF_SCAN_PREFIX) + 1;

                in_cmd_list->Dispatch( groupX, 1, 1 );
            };

        auto dispatch_scan_group = [in_cmd_list, this, in_num_inst](
            auto in_group_sum_buffer,
            auto out_group_sum_buffer )
            {
                in_cmd_list->SetPipelineState( m_pso_scan_group_pass.Get() );
                in_cmd_list->SetComputeRootSignature( m_rs_scan_group_pass.Get() );

                // slot 0: constant <--> cbuffer OcclusionPassCB, b1
                in_cmd_list->SetComputeRoot32BitConstants( 0, 12, &m_cb, 0 );

                // slot 1: root SRV <--> Buffer<bool> instancePredicatesIn, t0
                in_cmd_list->SetComputeRootShaderResourceView( 1, in_group_sum_buffer );

                // slot 2: root UAV <--> RWBuffer<uint> groupSumArray, u0
                in_cmd_list->SetComputeRootUnorderedAccessView( 2, out_group_sum_buffer );

                in_cmd_list->Dispatch( 1, 1, 1 );
            };

        auto dispatch_copy_instance_pass = [in_cmd_list, this, in_num_inst](
            auto in_inst_buffer,
            auto in_is_inst_alive_buffer,
            auto in_group_sum_buffer,
            auto in_inst_newpos_buffer,
            auto out_proccessed_inst_buffer)
            {
                in_cmd_list->SetPipelineState( m_pso_copy_instance_pass.Get() );
                in_cmd_list->SetComputeRootSignature( m_rs_copy_instance_pass.Get() );

                // slot 0: constant <--> cbuffer OcclusionPassCB, b1
                in_cmd_list->SetComputeRoot32BitConstants( 0, 12, &m_cb, 0 );

                // slot 1: root SRV <--> StructuredBuffer<Instance> instanceDataIn, t0
                in_cmd_list->SetComputeRootShaderResourceView( 1, in_inst_buffer );

                // slot 2: root SRV <--> StructuredBuffer<my_uint> instancePredicatesIn, t1
                in_cmd_list->SetComputeRootShaderResourceView( 2, in_is_inst_alive_buffer );

                // slot 3: root SRV <--> StructuredBuffer<my_uint> groupSumArray, t2
                in_cmd_list->SetComputeRootShaderResourceView( 3, in_group_sum_buffer );

                // slot 4: root SRV <--> StructuredBuffer<my_uint> scannedInstancePredicates, t3
                in_cmd_list->SetComputeRootShaderResourceView( 4, in_inst_newpos_buffer );

                // slot 5: root UAV <--> RWStructuredBuffer<Instance> instanceDataOut, u0
                in_cmd_list->SetComputeRootUnorderedAccessView( 5, out_proccessed_inst_buffer );
                
                unsigned groupX = (in_num_inst / NUM_THREADS_OF_COPY_INSTANCE_DATA) + 1;
                in_cmd_list->Dispatch( groupX, 1, 1 );
            };

        auto clear_buffer = [in_cmd_list]( auto in_res )
            {
                D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
                    in_res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST );
                in_cmd_list->ResourceBarrier( 1, &b );
                in_cmd_list->ClearUnorderedAccessViewUint( in_res, 0, 0, nullptr );
                b = CD3DX12_RESOURCE_BARRIER::Transition(
                    in_res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                in_cmd_list->ResourceBarrier( 1, &b );
            };

        auto insert_barrier_srv_to_uav = [in_cmd_list]( auto in_res )
            {
                CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
                    in_res, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                in_cmd_list->ResourceBarrier( 1, &b );
            };
        auto insert_barrier_uav_to_srv = [in_cmd_list]( auto in_res )
            {
                CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
                    in_res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                in_cmd_list->ResourceBarrier( 1, &b );
            };

        fill_cb(m_cb);


        insert_barrier_srv_to_uav( out_is_inst_alive_buffer );
        insert_barrier_srv_to_uav( out_command_buffer );
        dispatch_kill_instance_pass(
            in_inst_buffer->GetGPUVirtualAddress(),
            out_is_inst_alive_buffer->GetGPUVirtualAddress(),
            out_command_buffer->GetGPUVirtualAddress());
        insert_barrier_uav_to_srv( out_command_buffer );


        insert_barrier_uav_to_srv( out_is_inst_alive_buffer );
        insert_barrier_srv_to_uav( m_group_sum_buffer.Get() );
        dispatch_scan_prefix(
            out_is_inst_alive_buffer->GetGPUVirtualAddress(),
            out_inst_newpos_buffer->GetGPUVirtualAddress(),
            m_group_sum_buffer->GetGPUVirtualAddress() );

        insert_barrier_uav_to_srv( m_group_sum_buffer.Get() );
        insert_barrier_srv_to_uav( m_scanned_group_sum_buffer.Get() );
        dispatch_scan_group(
            m_group_sum_buffer->GetGPUVirtualAddress(),
            m_scanned_group_sum_buffer->GetGPUVirtualAddress() );

        insert_barrier_uav_to_srv( m_scanned_group_sum_buffer.Get() );
        insert_barrier_uav_to_srv( out_inst_newpos_buffer );
        dispatch_copy_instance_pass(
            in_inst_buffer->GetGPUVirtualAddress(),
            out_is_inst_alive_buffer->GetGPUVirtualAddress(),
            m_scanned_group_sum_buffer->GetGPUVirtualAddress(),
            out_inst_newpos_buffer->GetGPUVirtualAddress(),
            out_proccessed_inst_buffer->GetGPUVirtualAddress()
        );

        insert_barrier_uav_to_srv( out_proccessed_inst_buffer );
    }
};