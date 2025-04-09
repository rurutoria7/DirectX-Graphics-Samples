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

#include "GraphicsPass.h"

#define _DEBUG


const UINT D3D12ExecuteIndirect::CommandSizePerFrame = MaxNumMeshes * sizeof( IndirectCommand );
const UINT D3D12ExecuteIndirect::CommandBufferCounterOffset = AlignForUavCounter( D3D12ExecuteIndirect::CommandSizePerFrame );
const float D3D12ExecuteIndirect::TriangleHalfWidth = 0.05f;
const float D3D12ExecuteIndirect::TriangleDepth = 1.0f;
const float D3D12ExecuteIndirect::CullingCutoff = 0.5f;
const float D3D12ExecuteIndirect::FarPlaneMainCam = 700.0f;
const float D3D12ExecuteIndirect::FarPlaneDebugCam = 2000.0f;
const float D3D12ExecuteIndirect::FovDebugCam = XM_PI / 3;

D3D12ExecuteIndirect::D3D12ExecuteIndirect( UINT width, UINT height, std::wstring name ) :
    DXSample( width, height, name ),
    m_frameIndex( 0 ),
    m_cullingScissorRect(),
    m_rtvDescriptorSize( 0 ),
    m_cbvSrvUavDescriptorSize( 0 ),
    m_csRootConstants(),
    m_enableCulling( false ),
    m_fenceValues {},
    m_fbxDirName( "D:\\LocalFiles\\2024-Winter\\D3D\\DirectX-Graphics-Samples\\Samples\\Desktop\\D3D12ExecuteIndirect\\src\\Assets\\" ),
    m_fbxFilename( "texturedMonkey.obj" ),
    m_fovy( XM_PI / 5 )
{
    m_constantBufferData.resize( MaxNumMeshes * FrameCount );

    m_csRootConstants.xOffset = TriangleHalfWidth;
    m_csRootConstants.zOffset = TriangleDepth;
    m_csRootConstants.cullOffset = CullingCutoff;
    m_csRootConstants.commandCount = MaxNumMeshes;

    float center = width / 2.0f;
    m_cullingScissorRect.left = static_cast<LONG>(center - (center * CullingCutoff));
    m_cullingScissorRect.right = static_cast<LONG>(center + (center * CullingCutoff));
    m_cullingScissorRect.bottom = static_cast<LONG>(height);

    ThrowIfFailed( DXGIDeclareAdapterRemovalSupport() );
}

void D3D12ExecuteIndirect::OnInit()
{
    LoadPipeline();
    LoadAssets();

    auto num_inst = m_fbxLoader.GetInstances().size();

    m_graphicsPass.Init( m_device.Get(), GetAssetFullPath( L"" ) );
    m_processCommandPass.Init( m_device.Get(), GetAssetFullPath( L"" ) );
    m_cullInstancePass.init( m_device.Get(), GetAssetFullPath( L"" ), num_inst );
    m_mainCam.Init( { 0, 15, 40 }, false );
    m_mainCam.SetMoveSpeed( 25.0f );
    m_debugCam.Init( { 0, 50, 100 }, true );
    m_debugCam.SetMoveSpeed( 250.0f );
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
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed( CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &factory ) ) );

    if ( m_useWarpDevice )
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed( factory->EnumWarpAdapter( IID_PPV_ARGS( &warpAdapter ) ) );

        ThrowIfFailed( D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS( &m_device )
        ) );
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter( factory.Get(), &hardwareAdapter, true );

        ThrowIfFailed( D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS( &m_device )
        ) );
    }

    // Describe and create the command queues.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ) );
    NAME_D3D12_OBJECT( m_commandQueue );

    D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
    computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    ThrowIfFailed( m_device->CreateCommandQueue( &computeQueueDesc, IID_PPV_ARGS( &m_computeCommandQueue ) ) );
    NAME_D3D12_OBJECT( m_computeCommandQueue );

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
    ThrowIfFailed( factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ) );

    // This sample does not support fullscreen transitions.
    ThrowIfFailed( factory->MakeWindowAssociation( Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER ) );

    ThrowIfFailed( swapChain.As( &m_swapChain ) );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &m_rtvHeap ) ) );

        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_dsvHeap ) ) );

        // Describe and create a constant buffer view (CBV), Shader resource
        // view (SRV), and unordered access view (UAV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
        cbvSrvUavHeapDesc.NumDescriptors = CbvSrvUavDescriptorCountPerFrame * FrameCount;
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE | D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &cbvSrvUavHeapDesc, IID_PPV_ARGS( &m_cbvSrvUavHeap ) ) );
        NAME_D3D12_OBJECT( m_cbvSrvUavHeap );

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
        m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart() );

        // Create a RTV and command allocators for each frame.
        for ( UINT n = 0; n < FrameCount; n++ )
        {
            ThrowIfFailed( m_swapChain->GetBuffer( n, IID_PPV_ARGS( &m_renderTargets[n] ) ) );
            m_device->CreateRenderTargetView( m_renderTargets[n].Get(), nullptr, rtvHandle );
            rtvHandle.Offset( 1, m_rtvDescriptorSize );

            NAME_D3D12_OBJECT_INDEXED( m_renderTargets, n );

            ThrowIfFailed( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocators[n] ) ) );
            ThrowIfFailed( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS( &m_computeCommandAllocators[n] ) ) );
        }
    }
}

