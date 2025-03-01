//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12ExecuteIndirect.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define IMPLEMENT_FBXLOADER
#include "MyMesh.h"


const UINT D3D12ExecuteIndirect::CommandSizePerFrame = MaxNumMeshes * sizeof(IndirectCommand);
const UINT D3D12ExecuteIndirect::CommandBufferCounterOffset = AlignForUavCounter(D3D12ExecuteIndirect::CommandSizePerFrame);
const float D3D12ExecuteIndirect::TriangleHalfWidth = 0.05f;
const float D3D12ExecuteIndirect::TriangleDepth = 1.0f;
const float D3D12ExecuteIndirect::CullingCutoff = 0.5f;

D3D12ExecuteIndirect::D3D12ExecuteIndirect(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_cullingScissorRect(),
    m_rtvDescriptorSize(0),
    m_cbvSrvUavDescriptorSize(0),
    m_csRootConstants(),
    m_enableCulling(true),
    m_fenceValues{},
    m_fbxDirName("D:\\LocalFiles\\2024-Winter\\D3D\\DirectX-Graphics-Samples\\Samples\\Desktop\\D3D12ExecuteIndirect\\src\\Assets\\Meshes\\Buildings\\"),
    m_fbxFilename("building.fbx"),
    m_fovy(XM_PI / 3)
{
    m_constantBufferData.resize(MaxNumMeshes * FrameCount);

    m_csRootConstants.xOffset = TriangleHalfWidth;
    m_csRootConstants.zOffset = TriangleDepth;
    m_csRootConstants.cullOffset = CullingCutoff;
    m_csRootConstants.commandCount = MaxNumMeshes;

    float center = width / 2.0f;
    m_cullingScissorRect.left = static_cast<LONG>(center - (center * CullingCutoff));
    m_cullingScissorRect.right = static_cast<LONG>(center + (center * CullingCutoff));
    m_cullingScissorRect.bottom = static_cast<LONG>(height);

    ThrowIfFailed(DXGIDeclareAdapterRemovalSupport());
}

void D3D12ExecuteIndirect::OnInit()
{
    LoadPipeline();
    LoadAssets();
    m_graphicsPass.Init(m_device.Get(), GetAssetFullPath(L""));
    m_mainCam.Init({ 0, 15, 40 }, false);
    m_mainCam.SetMoveSpeed(250.0f);
    m_debugCam.Init({ 0, 15, 40 }, true);
    m_debugCam.SetMoveSpeed(400.0f);
}

// Load the rendering pipeline dependencies.
void D3D12ExecuteIndirect::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }

    // Describe and create the command queues.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    NAME_D3D12_OBJECT(m_commandQueue);

    D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
    computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    ThrowIfFailed(m_device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&m_computeCommandQueue)));
    NAME_D3D12_OBJECT(m_computeCommandQueue);

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        // Describe and create a constant buffer view (CBV), Shader resource
        // view (SRV), and unordered access view (UAV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
        cbvSrvUavHeapDesc.NumDescriptors = CbvSrvUavDescriptorCountPerFrame * FrameCount;
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE | D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap)));
        NAME_D3D12_OBJECT(m_cbvSrvUavHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and command allocators for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeCommandAllocators[n])));
        }
    }
}

// Load the sample assets.
void D3D12ExecuteIndirect::LoadAssets()
{
    // Load FBX.
    {
        m_fbxLoader.LoadFBX(m_fbxDirName + m_fbxFilename);
    }

    // Create the command list.
    {
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_computeCommandList)));
        ThrowIfFailed(m_computeCommandList->Close());
    }

    NAME_D3D12_OBJECT(m_commandList);
    NAME_D3D12_OBJECT(m_computeCommandList);

    // Create the depth stencil view.
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
        depthStencilDesc.Texture2D.MipSlice = 0;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&m_depthStencil)
        ));

        NAME_D3D12_OBJECT(m_depthStencil);

        m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create the constant buffers.
    {
        const UINT constantBufferDataSize = m_fbxLoader.NumMeshes() * FrameCount * sizeof(SceneConstantBuffer);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(constantBufferDataSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_upload_constantBuffer)));

        NAME_D3D12_OBJECT(m_upload_constantBuffer);

        for (UINT i = 0; i < m_fbxLoader.NumMeshes(); i++)
        {
            XMFLOAT4 color = {
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[0],
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[1],
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[2],
                1.0f
            };
            m_constantBufferData[i].diffuseColor = color;
        }

        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_upload_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof(SceneConstantBuffer));

