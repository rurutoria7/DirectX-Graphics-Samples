#pragma once
#include "d3dx12.h"

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

    void Init(
        ID3D12Device* m_device,
        std::wstring m_assetPath
    )
    {
        // Create Root signature
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;

            if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }

            // Graphics root signature
            /*
                Cbv: CBV(b0)
            */
            CD3DX12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount] = {};
            rootParameters[Cbv].InitAsConstantBufferView(0, 0);

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
            rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
            ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
            NAME_D3D12_OBJECT(m_rootSignature);
        }
        
        // Create the pipeline state, which includes compiling and loading shaders.
        {
            std::wstring c_vsFilename = m_assetPath + L"VS.cso";
            std::wstring c_psFilename = m_assetPath + L"PS.cso";
            std::wstring c_csFilename = m_assetPath + L"CS.cso";

            struct
            {
                byte* data;
                uint32_t size;
            } vshader, pshader, cshader;

            ReadDataFromFile(c_vsFilename.c_str(), &vshader.data, &vshader.size);
            ReadDataFromFile(c_psFilename.c_str(), &pshader.data, &pshader.size);
            ReadDataFromFile(c_csFilename.c_str(), &cshader.data, &cshader.size);

#if defined(_DEBUG)
            // Enable better shader debugging with the graphics debugging tools.
            UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT compileFlags = 0;
#endif

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };

            // Describe and create the graphics pipeline state objects (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
            psoDesc.pRootSignature = m_rootSignature.Get();
            psoDesc.VS = { vshader.data, vshader.size };
            psoDesc.PS = { pshader.data, pshader.size };
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
            NAME_D3D12_OBJECT(m_pipelineState);
        }
    }

    void Execute(
        ID3D12GraphicsCommandList* in_commandList,
        D3D12_VERTEX_BUFFER_VIEW in_vertexBufferView,
        D3D12_INDEX_BUFFER_VIEW in_indexBufferView,
        D3D12_GPU_VIRTUAL_ADDRESS in_constantBuffer,
        UINT width, UINT height,
        UINT numIndices,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle
    )
        // Record the rendering commands.
    {
        in_commandList->SetPipelineState(m_pipelineState.Get());
        // Set Root signature.
        in_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        // Set CBV
        //{
        //    in_commandList->SetGraphicsRootConstantBufferView(Cbv, in_cbvHandle);
        //}

        // Set RS
        {
            auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
            auto scissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));
            in_commandList->RSSetViewports(1, &viewport);
            in_commandList->RSSetScissorRects(1, &scissorRect);
        }

        // Set OM
        in_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        const float clearColor[] = { 0.3f, 0.2f, 0.4f, 1.0f };
        in_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        in_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Set IA
        {
            in_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            in_commandList->IASetVertexBuffers(0, 1, &in_vertexBufferView);
            in_commandList->IASetIndexBuffer(&in_indexBufferView);
        }

        // Set Root Parameters
        {
            in_commandList->SetGraphicsRootConstantBufferView(Cbv, in_constantBuffer);
        }

        // Draw
        in_commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
    }
};