// Load the sample assets.
void D3D12ExecuteIndirect::LoadAssets()
{
    // Load FBX.
    {
        m_fbxLoader.LoadFBX( m_fbxDirName + m_fbxFilename );
    }

    // Create the command list.
    {
        ThrowIfFailed( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS( &m_commandList ) ) );
        ThrowIfFailed( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS( &m_computeCommandList ) ) );
        ThrowIfFailed( m_computeCommandList->Close() );
    }

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

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D( DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL ),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS( &m_depthStencil )
        ) );

        NAME_D3D12_OBJECT( m_depthStencil );

        m_device->CreateDepthStencilView( m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );
    }

    // Create the constant buffers.
    {
        /*   Layout of upload_constantBuffer

        frame 0:
            | constant 1 | ... | constant #drawcall |
        frame 1:
            ...
        frame 2:
            ...

        */
        const UINT constantBufferDataSize = m_fbxLoader.NumMeshes() * sizeof( SceneConstantBuffer );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( constantBufferDataSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &m_upload_constantBuffer ) ) );

        NAME_D3D12_OBJECT( m_upload_constantBuffer );

        for ( UINT i = 0; i < m_fbxLoader.NumMeshes(); i++ )
        {
            XMFLOAT4 color = {
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[0],
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[1],
                m_fbxLoader.GetMeshes()[i].material.diffuseColor[2],
                1.0f
            };
            m_constantBufferData[i].diffuseColor = color;
        }

        CD3DX12_RANGE readRange( 0, 0 );        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed( m_upload_constantBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin) ) );
        memcpy( m_pCbvDataBegin, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof( SceneConstantBuffer ) );

#if 0
        // Create shader resource views (SRV) of the constant buffers for the
        // compute shader to read from.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
        srvDesc.Buffer.StructureByteStride = sizeof( SceneConstantBuffer );
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle( m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CbvSrvOffset, m_cbvSrvUavDescriptorSize );
        for ( UINT frame = 0; frame < FrameCount; frame++ )
        {
            srvDesc.Buffer.FirstElement = frame * m_fbxLoader.NumMeshes();
            m_device->CreateShaderResourceView( m_upload_constantBuffer.Get(), &srvDesc, cbvSrvHandle );
            cbvSrvHandle.Offset( CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize );
        }
#endif
    }