#if 0
        // Create shader resource views (SRV) of the constant buffers for the
        // compute shader to read from.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
        srvDesc.Buffer.StructureByteStride = sizeof(SceneConstantBuffer);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CbvSrvOffset, m_cbvSrvUavDescriptorSize);
        for (UINT frame = 0; frame < FrameCount; frame++)
        {
            srvDesc.Buffer.FirstElement = frame * m_fbxLoader.NumMeshes();
            m_device->CreateShaderResourceView(m_upload_constantBuffer.Get(), &srvDesc, cbvSrvHandle);
            cbvSrvHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
        }
#endif
    }
#if 0
    // Create the command signature used for indirect drawing.
    {
        // Each command consists of a CBV update and a DrawInstanced call.
        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        argumentDescs[0].ConstantBufferView.RootParameterIndex = Cbv;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.pArgumentDescs = argumentDescs;
        commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
        commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

        ThrowIfFailed(m_device->CreateCommandSignature(&commandSignatureDesc, m_rootSignature.Get(), IID_PPV_ARGS(&m_commandSignature)));
        NAME_D3D12_OBJECT(m_commandSignature);
    }
#endif

    // Create the command buffers and UAVs to store the results of the compute work.
    ComPtr<ID3D12Resource> commandBufferUpload;
#if 0
    {
        std::vector<IndirectCommand> commands;
        commands.resize(m_fbxLoader.NumMeshes() * FrameCount);
        const UINT commandBufferSize = m_fbxLoader.NumMeshes() * sizeof(IndirectCommand) * FrameCount;

        D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &commandBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_commandBuffer)));

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&commandBufferUpload)));

        NAME_D3D12_OBJECT(m_commandBuffer);

        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_upload_constantBuffer->GetGPUVirtualAddress();
        UINT commandIndex = 0;

        for (UINT frame = 0; frame < FrameCount; frame++)
        {
            for (UINT n = 0; n < m_fbxLoader.NumMeshes(); n++)
            {
                commands[commandIndex].cbv = gpuAddress;
                commands[commandIndex].drawArguments.VertexCountPerInstance = m_fbxLoader.GetMeshes()[n].vertices.size();
                commands[commandIndex].drawArguments.InstanceCount = 1;
                commands[commandIndex].drawArguments.StartVertexLocation = 0;
                commands[commandIndex].drawArguments.StartInstanceLocation = 0;

                commandIndex++;
                gpuAddress += sizeof(SceneConstantBuffer);
            }
        }

        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the command buffer.
        D3D12_SUBRESOURCE_DATA commandData = {};
        commandData.pData = reinterpret_cast<UINT8*>(&commands[0]);
        commandData.RowPitch = commandBufferSize;
        commandData.SlicePitch = commandData.RowPitch;

        UpdateSubresources<1>(m_commandList.Get(), m_commandBuffer.Get(), commandBufferUpload.Get(), 0, 0, 1, &commandData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

        // Create SRVs for the command buffers.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
        srvDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CommandsOffset, m_cbvSrvUavDescriptorSize);
        for (UINT frame = 0; frame < FrameCount; frame++)
        {
            srvDesc.Buffer.FirstElement = frame * m_fbxLoader.NumMeshes();
            m_device->CreateShaderResourceView(m_commandBuffer.Get(), &srvDesc, commandsHandle);
            commandsHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
        }

        // Create the unordered access views (UAVs) that store the results of the compute work.
        CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), ProcessedCommandsOffset, m_cbvSrvUavDescriptorSize);
        for (UINT frame = 0; frame < FrameCount; frame++)
        {
            // Allocate a buffer large enough to hold all of the indirect commands
            // for a single frame as well as a UAV counter.
            commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(CommandBufferCounterOffset + sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &commandBufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_processedCommandBuffers[frame])));

            NAME_D3D12_OBJECT_INDEXED(m_processedCommandBuffers, frame);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
            uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
            uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            m_device->CreateUnorderedAccessView(
                m_processedCommandBuffers[frame].Get(),
                m_processedCommandBuffers[frame].Get(),
                &uavDesc,
                processedCommandsHandle);

            processedCommandsHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
        }

        // Allocate a buffer that can be used to reset the UAV counters and initialize
        // it to 0.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_processedCommandBufferCounterReset)));

        UINT8* pMappedCounterReset = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_processedCommandBufferCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset)));
        ZeroMemory(pMappedCounterReset, sizeof(UINT));
        m_processedCommandBufferCounterReset->Unmap(0, nullptr);
    }
