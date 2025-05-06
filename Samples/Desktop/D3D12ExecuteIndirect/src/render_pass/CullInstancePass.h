#pragma once
#include "../defines.h"
#include <DirectXMath.h>
#include "d3dx12.h"
#include "../DXSampleHelper.h"
#include <iostream>
#include "../ResourceStateTracker.h"

#ifdef GR_WORKGRAPH
class WorkGraphContext
{
public:
    void Init(ID3D12Device* in_device, ComPtr<ID3D12StateObject> spSO, LPCWSTR pWorkGraphName, ResourceStateTracker& in_state_tracker)
    {
        auto make_buffer = [in_device]( ComPtr<ID3D12Resource>& spBuffer, UINT64 size, D3D12_RESOURCE_FLAGS flags )
        {
            CD3DX12_HEAP_PROPERTIES heapProps( D3D12_HEAP_TYPE_DEFAULT );
            CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( size, flags );
            ThrowIfFailed( in_device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &spBuffer ) ) );
        };

        ComPtr<ID3D12StateObjectProperties1> spSOProps;
        // spSOProps = spSO;
        spSO.As(&spSOProps);
        hWorkGraph = spSOProps->GetProgramIdentifier(pWorkGraphName);
        ComPtr<ID3D12WorkGraphProperties> spWGProps;
        spSO.As(&spWGProps);
        UINT WorkGraphIndex = spWGProps->GetWorkGraphIndex(pWorkGraphName);
        spWGProps->GetWorkGraphMemoryRequirements(WorkGraphIndex, &MemReqs);
        BackingMemory.SizeInBytes = MemReqs.MaxSizeInBytes;
        make_buffer(spBackingMemory, BackingMemory.SizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        BackingMemory.StartAddress = spBackingMemory->GetGPUVirtualAddress();
        in_state_tracker.TrackResourceState(spBackingMemory.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    ComPtr<ID3D12Resource> spBackingMemory;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE BackingMemory = {};
    D3D12_PROGRAM_IDENTIFIER hWorkGraph = {};
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS MemReqs = {};
};
#endif

template<size_t MAX_NUM_MESHES>
struct CullInstancePass
{
    struct MyBuffer {
        ComPtr<ID3D12Resource> buffer;
        D3D12_RESOURCE_DESC desc;
    };

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

    struct ClearBufferCB {
        unsigned int scanned_group_sum_buffer_noof_elements;
        unsigned int group_sum_buffer_noof_elements;
        unsigned int inst_newpos_buffer_noof_elements;
        unsigned int is_inst_alive_buffer_noof_elements;
        unsigned int command_buffer_noof_elements;
        unsigned int padding[3];  // Added padding to align to 32-byte boundary
    } m_clear_buffer_cb;

    static const int NUM_THREADS_OF_CLEAR_BUFFER = 64;
    static const int SCAN_BLOCK = 128;
    static const int NUM_SCAN_BLOCK = 2048;
    static const int NUM_THREADS_OF_KILL_INSTANCE = SCAN_BLOCK;
    static const int NUM_THREADS_OF_SCAN_PREFIX = SCAN_BLOCK / 2;
    static const int NUM_THREADS_OF_COPY_INSTANCE_DATA = SCAN_BLOCK;

    ComPtr<ID3D12RootSignature> m_rs_clear_buffer_pass;
    ComPtr<ID3D12PipelineState> m_pso_clear_buffer_pass;

    ComPtr<ID3D12RootSignature> m_rs_kill_instance_pass;
    ComPtr<ID3D12PipelineState> m_pso_kill_instance_pass;

    ComPtr<ID3D12RootSignature> m_rs_scan_prefix_pass;
    ComPtr<ID3D12PipelineState> m_pso_scan_prefix_pass;

    ComPtr<ID3D12RootSignature> m_rs_scan_group_pass;
    ComPtr<ID3D12PipelineState> m_pso_scan_group_pass;

    ComPtr<ID3D12RootSignature> m_rs_copy_instance_pass;
    ComPtr<ID3D12PipelineState> m_pso_copy_instance_pass;

    MyBuffer m_scanned_group_sum_buffer;
    MyBuffer m_group_sum_buffer;
    MyBuffer m_is_inst_alive_buffer;
    MyBuffer m_inst_newpos_buffer;

    ComPtr<ID3D12RootSignature> m_rs_scan_command_pass;
    ComPtr<ID3D12PipelineState> m_pso_scan_command_pass;

#ifdef GR_WORKGRAPH
    ComPtr<ID3D12StateObject> spSO;
    ComPtr<ID3D12RootSignature> spRS;
    WorkGraphContext WG;
#endif

    static constexpr uint32_t ceil_div( uint32_t x, uint32_t y ) { return (x + y - 1) / y; }

    static uint32_t get_padded_size( uint32_t instance_count )
    {
        uint32_t res = (instance_count / 2 / NUM_THREADS_OF_SCAN_PREFIX + 1) * NUM_THREADS_OF_SCAN_PREFIX * 2;
        res = max( res, static_cast<uint32_t>(4096) );
        return res;
    }

    void init(
        ID3D12Device14* in_device,
        std::wstring in_asset_path,
        unsigned in_num_inst,
        ResourceStateTracker& in_state_tracker )
    {
        auto create_pso_rs = [in_device, in_asset_path](
            std::wstring in_shaderPath, ComPtr<ID3D12RootSignature>& out_rs, ComPtr<ID3D12PipelineState>& out_pso )
            {
                std::wstring c_csFilename = in_asset_path + in_shaderPath;
                struct
                {
                    byte* data;
                    uint32_t size;
                } cshader;
                ThrowIfFailed( ReadDataFromFile( c_csFilename.c_str(), &cshader.data, &cshader.size ) );

                ThrowIfFailed( in_device->CreateRootSignature( 0, cshader.data, cshader.size, IID_PPV_ARGS( &out_rs ) ) );

                D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
                computePsoDesc.pRootSignature = out_rs.Get();
                computePsoDesc.CS = { cshader.data, cshader.size };
                ThrowIfFailed( in_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &out_pso ) ) );
            };
        auto create_buffer = [in_device, in_num_inst, &in_state_tracker]( auto& res )
            {
                auto buffer_size = get_padded_size( in_num_inst ) * sizeof( unsigned );
                auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                res.desc = CD3DX12_RESOURCE_DESC::Buffer( buffer_size, flags );

                ThrowIfFailed( in_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                    D3D12_HEAP_FLAG_NONE,
                    &res.desc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS( &res.buffer ) ) );
                NAME_D3D12_OBJECT( res.buffer );
                in_state_tracker.TrackResourceState( res.buffer.Get(), D3D12_RESOURCE_STATE_COMMON );
            };

