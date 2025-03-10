#pragma once
#include "d3dx12.h"

// Data structure to match the command signature used for ExecuteIndirect.
// command per mesh : VBV, VBV, IBV, RootCBV, DrawInstanced
struct IndirectCommand
{
    D3D12_VERTEX_BUFFER_VIEW vbv0;
    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_INDEX_BUFFER_VIEW ibv;
    D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddr;
    D3D12_DRAW_INDEXED_ARGUMENTS drawIndexedArgs;
};

template<size_t MAX_NUM_TEXTURES>
struct GraphicsPass
{
    // Graphics root signature parameter offsets.
    enum GraphicsRootParameters
    {
        Cbv,
        GraphicsRootParametersCount
    };


    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12CommandSignature> m_commandSignature;

    void Init(
        ID3D12Device* in_device,
        std::wstring in_assetPath
    )
    {
        // Create Root signature
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

            if ( FAILED( in_device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof( featureData ) ) ) )
            {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }

            // Graphics root signature
            /*
                Cbv: CBV(b0)
            */
            CD3DX12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount] = {};
            rootParameters[Cbv].InitAsConstantBufferView( 0, 0 );

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.MipLODBias = 0;
            sampler.MaxAnisotropy = 0;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD = 0.0f;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = 0;
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1( _countof( rootParameters ), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED );

            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
            ThrowIfFailed( in_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) );
            NAME_D3D12_OBJECT( m_rootSignature );
        }

        // Create the pipeline state, which includes compiling and loading shaders.
        {
            std::wstring c_vsFilename = in_assetPath + L"VS.cso";
            std::wstring c_psFilename = in_assetPath + L"PS.cso";
            std::wstring c_csFilename = in_assetPath + L"CS.cso";

            struct
            {
                byte* data;
                uint32_t size;
            } vshader, pshader, cshader;

            ReadDataFromFile( c_vsFilename.c_str(), &vshader.data, &vshader.size );
            ReadDataFromFile( c_psFilename.c_str(), &pshader.data, &pshader.size );
            ReadDataFromFile( c_csFilename.c_str(), &cshader.data, &cshader.size );

#if defined(_DEBUG)
            // Enable better shader debugging with the graphics debugging tools.
            UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT compileFlags = 0;
#endif

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                // Vertex Buffer
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

                // Instance Buffer
                { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                { "MATERIAL_IDX", 0, DXGI_FORMAT_R32G32B32A32_SINT, 1, 64, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
            };

            // Describe and create the graphics pipeline state objects (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
            psoDesc.pRootSignature = m_rootSignature.Get();
            psoDesc.VS = { vshader.data, vshader.size };
            psoDesc.PS = { pshader.data, pshader.size };
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            ThrowIfFailed( in_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
            NAME_D3D12_OBJECT( m_pipelineState );
        }

        // Create Command signature
        {
            // Each command consists of a CBV update and a DrawInstanced call.
            D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[5] = {};
            argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            argumentDescs[0].VertexBuffer.Slot = 0;
            argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            argumentDescs[1].VertexBuffer.Slot = 1;
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
            argumentDescs[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
            argumentDescs[3].ConstantBufferView.RootParameterIndex = GraphicsPass<0>::Cbv;   // 0
            argumentDescs[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof( argumentDescs );
            commandSignatureDesc.ByteStride = sizeof( IndirectCommand );

            ThrowIfFailed( in_device->CreateCommandSignature( &commandSignatureDesc, m_rootSignature.Get(), IID_PPV_ARGS( &m_commandSignature ) ) );
        }
    }

    void SetBeforeDraw(
        ID3D12GraphicsCommandList* in_commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        UINT width, UINT height, int leftOrRight,
        int doClear = 1
    )
    {
        {
            in_commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &dsvHandle );
            const float clearColor[] = { 0.3f, 0.2f, 0.4f, 1.0f };

            if ( doClear )
            {
                in_commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
                in_commandList->ClearDepthStencilView( dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
            }

            // Set Pipeline state.
            {
                in_commandList->SetPipelineState( m_pipelineState.Get() );
                in_commandList->SetGraphicsRootSignature( m_rootSignature.Get() );
            }

            // Set IA
            {
                in_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            }

            // Set RS
            {
                CD3DX12_VIEWPORT viewport;
                CD3DX12_RECT scissorRect;
                if ( leftOrRight == 0 )
                {
                    viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) );
                    scissorRect = CD3DX12_RECT( 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) );
                }
                else if ( leftOrRight < 0 )
                {     // lefts
                    viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>(width / 2), static_cast<float>(height) );
                    scissorRect = CD3DX12_RECT( 0, 0, static_cast<LONG>(width / 2), static_cast<LONG>(height) );
                }
                else
                {     // right
                    viewport = CD3DX12_VIEWPORT( static_cast<float>(width / 2), 0.0f, static_cast<float>(width / 2), static_cast<float>(height) );
                    scissorRect = CD3DX12_RECT( static_cast<LONG>(width / 2), 0, static_cast<LONG>(width), static_cast<LONG>(height) );
                }

                in_commandList->RSSetViewports( 1, &viewport );
                in_commandList->RSSetScissorRects( 1, &scissorRect );
            }
        }
    }

    void Draw(
        ID3D12GraphicsCommandList* in_commandList,
        int in_numMeshes,
        ID3D12Resource* in_commandBuffer
        )
    {
        in_commandList->ExecuteIndirect(
            m_commandSignature.Get(),
            in_numMeshes,
            in_commandBuffer,
            0,
            nullptr,
            0
        );
    }
};