#endif

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence)));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForGpu();
    }
    ComPtr<ID3D12Resource> upload_vertexBuffer;
    ComPtr<ID3D12Resource> upload_indexBuffer;

    // Create Vertex buffer.
    {
        const UINT vertexBufferSize = m_fbxLoader.GetVertices().size() * sizeof(OWO::Vertex);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_default_vertexBuffer)));

        NAME_D3D12_OBJECT(m_default_vertexBuffer);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload_vertexBuffer)));

        D3D12_SUBRESOURCE_DATA vertexData = {};
        auto vertices = m_fbxLoader.GetVertices();
        vertexData.pData = vertices.data();
        vertexData.RowPitch = m_fbxLoader.GetVertices().size() * sizeof(OWO::Vertex);
        vertexData.SlicePitch = vertexData.RowPitch;

        UpdateSubresources<1>(m_commandList.Get(), m_default_vertexBuffer.Get(), upload_vertexBuffer.Get(), 0, 0, 1, &vertexData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_default_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
    }

    // Create Index buffer.
    {
        const UINT indexBufferSize = m_fbxLoader.GetIndices().size() * sizeof(UINT);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_default_indexBuffer)));

        NAME_D3D12_OBJECT(m_default_indexBuffer);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload_indexBuffer)));

        D3D12_SUBRESOURCE_DATA indicesData = {};
        auto indices = m_fbxLoader.GetIndices();
        indicesData.pData = indices.data();
        indicesData.RowPitch = indexBufferSize;
        indicesData.SlicePitch = indicesData.RowPitch;

        UpdateSubresources<1>(m_commandList.Get(), m_default_indexBuffer.Get(), upload_indexBuffer.Get(), 0, 0, 1, &indicesData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_default_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
    }

    // Create Instance buffer.
    {
        auto CreateMockInstances = []() -> std::vector<OWO::Instance> {
            std::vector<OWO::Instance> instances;

            float spacing = 200.0f;
            for (UINT i = 0; i < 5; i++) {
                OWO::Instance inst;
                auto world = XMMatrixTranslation(spacing * i, 0, 0);
                world = XMMatrixMultiply(world, XMMatrixRotationX(XMConvertToRadians(270.0f)));
                XMStoreFloat4x4(&inst.world, XMMatrixTranspose(world));
                inst.materialIndex = XMINT4(i, 0, 0, 0);
                instances.push_back(inst);
            }
            return instances;
            };

        auto _instances = CreateMockInstances();

        const UINT instanceBufferSize = _instances.size() * sizeof(OWO::Instance);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_upload_instanceBuffer)));

        NAME_D3D12_OBJECT(m_upload_instanceBuffer);

        UINT8* pInstanceDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_upload_instanceBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pInstanceDataBegin)));
        memcpy(pInstanceDataBegin, &_instances[0], instanceBufferSize);
    }

    // Create diffuse texture.
    {
        for (UINT i = 0; i < m_fbxLoader.GetTextures().size(); i++)
        {
            auto& tex = m_fbxLoader.GetTextures()[i];
            if (tex.type != "diffuse") continue;

            // Load image.
            int texWidth, texHeight, texChannels;
            std::string filename = m_fbxDirName + tex.path;
            UINT8* texture = stbi_load(filename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

            auto& _diffuse = m_diffuseTexture[i];
            auto& _buffer = m_upload_buffer[i];

            // Create diffuse texture itself.
            D3D12_RESOURCE_DESC textureDesc = {};
            textureDesc.MipLevels = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.Width = texWidth;
            textureDesc.Height = texHeight;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            textureDesc.DepthOrArraySize = 1;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&_diffuse)));

            // Create upload buffer
            const UINT64 uploadBufferSize = GetRequiredIntermediateSize(_diffuse.Get(), 0, 1);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&_buffer)));

            // Record command: copy image data to texture
            auto imageData = D3D12_SUBRESOURCE_DATA{};
            imageData.pData = &texture[0];
            imageData.RowPitch = texWidth * 4;
            imageData.SlicePitch = imageData.RowPitch * texHeight;
            UpdateSubresources(m_commandList.Get(), _diffuse.Get(), _buffer.Get(), 0, 0, 1, &imageData);

            // Transition texture to shader resource state.
            m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                _diffuse.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        }
    }

    // Create SRVs for diffuse texture
    {
        for (int i = 0; i < m_fbxLoader.GetTextures().size(); i++)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
                m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
                TextureOffset + i,
                m_cbvSrvUavDescriptorSize);

            auto& _diffuse = m_diffuseTexture[i];
            for (UINT frame = 0; frame < FrameCount; frame++)
            {
                m_device->CreateShaderResourceView(_diffuse.Get(), &srvDesc, srvHandle);
                srvHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
            }
        }
    }

    // Init frustrum visualizer
    {
        m_frustumDraw.CreateDeviceResources(m_device.Get(), m_renderTargets[0]->GetDesc().Format, m_depthStencil->GetDesc().Format);
    }

    // Create command buffer.
    {
        const UINT commandBufferDataSize = m_fbxLoader.NumMeshes() * FrameCount * sizeof(IndirectCommand);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(commandBufferDataSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_upload_commandBuffer)));

        NAME_D3D12_OBJECT(m_upload_commandBuffer);

        std::vector<IndirectCommand> commandsBufferData(m_fbxLoader.NumMeshes());

        /*
         for i = 0 .. NumMeshes
              cmd[i].vbv = m_default_vertexBuffer->GetGPUVirtualAddress()
              cmd[i].vbv += i * sizeof(OWO::Vertex)
              cmd[i].ibv = m_default_indexBuffer->GetGPUVirtualAddress()
              cmd[i].ibv += i * sizeof(UINT)
              cmd[i].instVBV = m_upload_instanceBuffer->GetGPUVirtualAddress()
              cmd[i].drawArguments.IndexCountPerInstance = m_fbxLoader.GetMeshes()[i].indices.size()
              cmd[i].drawArguments.InstanceCount = 5
              cmd[i].drawArguments.StartIndexLocation = 0
              cmd[i].drawArguments.BaseVertexLocation = 0
              cmd[i].drawArguments.StartInstanceLocation = 0
        */

        for (int i = 0; i < m_fbxLoader.NumMeshes(); i++) {
            
        }
        
        /*
         data = new IndirectCommand[NumMeshes]
         m_upload_commandBuffer -> Map(&data)
         memcpy(data, commandsBufferData)
        */

        
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_upload_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof(SceneConstantBuffer));
    }

    ExecuteGFXCommandList();
}