#if 0
    {
        const UINT commandBufferSize = m_fbxLoader.NumMeshes() * sizeof( IndirectCommand ) * FrameCount;

        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_upload_constantBuffer->GetGPUVirtualAddress();
        UINT commandIndex = 0;

        // Create SRVs for the command buffers. 
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
            srvDesc.Buffer.StructureByteStride = sizeof( IndirectCommand );
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandle( m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CommandsOffset, m_cbvSrvUavDescriptorSize );
            for ( UINT frame = 0; frame < FrameCount; frame++ )
            {
                srvDesc.Buffer.FirstElement = frame * m_fbxLoader.NumMeshes();
                m_device->CreateShaderResourceView( m_upload_commandBuffer.Get(), &srvDesc, commandsHandle );
                commandsHandle.Offset( CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize );
            }
        }

        // Create the unordered access views (UAVs) that store the results of the compute work.
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle( m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), ProcessedCommandsOffset, m_cbvSrvUavDescriptorSize );
            for ( UINT frame = 0; frame < FrameCount; frame++ )
            {
                // Allocate a buffer large enough to hold all of the indirect commands
                // for a single frame as well as a UAV counter.
                auto commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( CommandBufferCounterOffset + sizeof( UINT ), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

                ThrowIfFailed( m_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                    D3D12_HEAP_FLAG_NONE,
                    &commandBufferDesc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS( &m_processedCommandBuffers[frame] ) ) );

                NAME_D3D12_OBJECT_INDEXED( m_processedCommandBuffers, frame );

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = DXGI_FORMAT_UNKNOWN;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = 0;
                uavDesc.Buffer.NumElements = m_fbxLoader.NumMeshes();
                uavDesc.Buffer.StructureByteStride = sizeof( IndirectCommand );
                uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
                uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

                m_device->CreateUnorderedAccessView(
                    m_processedCommandBuffers[frame].Get(),
                    m_processedCommandBuffers[frame].Get(),
                    &uavDesc,
                    processedCommandsHandle );

                processedCommandsHandle.Offset( CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize );
            }

            // Allocate a buffer that can be used to reset the UAV counters and initialize
            // it to 0.
            {
                ThrowIfFailed( m_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer( sizeof( UINT ) ),
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS( &m_processedCommandBufferCounterReset ) ) );

                UINT8* pMappedCounterReset = nullptr;
                CD3DX12_RANGE readRange( 0, 0 );        // We do not intend to read from this resource on the CPU.
                ThrowIfFailed( m_processedCommandBufferCounterReset->Map( 0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset) ) );
                ZeroMemory( pMappedCounterReset, sizeof( UINT ) );
                m_processedCommandBufferCounterReset->Unmap( 0, nullptr );
            }
        }
    }
