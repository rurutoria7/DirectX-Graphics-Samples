#pragma once
#include "d3dx12.h"

struct GenArgPass
{
    void Execute(
        ID3D12CommandList* in_commandList,
        ID3D12Resource* in_indexBuffer,
        ID3D12Resource* in_vertexBuffer,
        ID3D12Resource* out_drawArgsBuffer,
        ID3D12Resource* out_culledIndexBuffer,
        ID3D12Resource* out_culledVertexBuffer
    ) {

    }

    //void Execute(
    //    ID3D12CommandList* in_commandList,
    //    ID3D12Resource* in_indexBuffer,
    //    ID3D12Resource* in_vertexBuffer,
    //    ID3D12Resource* in_drawArgsBuffer,
    //    ID3D12Resource* in_constantBuffer,
    //    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    //    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle
    //) {

    //}
};