#ifdef GR_WORKGRAPH
        auto wg_init = [in_device, in_asset_path, &in_state_tracker, this](){
            CD3DX12_STATE_OBJECT_DESC SO(D3D12_STATE_OBJECT_TYPE_EXECUTABLE);
            auto pLib = SO.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

            std::wstring c_csFilename = in_asset_path + L"Dummy_wg.cso";
            struct
            {
                byte* data;
                uint32_t size;
            } cshader;
            ThrowIfFailed( ReadDataFromFile( c_csFilename.c_str(), &cshader.data, &cshader.size ) );

            CD3DX12_SHADER_BYTECODE libCode;
            libCode = { cshader.data, cshader.size };
            pLib->SetDXILLibrary(&libCode);
            ThrowIfFailed(in_device->CreateRootSignatureFromSubobjectInLibrary(0, libCode.pShaderBytecode, libCode.BytecodeLength, L"globalRS", IID_PPV_ARGS(&spRS)));

            auto pWG = SO.CreateSubobject<CD3DX12_WORK_GRAPH_SUBOBJECT>();
            pWG->IncludeAllAvailableNodes(); // Auto populate the graph
            LPCWSTR workGraphName = L"HelloWorkGraphs";
            pWG->SetProgramName(workGraphName);

            ThrowIfFailed(in_device->CreateStateObject(SO, IID_PPV_ARGS(&spSO)));
            WG.Init(in_device, spSO, workGraphName, in_state_tracker);
        };
        wg_init();