#endif
    // Create the fence
    auto createFence = [&]()
        {
            ThrowIfFailed( m_device->CreateFence( m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
            ThrowIfFailed( m_device->CreateFence( m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_computeFence ) ) );
            m_fenceValues[m_frameIndex]++;

            m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
            if ( m_fenceEvent == nullptr )
            {
                ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
            }

            // Wait for the command list to execute; we are reusing the same command 
            // list in our main loop but for now, we just want to wait for setup to 
            // complete before continuing.
            WaitForGpu();
        };
    createFence();


    ComPtr<ID3D12Resource> upload_vertexBuffer;
    ComPtr<ID3D12Resource> upload_indexBuffer;

    // Create Vertex buffer.
    {
        /*    Layout of default_vertexBuffer

        | mesh 1 | mesh 2 | ... | mesh #meshes |

        */

        const UINT vertexBufferSize = m_fbxLoader.GetVertices().size() * sizeof( OWO::Vertex );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS( &m_default_vertexBuffer ) ) );

        NAME_D3D12_OBJECT( m_default_vertexBuffer );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &upload_vertexBuffer ) ) );

        D3D12_SUBRESOURCE_DATA vertexData = {};
        auto vertices = m_fbxLoader.GetVertices();
        vertexData.pData = vertices.data();
        vertexData.RowPitch = m_fbxLoader.GetVertices().size() * sizeof( OWO::Vertex );
        vertexData.SlicePitch = vertexData.RowPitch;

        UpdateSubresources<1>( m_commandList.Get(), m_default_vertexBuffer.Get(), upload_vertexBuffer.Get(), 0, 0, 1, &vertexData );
        m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_default_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
    }

    // Create Index buffer.
    {
        /*    Layout of default_indexBuffer

            | mesh 1 | mesh 2 | ... | mesh #meshes |

        */
        const UINT indexBufferSize = m_fbxLoader.GetIndices().size() * sizeof( UINT );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS( &m_default_indexBuffer ) ) );

        NAME_D3D12_OBJECT( m_default_indexBuffer );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &upload_indexBuffer ) ) );

        D3D12_SUBRESOURCE_DATA indicesData = {};
        auto indices = m_fbxLoader.GetIndices();
        indicesData.pData = indices.data();
        indicesData.RowPitch = indexBufferSize;
        indicesData.SlicePitch = indicesData.RowPitch;

        UpdateSubresources<1>( m_commandList.Get(), m_default_indexBuffer.Get(), upload_indexBuffer.Get(), 0, 0, 1, &indicesData );
        m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_default_indexBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER ) );
    }

    // Create Instance buffer.
    {
        auto _instances = m_fbxLoader.GetInstances();
        const UINT instanceBufferSize = _instances.size() * sizeof( OWO::Instance );

        // Create upload buffer & upload data from main memory
        {
            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( instanceBufferSize ),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS( &m_upload_instanceBuffer ) ) );

            NAME_D3D12_OBJECT( m_upload_instanceBuffer );

            UINT8* pInstanceDataBegin;
            CD3DX12_RANGE readRange( 0, 0 );
            ThrowIfFailed( m_upload_instanceBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&pInstanceDataBegin) ) );
            memcpy( pInstanceDataBegin, &_instances[0], instanceBufferSize );
        }

        // Create default buffer & copy data from upload buffer
        {
            auto buffer_size = CullInstancePass::get_padded_size( _instances.size() ) * sizeof( OWO::Instance );

            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( instanceBufferSize ),
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &m_default_instance_buffer ) ) );
            NAME_D3D12_OBJECT( m_default_instance_buffer );
            m_commandList->CopyBufferRegion( m_default_instance_buffer.Get(), 0, m_upload_instanceBuffer.Get(), 0, instanceBufferSize );
            m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_default_instance_buffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }

        // Create default proccessed buffer
        {
            auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( instanceBufferSize, flags ),
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &m_default_proccessed_instanceBuffer ) ) );

            NAME_D3D12_OBJECT( m_default_proccessed_instanceBuffer );
        }

        // Create instance newpos buffer
        {
            auto buffer_size = CullInstancePass::get_padded_size( _instances.size() ) * sizeof( unsigned int );
            auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( buffer_size, flags ),
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &m_default_instance_newpos_buffer ) ) );
        }

        // Create is instance alive buffer
        {
            auto buffer_size = CullInstancePass::get_padded_size( _instances.size() ) * sizeof( unsigned int );
            auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;


            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( buffer_size, flags ),
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &m_default_is_instance_alive_buffer ) ) );
        }
    }

    // Create diffuse texture.
    {
        for ( UINT i = 0; i < m_fbxLoader.GetTextures().size(); i++ )
        {
            auto& tex = m_fbxLoader.GetTextures()[i];
            if ( tex.type != "diffuse" ) continue;

            // Load image.
            int texWidth, texHeight, texChannels;
            std::string filename = m_fbxDirName + tex.path;
            UINT8* texture = stbi_load( filename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha );

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
            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS( &_diffuse ) ) );

            // Create upload buffer
            const UINT64 uploadBufferSize = GetRequiredIntermediateSize( _diffuse.Get(), 0, 1 );
            ThrowIfFailed( m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer( uploadBufferSize ),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS( &_buffer ) ) );

            // Record command: copy image data to texture
            auto imageData = D3D12_SUBRESOURCE_DATA {};
            imageData.pData = &texture[0];
            imageData.RowPitch = texWidth * 4;
            imageData.SlicePitch = imageData.RowPitch * texHeight;
            UpdateSubresources( m_commandList.Get(), _diffuse.Get(), _buffer.Get(), 0, 0, 1, &imageData );

            // Transition texture to shader resource state.
            m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition(
                _diffuse.Get(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) );
        }
    }

    // Create SRVs for diffuse texture
    {
        for ( int i = 0; i < m_fbxLoader.GetTextures().size(); i++ )
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
                m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
                TextureOffset + i,
                m_cbvSrvUavDescriptorSize );

            auto& _diffuse = m_diffuseTexture[i];
            for ( UINT frame = 0; frame < FrameCount; frame++ )
            {
                m_device->CreateShaderResourceView( _diffuse.Get(), &srvDesc, srvHandle );
                srvHandle.Offset( CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize );
            }
        }
    }

    // Init frustrum visualizer
    {
        m_frustumDraw.CreateDeviceResources( m_device.Get(), m_renderTargets[0]->GetDesc().Format, m_depthStencil->GetDesc().Format );
    }

    // Create the Command buffer.
    ComPtr<ID3D12Resource> upload_command_buffer;
    {
        const UINT commandBufferDataSize = m_fbxLoader.NumMeshes() * sizeof( IndirectCommand );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( commandBufferDataSize ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &upload_command_buffer ) ) );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( commandBufferDataSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &m_default_command_buffer ) ) );

        NAME_D3D12_OBJECT( m_default_command_buffer );

        // Fill the command buffer data in main memory
        std::vector<IndirectCommand> commandsBufferData( m_fbxLoader.NumMeshes() );
        for ( int i = 0; i < m_fbxLoader.GetMeshes().size(); i++ )
        {
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = m_default_vertexBuffer->GetGPUVirtualAddress();
            vbv.BufferLocation += m_fbxLoader.GetVertexOffset( i ) * sizeof( OWO::Vertex );
            vbv.StrideInBytes = sizeof( OWO::Vertex );
            vbv.SizeInBytes = sizeof( OWO::Vertex ) * m_fbxLoader.GetMeshes()[i].vertices.size();

            D3D12_VERTEX_BUFFER_VIEW instbv;
            instbv.BufferLocation = m_default_proccessed_instanceBuffer->GetGPUVirtualAddress();
            instbv.BufferLocation += m_fbxLoader.GetInstanceOffset( i ) * sizeof( OWO::Instance );
            instbv.StrideInBytes = sizeof( OWO::Instance );
            instbv.SizeInBytes = sizeof( OWO::Instance ) * m_fbxLoader.GetMeshes()[i].instances.size();

            D3D12_INDEX_BUFFER_VIEW ibv;
            ibv.BufferLocation = m_default_indexBuffer->GetGPUVirtualAddress();
            ibv.BufferLocation += m_fbxLoader.GetIndexOffset( i ) * sizeof( UINT );
            ibv.Format = DXGI_FORMAT_R32_UINT;
            ibv.SizeInBytes = sizeof( UINT ) * m_fbxLoader.GetMeshes()[i].indices.size();

            D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = m_upload_constantBuffer->GetGPUVirtualAddress();
            constantBuffer += (i) * sizeof( SceneConstantBuffer );

            IndirectCommand cmd;
            cmd.vbv0 = vbv;
            cmd.vbv1 = instbv;
            cmd.ibv = ibv;
            cmd.constantBufferAddr = constantBuffer;
            cmd.drawIndexedArgs = {};
            cmd.drawIndexedArgs.IndexCountPerInstance = m_fbxLoader.GetMeshes()[i].indices.size();
            cmd.drawIndexedArgs.InstanceCount = m_fbxLoader.GetMeshes()[i].instances.size();
            cmd.drawIndexedArgs.StartIndexLocation = 0;
            cmd.drawIndexedArgs.BaseVertexLocation = 0;
            cmd.drawIndexedArgs.StartInstanceLocation = 0;

            /* TOODOO: optimize command buffer (medium priority)
            - change per - command binding for IA--> different start location in draw arguments
            */

            commandsBufferData[i] = cmd;
        }

        // Copy from upload_command_buffer to m_default_command_buffer
        D3D12_SUBRESOURCE_DATA sd = {};
        sd.pData = commandsBufferData.data();
        sd.RowPitch = commandBufferDataSize;
        sd.SlicePitch = sd.RowPitch;

        UpdateSubresources<1>( m_commandList.Get(), m_default_command_buffer.Get(), upload_command_buffer.Get(), 0, 0, 1, &sd );
        m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_default_command_buffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
    }

    // Create the Proccessed command buffer
    {
        const UINT commandBufferDataSize = m_fbxLoader.NumMeshes() * sizeof( IndirectCommand );

        auto rd = CD3DX12_RESOURCE_DESC::Buffer( commandBufferDataSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS( &m_default_proccessed_command_buffer ) ) );
    }

    // Create no culling command buffer
    ComPtr<ID3D12Resource> upload_tmp_buffer;
    {
        const UINT commandBufferDataSize = m_fbxLoader.NumMeshes() * sizeof( IndirectCommand );
        auto rd = CD3DX12_RESOURCE_DESC::Buffer( commandBufferDataSize );
        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS( &m_default_no_culling_command_buffer ) ) );

        ThrowIfFailed( m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &upload_tmp_buffer ) ) );

        std::vector<IndirectCommand> commandsBufferData( m_fbxLoader.NumMeshes() );
        for ( int i = 0; i < m_fbxLoader.GetMeshes().size(); i++ )
        {
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = m_default_vertexBuffer->GetGPUVirtualAddress();
            vbv.BufferLocation += m_fbxLoader.GetVertexOffset( i ) * sizeof( OWO::Vertex );
            vbv.StrideInBytes = sizeof( OWO::Vertex );
            vbv.SizeInBytes = sizeof( OWO::Vertex ) * m_fbxLoader.GetMeshes()[i].vertices.size();
            D3D12_VERTEX_BUFFER_VIEW instbv;
            instbv.BufferLocation = m_default_instance_buffer->GetGPUVirtualAddress();
            instbv.BufferLocation += m_fbxLoader.GetInstanceOffset( i ) * sizeof( OWO::Instance );
            instbv.StrideInBytes = sizeof( OWO::Instance );
            instbv.SizeInBytes = sizeof( OWO::Instance ) * m_fbxLoader.GetMeshes()[i].instances.size();
            D3D12_INDEX_BUFFER_VIEW ibv;
            ibv.BufferLocation = m_default_indexBuffer->GetGPUVirtualAddress();
            ibv.BufferLocation += m_fbxLoader.GetIndexOffset( i ) * sizeof( UINT );
            ibv.Format = DXGI_FORMAT_R32_UINT;
            ibv.SizeInBytes = sizeof( UINT ) * m_fbxLoader.GetMeshes()[i].indices.size();
            D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = m_upload_constantBuffer->GetGPUVirtualAddress();
            constantBuffer += (i) * sizeof( SceneConstantBuffer );
            IndirectCommand cmd;
            cmd.vbv0 = vbv;
            cmd.vbv1 = instbv;
            cmd.ibv = ibv;
            cmd.constantBufferAddr = constantBuffer;
            cmd.drawIndexedArgs = {};
            cmd.drawIndexedArgs.IndexCountPerInstance = m_fbxLoader.GetMeshes()[i].indices.size();
            cmd.drawIndexedArgs.InstanceCount = m_fbxLoader.GetMeshes()[i].instances.size();
            cmd.drawIndexedArgs.StartIndexLocation = 0;
            cmd.drawIndexedArgs.BaseVertexLocation = 0;
            cmd.drawIndexedArgs.StartInstanceLocation = 0;
            commandsBufferData[i] = cmd;
        }

        void* pMappedCommandBuffer;
        CD3DX12_RANGE readRange( 0, 0 );        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed( upload_tmp_buffer->Map( 0, &readRange, &pMappedCommandBuffer ) );
        memcpy( pMappedCommandBuffer, &commandsBufferData[0], commandBufferDataSize );

        m_commandList->CopyBufferRegion( m_default_no_culling_command_buffer.Get(), 0, upload_tmp_buffer.Get(), 0, commandBufferDataSize );

        auto trans = CD3DX12_RESOURCE_BARRIER::Transition( m_default_no_culling_command_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT );
        m_commandList->ResourceBarrier( 1, &trans );
    }

    ExecuteGFXCommandList();
}