// Get a random float value between min and max.
float D3D12ExecuteIndirect::GetRandomFloat(float min, float max)
{
    float scale = static_cast<float>(rand()) / RAND_MAX;
    float range = max - min;
    return scale * range + min;
}

// Update frame-based values.
void D3D12ExecuteIndirect::OnUpdate()
{
    m_timer.Tick(NULL);
    auto frameTime = static_cast<float>(m_timer.GetElapsedSeconds());
    m_mainCam.Update(frameTime);
    m_debugCam.Update(frameTime);

    // Update view frustrum
    {
        XMMATRIX view = m_debugCam.GetViewMatrix();
        XMMATRIX proj = m_debugCam.GetProjectionMatrix(m_fovy, m_aspectRatio);
        XMMATRIX viewInv = XMMatrixInverse(nullptr, view);

        XMMATRIX cullWorld = XMMatrixInverse(nullptr, m_mainCam.GetViewMatrix());
        XMMATRIX cullView = m_mainCam.GetViewMatrix();
        XMMATRIX cullProj = m_mainCam.GetProjectionMatrix(m_fovy, m_aspectRatio, 1.0f, 300.0f);

        XMMATRIX vp = XMMatrixTranspose(cullView * cullProj);
        XMVECTOR planes[6] =
        {
            XMPlaneNormalize(vp.r[3] + vp.r[0]), // Left
            XMPlaneNormalize(vp.r[3] - vp.r[0]), // Right
            XMPlaneNormalize(vp.r[3] + vp.r[1]), // Bottom
            XMPlaneNormalize(vp.r[3] - vp.r[1]), // Top
            XMPlaneNormalize(vp.r[2]),           // Near
            XMPlaneNormalize(vp.r[3] - vp.r[2]), // Far
        };

        m_frustumDraw.Update(view * proj, planes);
    }


    //// Update the MVP matrix for each mesh.
    //{
    //    auto mvp = XMMatrixMultiply(view, proj);

    //    for (UINT i = 0; i < m_fbxLoader.NumMeshes(); i++)
    //    {
    //        int diffID = m_fbxLoader.meshes[i].material.GetTextureID("diffuse", m_fbxLoader.GetTextures());
    //        if (diffID >= 0) diffID += TextureOffset;
    //        m_constantBufferData[i].textureID = XMINT4(diffID, 0, 0, 0);
    //        XMStoreFloat4x4(&m_constantBufferData[i].mvp, XMMatrixTranspose(mvp));
    //    }
    //}

    //UINT8* destination = m_pCbvDataBegin + (m_fbxLoader.NumMeshes() * m_frameIndex * sizeof(SceneConstantBuffer));
    //memcpy(destination, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof(SceneConstantBuffer));
}