#endif

        create_pso_rs( L"ClearBufferCS.cso", m_rs_clear_buffer_pass, m_pso_clear_buffer_pass );
        create_pso_rs( L"KillInstancesCS.cso", m_rs_kill_instance_pass, m_pso_kill_instance_pass );
        create_pso_rs( L"ScanInstancesCS.cso", m_rs_scan_prefix_pass, m_pso_scan_prefix_pass );
        create_pso_rs( L"ScanGroupsCS.cso", m_rs_scan_group_pass, m_pso_scan_group_pass );
        create_pso_rs( L"CopyInstancesCS.cso", m_rs_copy_instance_pass, m_pso_copy_instance_pass );
        create_pso_rs( L"ScanCommandsCS.cso", m_rs_scan_command_pass, m_pso_scan_command_pass );

        create_buffer( m_group_sum_buffer );
        create_buffer( m_scanned_group_sum_buffer );
        create_buffer( m_inst_newpos_buffer );
        create_buffer( m_is_inst_alive_buffer );
    }

    struct RecordDispatchParams
    {
        ID3D12GraphicsCommandList10* cmd_list;
        ID3D12Resource* inst_buffer;
        ID3D12Resource* processed_inst_buffer;
        ID3D12Resource* command_buffer;
        UINT num_inst;
        UINT num_meshes;
        DirectX::XMMATRIX vp_no_transpose;
    };
    void record_dispatch( const RecordDispatchParams& in_params, ResourceStateTracker& in_state_tracker )
    {
        auto& in_cmd_list = in_params.cmd_list;
        auto& in_inst_buffer = in_params.inst_buffer;
        auto& out_proccessed_inst_buffer = in_params.processed_inst_buffer;
        auto& out_command_buffer = in_params.command_buffer;
        auto& in_num_inst = in_params.num_inst;
        auto& in_num_meshes = in_params.num_meshes;
        auto& in_vp_no_transpose = in_params.vp_no_transpose;

        auto fill_cb = [in_num_inst, in_num_meshes](CB& cb){
            cb = {};
            UINT noofGroups = get_padded_size( in_num_inst ) / (NUM_SCAN_BLOCK);
            noofGroups = (UINT) pow( 2, floor( log( noofGroups ) / log( 2 ) ) + 1 );
            cb.NoofGroups = noofGroups;
            cb.NoofDrawcalls = in_num_meshes;
        };
        auto fill_clear_buffer_cb = [this, in_num_meshes](ClearBufferCB& cb) {
            cb.scanned_group_sum_buffer_noof_elements = m_scanned_group_sum_buffer.desc.Width / sizeof( unsigned );
            cb.group_sum_buffer_noof_elements = m_group_sum_buffer.desc.Width / sizeof( unsigned );
            cb.inst_newpos_buffer_noof_elements = m_inst_newpos_buffer.desc.Width / sizeof( unsigned );
            cb.is_inst_alive_buffer_noof_elements = m_is_inst_alive_buffer.desc.Width / sizeof( unsigned );
            cb.command_buffer_noof_elements = MAX_NUM_MESHES;
        };
        auto dispatch_kill_instance_pass = [in_cmd_list, this, in_num_inst, in_num_meshes, in_vp_no_transpose](
            auto in_inst_buffer,
            auto out_is_inst_alive_buffer,
            auto out_command_buffer )
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

                // Set numMeshes
                in_cmd_list->SetComputeRoot32BitConstant( 0, in_num_meshes, 20 );

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
            auto out_proccessed_inst_buffer )
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

        auto dispatch_scan_command_pass = [in_cmd_list, this, in_num_meshes](
            auto inout_command_buffer )
            {
                in_cmd_list->SetPipelineState(m_pso_scan_command_pass.Get());
                in_cmd_list->SetComputeRootSignature(m_rs_scan_command_pass.Get());

                // slot 0: constant <--> cbuffer OcclusionPassCB, b1
                in_cmd_list->SetComputeRoot32BitConstants( 0, 12, &m_cb, 0 );

                // slot 1: root UAV <--> RWStructuredBuffer<IndirectCommand> inoutCommands, u0
                in_cmd_list->SetComputeRootUnorderedAccessView( 1, inout_command_buffer );

                in_cmd_list->Dispatch(1, 1, 1);
            };

        auto dispatch_clear_buffer_pass = [in_cmd_list, this](
            auto inst_buffer,
            auto is_inst_alive_buffer,
            auto inst_newpos_buffer,
            auto processed_inst_buffer,
            auto command_buffer ){

            in_cmd_list->SetPipelineState( m_pso_clear_buffer_pass.Get() );
            in_cmd_list->SetComputeRootSignature( m_rs_clear_buffer_pass.Get() );

            // slot 0: constant <--> cbuffer OcclusionPassCB, b1
            in_cmd_list->SetComputeRoot32BitConstants( 0, sizeof(ClearBufferCB)/sizeof(unsigned int), &m_clear_buffer_cb, 0 );

            // slot 1: root UAV <--> RWStructuredBuffer<Instance> instanceDataIn, u0
            in_cmd_list->SetComputeRootUnorderedAccessView( 1, inst_buffer );

            // slot 2: root UAV <--> RWStructuredBuffer<bool> instancePredicatesIn, u1
            in_cmd_list->SetComputeRootUnorderedAccessView( 2, is_inst_alive_buffer );

            // slot 3: root UAV <--> RWStructuredBuffer<my_uint> groupSumArray, u2
            in_cmd_list->SetComputeRootUnorderedAccessView( 3, inst_newpos_buffer );

            // slot 4: root UAV <--> RWStructuredBuffer<my_uint> scannedInstancePredicates, u3
            in_cmd_list->SetComputeRootUnorderedAccessView( 4, processed_inst_buffer );

            // slot 5: root UAV
            in_cmd_list->SetComputeRootUnorderedAccessView( 5, command_buffer );

            unsigned groupX = 1 +  m_clear_buffer_cb.scanned_group_sum_buffer_noof_elements / NUM_THREADS_OF_CLEAR_BUFFER;
            in_cmd_list->Dispatch( groupX, 1, 1 );
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

#ifdef GR_WORKGRAPH
        auto wg_dispatch_kill_instance_pass = [in_num_inst, in_cmd_list, this, in_num_meshes, in_vp_no_transpose](
            auto in_inst_buffer,
            auto out_is_inst_alive_buffer,
            auto out_command_buffer ){

            in_cmd_list->SetComputeRootSignature(spRS.Get());

            in_cmd_list->SetComputeRoot32BitConstant( 0, in_num_inst, 0 );
            in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 1 );
            in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 2 );
            in_cmd_list->SetComputeRoot32BitConstant( 0, 0, 3 );
            DirectX::XMFLOAT4X4 vp_data;
            DirectX::XMStoreFloat4x4( &vp_data, XMMatrixTranspose( in_vp_no_transpose ) );
            in_cmd_list->SetComputeRoot32BitConstants( 0, 16, &vp_data, 4 );
            in_cmd_list->SetComputeRoot32BitConstant( 0, in_num_meshes, 20 );

            in_cmd_list->SetComputeRootShaderResourceView(1, in_inst_buffer);
            in_cmd_list->SetComputeRootUnorderedAccessView(2, out_is_inst_alive_buffer);
            in_cmd_list->SetComputeRootUnorderedAccessView(3, out_command_buffer);

            D3D12_SET_PROGRAM_DESC setProg = {};
            setProg.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
            setProg.WorkGraph.ProgramIdentifier = WG.hWorkGraph;
            setProg.WorkGraph.Flags = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
            setProg.WorkGraph.BackingMemory = WG.BackingMemory;
            in_cmd_list->SetProgram(&setProg);

            // Prepare input data for the graph
            struct entryRecord {
                UINT pad;
            };

            entryRecord inputData = {};

            // Dispatch the graph
            D3D12_DISPATCH_GRAPH_DESC DSDesc = {};
            DSDesc.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
            DSDesc.NodeCPUInput.EntrypointIndex = 0;
            DSDesc.NodeCPUInput.NumRecords = 1;
            DSDesc.NodeCPUInput.RecordStrideInBytes = sizeof(entryRecord);
            DSDesc.NodeCPUInput.pRecords = &inputData;
            in_cmd_list->DispatchGraph(&DSDesc);
        };