// Get a random float value between min and max.
float D3D12ExecuteIndirect::GetRandomFloat( float min, float max )
{
    float scale = static_cast<float>(rand()) / RAND_MAX;
    float range = max - min;
    return scale * range + min;
}

// Update frame-based values.
void D3D12ExecuteIndirect::OnUpdate()
{
    m_timer.Tick( NULL );
    auto frameTime = static_cast<float>(m_timer.GetElapsedSeconds());
    m_mainCam.Update( frameTime );
    m_debugCam.Update( frameTime );

    // Update view frustrum
    {
        XMMATRIX view = m_debugCam.GetViewMatrix();
        XMMATRIX proj = m_debugCam.GetProjectionMatrix( FovDebugCam, m_aspectRatio / AspectRatioDivider, 1.0f, FarPlaneDebugCam);

        XMMATRIX cullView = m_mainCam.GetViewMatrix();
        XMMATRIX cullProj = m_mainCam.GetProjectionMatrix( m_fovy, m_aspectRatio / AspectRatioDivider, 1.0f, FarPlaneMainCam);

        XMMATRIX vp = XMMatrixTranspose( cullView * cullProj );
        XMVECTOR planes[6] =
        {
            XMPlaneNormalize( vp.r[3] + vp.r[0] ), // Left
            XMPlaneNormalize( vp.r[3] - vp.r[0] ), // Right
            XMPlaneNormalize( vp.r[3] + vp.r[1] ), // Bottom
            XMPlaneNormalize( vp.r[3] - vp.r[1] ), // Top
            XMPlaneNormalize( vp.r[2] ),           // Near
            XMPlaneNormalize( vp.r[3] - vp.r[2] ), // Far
        };

        m_frustumDraw.Update( view * proj, planes );
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
    PIXBeginEvent( m_commandQueue.Get(), 0, L"Render" );

    auto updateCameraConstant = [&]( int playerOrGod)
        {
            if ( playerOrGod < 0 )        // play
            {
                XMMATRIX view = m_mainCam.GetViewMatrix();
                XMMATRIX proj = m_mainCam.GetProjectionMatrix( m_fovy, m_aspectRatio / AspectRatioDivider, 1.0f, FarPlaneMainCam);
                auto mvp = XMMatrixMultiply( view, proj );

                for ( UINT i = 0; i < m_fbxLoader.NumMeshes(); i++ )
                {
                    int diffID = m_fbxLoader.meshes[i].material.GetTextureID( "diffuse", m_fbxLoader.GetTextures() );
                    if ( diffID >= 0 ) diffID += TextureOffset;
                    m_constantBufferData[i].textureID = XMINT4( diffID, 0, 0, 0 );
                    XMStoreFloat4x4( &m_constantBufferData[i].mvp, XMMatrixTranspose( mvp ) );
                }
            }
            else        // god
            {
                XMMATRIX view = m_debugCam.GetViewMatrix();
                XMMATRIX proj = m_debugCam.GetProjectionMatrix( FovDebugCam, m_aspectRatio / AspectRatioDivider, 1.0f, FarPlaneDebugCam);
                auto mvp = XMMatrixMultiply( view, proj );

                for ( UINT i = 0; i < m_fbxLoader.NumMeshes(); i++ )
                {
                    int diffID = m_fbxLoader.meshes[i].material.GetTextureID( "diffuse", m_fbxLoader.GetTextures() );
                    if ( diffID >= 0 ) diffID += TextureOffset;
                    m_constantBufferData[i].textureID = XMINT4( diffID, 0, 0, 0 );
                    XMStoreFloat4x4( &m_constantBufferData[i].mvp, XMMatrixTranspose( mvp ) );
                }
            }

            UINT8* destination = m_pCbvDataBegin;
            memcpy( destination, &m_constantBufferData[0], m_fbxLoader.NumMeshes() * sizeof( SceneConstantBuffer ) );
        };

    updateCameraConstant( -1 );

    ResetGFXCommandList();

    // Transist Render Target from PRESENT to RENDER_TARGET
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET );
        m_commandList->ResourceBarrier( 1, &barrier );
    }

    ExecuteGFXCommandList();

    ResetComputeCommandList();

    // Populate cull instance pass
    {
        XMMATRIX view = m_mainCam.GetViewMatrix();
        XMMATRIX proj = m_mainCam.GetProjectionMatrix( m_fovy, m_aspectRatio / AspectRatioDivider, 1.0f, FarPlaneMainCam );
        auto mvp = XMMatrixMultiply( view, proj );
        
        ID3D12GraphicsCommandList* in_cmd_list = m_computeCommandList.Get();
        auto in_inst_buffer = m_default_instance_buffer.Get();
        auto out_is_inst_alive_buffer = m_default_is_instance_alive_buffer.Get();
        auto out_inst_newpos_buffer = m_default_instance_newpos_buffer.Get();
        auto out_proccessed_inst_buffer = m_default_proccessed_instanceBuffer.Get();
        auto out_command_buffer = m_default_command_buffer.Get();
        UINT in_num_inst = m_fbxLoader.GetInstances().size();
        DirectX::XMMATRIX in_vp_no_transpose = mvp;

        m_cullInstancePass.record_dispatch(
            in_cmd_list,
            in_inst_buffer,
            out_is_inst_alive_buffer,
            out_inst_newpos_buffer,
            out_proccessed_inst_buffer,
            out_command_buffer,
            in_num_inst,
            in_vp_no_transpose
        );
    }

    ExecuteComputeCommandList();

    ResetComputeCommandList();

    // Populate proccess command pass
    {
        auto upload_command_buffer_addr = m_default_command_buffer->GetGPUVirtualAddress();
        auto processed_command_buffer_addr = m_default_proccessed_command_buffer->GetGPUVirtualAddress();

        m_processCommandPass.RecordDispatch(
            m_computeCommandList.Get(),
            upload_command_buffer_addr,
            processed_command_buffer_addr,
            m_fbxLoader.NumMeshes() );
    }

    ExecuteComputeCommandList();

    ResetGFXCommandList();

    // Populate GFX command list (player).
    {
        auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize );
        auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
        m_commandList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

        m_graphicsPass.SetBeforeDraw( m_commandList.Get(), rtvHandle, dsvHandle, m_width, m_height, -1, 1 );

        // render left (player)
        if ( m_enableCulling )
        {
            m_graphicsPass.Draw(
                m_commandList.Get(),
                m_fbxLoader.GetMeshes().size(),
                m_default_proccessed_command_buffer.Get() );
        }
        else
        {
            m_graphicsPass.Draw(
                m_commandList.Get(),
                m_fbxLoader.GetMeshes().size(),
                m_default_no_culling_command_buffer.Get() );
        }
    }

    ExecuteGFXCommandList();

    ResetGFXCommandList();

    updateCameraConstant( 1 );

    // Populate GFX command list (god).
    {
        auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize );
        auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
        m_commandList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

        m_graphicsPass.SetBeforeDraw(
            m_commandList.Get(),
            rtvHandle, dsvHandle,
            m_width, m_height, 1,
            0 );

        if ( m_enableCulling )
        {
            m_graphicsPass.Draw(
                m_commandList.Get(),
                m_fbxLoader.GetMeshes().size(),
                m_default_proccessed_command_buffer.Get() );
        }
        else
        {
            m_graphicsPass.Draw(
                m_commandList.Get(),
                m_fbxLoader.GetMeshes().size(),
                m_default_no_culling_command_buffer.Get() );
        }
    }

    m_frustumDraw.Draw( m_commandList.Get() );

    // Transist Render Target from RENDER_TARGET to PRESENT
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT );
        m_commandList->ResourceBarrier( 1, &barrier );
    }

    ExecuteGFXCommandList();

    PIXEndEvent( m_commandQueue.Get() );

    ThrowIfFailed( m_swapChain->Present( 1, 0 ) );

    MoveToNextFrame();
}