// Render the scene.
void D3D12ExecuteIndirect::OnRender()
{
    PIXBeginEvent(m_commandQueue.Get(), 0, L"Render");
    ResetGFXCommandList();

    // Transist Render Target from PRESENT to RENDER_TARGET
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &barrier);
    }

    auto updateCameraConstant = [&](int playerOrGod, float aspectRatioDiv = 2) {
        if (playerOrGod < 0)        // play
        {
            XMMATRIX view = m_mainCam.GetViewMatrix();
            XMMATRIX proj = m_mainCam.GetProjectionMatrix(m_fovy, m_aspectRatio / aspectRatioDiv);
            auto mvp = XMMatrixMultiply(view, proj);

            for (UINT i = 0; i < m_fbxLoader.NumMeshes(); i++)
            {
                int diffID = m_fbxLoader.meshes[i].material.GetTextureID("diffuse", m_fbxLoader.GetTextures());
                if (diffID >= 0) diffID += TextureOffset;
                m_constantBufferData[i].textureID = XMINT4(diffID, 0, 0, 0);
                XMStoreFloat4x4(&m_constantBufferData[i].mvp, XMMatrixTranspose(mvp));
            }
        }
        else        // god
        {
            XMMATRIX view = m_debugCam.GetViewMatrix();
            XMMATRIX proj = m_debugCam.GetProjectionMatrix(m_fovy, m_aspectRatio / aspectRatioDiv);
            auto mvp = XMMatrixMultiply(view, proj);

            for (UINT i = 0; i < m_fbxLoader.NumMeshes(); i++)
            {
                int diffID = m_fbxLoader.meshes[i].material.GetTextureID("diffuse", m_fbxLoader.GetTextures());
                if (diffID >= 0) diffID += TextureOffset;
                m_constantBufferData[i].textureID = XMINT4(diffID, 0, 0, 0);
                XMStoreFloat4x4(&m_constantBufferData[i].mvp, XMMatrixTranspose(mvp));
            }
        }

        UINT8* destination = m_pCbvDataBegin + (m_fbxLoader.NumMeshes() * m_frameIndex * sizeof(SceneConstantBuffer));
        memcpy(destination, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof(SceneConstantBuffer));
    };

    // Populate command list (player).
    {
        auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        m_graphicsPass.RecordCommandCommand(m_commandList.Get(), rtvHandle, dsvHandle, m_width, m_height, -1);

        // render left (player)
        updateCameraConstant(-1);
        for (int i = 0; i < m_fbxLoader.GetMeshes().size(); i++)
        {
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = m_default_vertexBuffer->GetGPUVirtualAddress();
            vbv.BufferLocation += m_fbxLoader.GetVertexOffset(i) * sizeof(OWO::Vertex);
            vbv.StrideInBytes = sizeof(OWO::Vertex);
            vbv.SizeInBytes = sizeof(OWO::Vertex) * m_fbxLoader.GetMeshes()[i].vertices.size();

            D3D12_VERTEX_BUFFER_VIEW instbv;
            instbv.BufferLocation = m_upload_instanceBuffer->GetGPUVirtualAddress();
            instbv.StrideInBytes = sizeof(OWO::Instance);
            instbv.SizeInBytes = sizeof(OWO::Instance) * 5;

            D3D12_INDEX_BUFFER_VIEW ibv;
            ibv.BufferLocation = m_default_indexBuffer->GetGPUVirtualAddress();
            ibv.BufferLocation += m_fbxLoader.GetIndexOffset(i) * sizeof(UINT);
            ibv.Format = DXGI_FORMAT_R32_UINT;
            ibv.SizeInBytes = sizeof(UINT) * m_fbxLoader.GetMeshes()[i].indices.size();

            D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = m_upload_constantBuffer->GetGPUVirtualAddress();
            constantBuffer += (i + m_frameIndex * m_fbxLoader.NumMeshes()) * sizeof(SceneConstantBuffer);

            m_graphicsPass.RecordPerMeshCommand(
                m_commandList.Get(),
                vbv, instbv, ibv,
                constantBuffer,
                m_fbxLoader.GetMeshes()[i].indices.size()
            );
        }
    }

    ExecuteGFXCommandList();

    ResetGFXCommandList();

    // Populate command list (god).
    {
        auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        m_graphicsPass.RecordCommandCommand(
            m_commandList.Get(),
            rtvHandle, dsvHandle,
            m_width, m_height, 1,
            0 );
        updateCameraConstant(1);
        for (int i = 0; i < m_fbxLoader.GetMeshes().size(); i++)
        {
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = m_default_vertexBuffer->GetGPUVirtualAddress();
            vbv.BufferLocation += m_fbxLoader.GetVertexOffset(i) * sizeof(OWO::Vertex);
            vbv.StrideInBytes = sizeof(OWO::Vertex);
            vbv.SizeInBytes = sizeof(OWO::Vertex) * m_fbxLoader.GetMeshes()[i].vertices.size();

            D3D12_VERTEX_BUFFER_VIEW instbv;
            instbv.BufferLocation = m_upload_instanceBuffer->GetGPUVirtualAddress();
            instbv.StrideInBytes = sizeof(OWO::Instance);
            instbv.SizeInBytes = sizeof(OWO::Instance) * 5;

            D3D12_INDEX_BUFFER_VIEW ibv;
            ibv.BufferLocation = m_default_indexBuffer->GetGPUVirtualAddress();
            ibv.BufferLocation += m_fbxLoader.GetIndexOffset(i) * sizeof(UINT);
            ibv.Format = DXGI_FORMAT_R32_UINT;
            ibv.SizeInBytes = sizeof(UINT) * m_fbxLoader.GetMeshes()[i].indices.size();

            D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = m_upload_constantBuffer->GetGPUVirtualAddress();
            constantBuffer += (i + m_frameIndex * m_fbxLoader.NumMeshes()) * sizeof(SceneConstantBuffer);

            m_graphicsPass.RecordPerMeshCommand(
                m_commandList.Get(),
                vbv,
                instbv,
                ibv,
                constantBuffer,
                m_fbxLoader.GetMeshes()[i].indices.size()
            );
        }
        m_frustumDraw.Draw(m_commandList.Get());
    }

    // Transist Render Target from RENDER_TARGET to PRESENT
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &barrier);
    }

    ExecuteGFXCommandList();

    PIXEndEvent(m_commandQueue.Get());

    ThrowIfFailed(m_swapChain->Present(1, 0));

    MoveToNextFrame();
}

