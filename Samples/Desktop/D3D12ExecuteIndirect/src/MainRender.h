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

#pragma once
#include "defines.h"
#include "DXSample.h"
#include "MyMesh.h"
#include "render_pass/GraphicsPass.h"
#include "render_pass/ProccessCommandPass.h"
#include "render_pass/CullTrianglePass.h"
#include "SimpleCamera.h"
#include "StepTimer.h"
#include "FrustumVisualizer.h"
#include "d3d12.h"
#include "render_pass/CullInstancePass.h"
#include "ResourceStateTracker.h"
#include <thread>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class MainRender : public DXSample
{
public:
    MainRender( UINT width, UINT height, std::wstring name );

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown( UINT8 key );
    virtual void OnKeyUp( UINT8 key );
    void ResetGFXCommandList();
    void ExecuteGFXCommandList();
    void ResetComputeCommandList();
    void ExecuteComputeCommandList();

private:
    static const int MAX_NUM_TEXTURES = 1000;

    static const UINT FrameCount = 3;
    static const UINT MaxMeshResourceCount = MAX_NOOF_MESHES * FrameCount;
    static const UINT CommandSizePerFrame = MAX_NOOF_MESHES * sizeof( IndirectCommand );                // The size of the indirect commands to draw all of the triangles in a single frame.
    static const UINT CommandBufferCounterOffset;        // The offset of the UAV counter in the processed command buffer.
    static const UINT ComputeThreadBlockSize = 64;        // Should match the value in compute.hlsl.
    static constexpr const float TriangleHalfWidth = 0.05f;                // The x and y offsets used by the triangle vertices.
    static constexpr const float TriangleDepth = 1.0f;                    // The z offset used by the triangle vertices.
    static constexpr const float CullingCutoff = 0.5f;                    // The +/- x offset of the clipping planes in homogenous space [-1,1].
    static const int AspectRatioDivider = 2;                // Support God & Player view
    static constexpr const float FarPlaneMainCam = 700.0f;                    // Far plane for the main camera.
    static constexpr const float FarPlaneDebugCam = 2000.0f;                    // Far plane for the debug camera.
    static constexpr const float FovDebugCam = XM_PI / 3;                        // Field of view for the debug camera.

    std::string MODEL_DIR_PATH;  // 將在 OnInit 中設置
    std::string MODEL_FILE_NAME = "four_mat.obj";
    float FOV = XM_PI / 5;


    // Constant buffer definition.
    struct SceneConstantBuffer
    {
        XMFLOAT4 diffuseColor;
        XMINT4 textureID;
        XMFLOAT4X4 mvp;

        // Constant buffers are 256-byte aligned. Add padding in the struct to allow multiple buffers
        // to be array-indexed.
        float padding[40];
    };

    // Root constants for the compute shader.
    struct CSRootConstants
    {
        float xOffset;
        float zOffset;
        float cullOffset;
        float commandCount;
    };

    // Compute root signature parameter offsets.
    enum ComputeRootParameters
    {
        SrvUavTable,
        RootConstants,            // Root constants that give the shader information about the triangle vertices and culling planes.
        ComputeRootParametersCount
    };

    // CBV/SRV/UAV desciptor heap offsets.
    enum HeapOffsets
    {
        CbvSrvOffset = 0,                                                    // SRV that points to the constant buffers used by the rendering thread.
        CommandsOffset = CbvSrvOffset + 1,                                    // SRV that points to all of the indirect commands.
        ProcessedCommandsOffset = CommandsOffset + 1,                        // UAV that records the commands we actually want to execute.
        TextureOffset = ProcessedCommandsOffset + 1,                        // SRV that points to the texture used by the rendering thread.
        CbvSrvUavDescriptorCountPerFrame = TextureOffset + MAX_NUM_TEXTURES,    // The number of descriptors per frame.
    };

    StepTimer                          m_timer;
    SimpleCamera m_mainCam;
    SimpleCamera m_debugCam;
    GraphicsPass<MAX_NUM_TEXTURES> m_graphicsPass;
    ProcessCommandPass m_processCommandPass;
    CullInstancePass m_cullInstancePass;
    OWO::FBXLoader m_fbxLoader;
    FrustumVisualizer m_frustumDraw;
    ResourceStateTracker m_stateTracker;

    // Each triangle gets its own constant buffer per frame.
    std::vector<SceneConstantBuffer> m_constantBufferData;
    UINT8* m_pCbvDataBegin;

    bool m_enableCulling;                // Toggle whether the compute shader pre-processes the indirect commands.

    // Pipeline objects.
    D3D12_RECT m_cullingScissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device14> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_computeCommandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandQueue> m_computeCommandQueue;
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12CommandSignature> m_commandSignature;
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    UINT m_rtvDescriptorSize;
    UINT m_cbvSrvUavDescriptorSize;
    UINT m_frameIndex;

    // Synchronization objects.
    ComPtr<ID3D12Fence> m_fence;
    ComPtr<ID3D12Fence> m_computeFence;
    UINT64 m_fenceValues[FrameCount];
    HANDLE m_fenceEvent;

    // Asset objects.
    ComPtr<ID3D12Resource> m_upload_buffer[MAX_NUM_TEXTURES];
    ComPtr<ID3D12Resource> m_diffuseTexture[MAX_NUM_TEXTURES];
    ComPtr<ID3D12GraphicsCommandList6> m_commandList;
    ComPtr<ID3D12GraphicsCommandList10> m_computeCommandList;
    ComPtr<ID3D12Resource> m_default_command_buffer;
    ComPtr<ID3D12Resource> m_upload_instanceBuffer;
    ComPtr<ID3D12Resource> m_default_instance_buffer;
    ComPtr<ID3D12Resource> m_default_proccessed_instanceBuffer;
    ComPtr<ID3D12Resource> m_default_vertexBuffer;
    ComPtr<ID3D12Resource> m_default_culled_vertex_buffer;
    ComPtr<ID3D12Resource> m_default_instance_newpos_buffer;
    ComPtr<ID3D12Resource> m_default_group_sum_buffer;
    ComPtr<ID3D12Resource> m_default_is_instance_alive_buffer;
    ComPtr<ID3D12Resource> m_default_indexBuffer;
    ComPtr<ID3D12Resource> m_drawArgsBuffer;
    ComPtr<ID3D12Resource> m_upload_constantBuffer;
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12Resource> m_commandBuffer;
    ComPtr<ID3D12Resource> m_processedCommandBuffers[FrameCount];
    ComPtr<ID3D12Resource> m_processedCommandBufferCounterReset;
    ComPtr<ID3D12Resource> m_default_proccessed_command_buffer;
    ComPtr<ID3D12Resource> m_default_no_culling_command_buffer;


    void LoadPipeline();
    void LoadAssets();
    void RestoreD3DResources();
    void ReleaseD3DResources();
    float GetRandomFloat( float min, float max );
    void WaitForGpu();
    void WaitForGpuCompute();
    void MoveToNextFrame();

    // We pack the UAV counter into the same buffer as the commands rather than create
    // a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
    // so we pad the command buffer (if necessary) such that the counter will be placed
    // at a valid location in the buffer.
    static inline UINT AlignForUavCounter( UINT bufferSize )
    {
        const UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
        return (bufferSize + (alignment - 1)) & ~(alignment - 1);
    }
};