// Release sample's D3D objects.
void D3D12ExecuteIndirect::ReleaseD3DResources()
{
    m_fence.Reset();
    ResetComPtrArray( &m_renderTargets );
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
    catch ( HrException& )
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

    CloseHandle( m_fenceEvent );
}

void D3D12ExecuteIndirect::OnKeyDown( UINT8 key )
{
    if ( key == VK_SPACE )
    {
        m_enableCulling = !m_enableCulling;
    }
    m_mainCam.OnKeyDown( key );
    m_debugCam.OnKeyDown( key );
}

void D3D12ExecuteIndirect::OnKeyUp( UINT8 key )
{
    m_mainCam.OnKeyUp( key );
    m_debugCam.OnKeyUp( key );
}

void D3D12ExecuteIndirect::ResetGFXCommandList()
{
    ThrowIfFailed( m_commandAllocators[m_frameIndex]->Reset() );
    ThrowIfFailed( m_commandList->Reset( m_commandAllocators[m_frameIndex].Get(), nullptr ) );
}

void D3D12ExecuteIndirect::ExecuteGFXCommandList()
{
    ThrowIfFailed( m_commandList->Close() );
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
    WaitForGpu();
}

void D3D12ExecuteIndirect::ResetComputeCommandList()
{
    ThrowIfFailed( m_computeCommandAllocators[m_frameIndex]->Reset() );
    ThrowIfFailed( m_computeCommandList->Reset( m_computeCommandAllocators[m_frameIndex].Get(), nullptr ) );
}