#endif            
        
        fill_clear_buffer_cb( m_clear_buffer_cb );
        fill_cb( m_cb );

        in_state_tracker.Transition(m_scanned_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        in_state_tracker.Transition(m_is_inst_alive_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        in_state_tracker.Transition(m_inst_newpos_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        in_state_tracker.Transition(m_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        in_state_tracker.Transition(out_command_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        in_state_tracker.FlushBarriers(in_cmd_list);
        dispatch_clear_buffer_pass(
            m_scanned_group_sum_buffer.buffer->GetGPUVirtualAddress(),
            m_is_inst_alive_buffer.buffer->GetGPUVirtualAddress(), 
            m_inst_newpos_buffer.buffer->GetGPUVirtualAddress(),
            m_group_sum_buffer.buffer->GetGPUVirtualAddress(),
            out_command_buffer->GetGPUVirtualAddress());

        in_state_tracker.Transition( in_inst_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        in_state_tracker.Transition( m_is_inst_alive_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.Transition( out_command_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.FlushBarriers( in_cmd_list );

#ifdef GR_WORKGRAPH
        wg_dispatch_kill_instance_pass(
            in_inst_buffer->GetGPUVirtualAddress(),
            m_is_inst_alive_buffer.buffer->GetGPUVirtualAddress(),
            out_command_buffer->GetGPUVirtualAddress() );
#else
        dispatch_kill_instance_pass(
            in_inst_buffer->GetGPUVirtualAddress(),
            m_is_inst_alive_buffer.buffer->GetGPUVirtualAddress(),
            out_command_buffer->GetGPUVirtualAddress() );
#endif

        in_state_tracker.Transition( m_is_inst_alive_buffer.buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        in_state_tracker.Transition( m_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.Transition( m_inst_newpos_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.FlushBarriers( in_cmd_list );
        dispatch_scan_prefix(
            m_is_inst_alive_buffer.buffer->GetGPUVirtualAddress(),
            m_inst_newpos_buffer.buffer->GetGPUVirtualAddress(),
            m_group_sum_buffer.buffer->GetGPUVirtualAddress() );

        in_state_tracker.Transition( m_scanned_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.Transition( m_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        in_state_tracker.FlushBarriers( in_cmd_list );
        dispatch_scan_group(
            m_group_sum_buffer.buffer->GetGPUVirtualAddress(),
            m_scanned_group_sum_buffer.buffer->GetGPUVirtualAddress() );

        in_state_tracker.Transition( m_scanned_group_sum_buffer.buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        in_state_tracker.Transition( m_inst_newpos_buffer.buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        in_state_tracker.Transition( out_proccessed_inst_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        in_state_tracker.FlushBarriers( in_cmd_list );
        dispatch_copy_instance_pass(
            in_inst_buffer->GetGPUVirtualAddress(),
            m_is_inst_alive_buffer.buffer->GetGPUVirtualAddress(),
            m_scanned_group_sum_buffer.buffer->GetGPUVirtualAddress(),
            m_inst_newpos_buffer.buffer->GetGPUVirtualAddress(),
            out_proccessed_inst_buffer->GetGPUVirtualAddress());

        in_state_tracker.Transition( out_proccessed_inst_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        dispatch_scan_command_pass( out_command_buffer->GetGPUVirtualAddress() );
    }
};