// Release sample's D3D objects.
void D3D12ExecuteIndirect::ReleaseD3DResources()
{
    m_fence.Reset();
    ResetComPtrArray(&m_renderTargets);
    m_commandQueue.Reset();
    m_swapChain.Reset();
    m_device.Reset();
}

// Tears down D3D resources and reinitializes them.
void D3D12ExecuteIndirect::RestoreD3DResources()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        WaitForGpu();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    ReleaseD3DResources();
    OnInit();
}

void D3D12ExecuteIndirect::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void D3D12ExecuteIndirect::OnKeyDown(UINT8 key)
{
    if (key == VK_SPACE)
    {
        m_enableCulling = !m_enableCulling;
    }
    m_mainCam.OnKeyDown(key);
    m_debugCam.OnKeyDown(key);
}

void D3D12ExecuteIndirect::OnKeyUp(UINT8 key)
{
    m_mainCam.OnKeyUp(key);
    m_debugCam.OnKeyUp(key);
}

void D3D12ExecuteIndirect::ResetGFXCommandList()
{
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
}

void D3D12ExecuteIndirect::ExecuteGFXCommandList()
{
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    WaitForGpu();
}

// Wait for pending GPU work to complete.
void D3D12ExecuteIndirect::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12ExecuteIndirect::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