void D3D12ExecuteIndirect::ExecuteComputeCommandList()
{
    ThrowIfFailed( m_computeCommandList->Close() );
    ID3D12CommandList* ppCommandLists[] = { m_computeCommandList.Get() };
    m_computeCommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
    WaitForGpuCompute();
}

void D3D12ExecuteIndirect::WaitForGpuCompute()
{
    m_computeCommandQueue->Signal( m_computeFence.Get(), m_fenceValues[m_frameIndex] );
    m_computeFence->SetEventOnCompletion( m_fenceValues[m_frameIndex], m_fenceEvent );
    WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );
    m_fenceValues[m_frameIndex]++;
}

// Wait for pending GPU work to complete.
void D3D12ExecuteIndirect::WaitForGpu()
{
    ThrowIfFailed( m_commandQueue->Signal( m_fence.Get(), m_fenceValues[m_frameIndex] ) );
    ThrowIfFailed( m_fence->SetEventOnCompletion( m_fenceValues[m_frameIndex], m_fenceEvent ) );
    WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12ExecuteIndirect::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed( m_commandQueue->Signal( m_fence.Get(), currentFenceValue ) );

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if ( m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex] )
    {
        ThrowIfFailed( m_fence->SetEventOnCompletion( m_fenceValues[m_frameIndex], m_fenceEvent ) );
        WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
