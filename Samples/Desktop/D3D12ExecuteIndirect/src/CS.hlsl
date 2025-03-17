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

#define threadBlockSize 64

struct SceneConstantBuffer
{
    float4 diffuseColor;
    int4 textureID;
    float4x4 mvp;
    float padding[40];
};

struct IndirectCommand
{
    uint vbv0_BufferLocation_high; // high 32 bit of uint64_t
    uint vbv0_BufferLocation_low;
    uint vbv0_SizeInBytes;
    uint vbv0_StrideInBytes;
    
    uint vbv1_BufferLocation_high;
    uint vbv1_BufferLocation_low;
    uint vbv1_SizeInBytes;
    uint vbv1_StrideInBytes;
    
    uint ibv_BufferLocation_high;
    uint ibv_BufferLocation_low;
    uint ibv_SizeInBytes;
    uint ibv_Format;
    
    uint constantBUfferAddr_high;
    uint constantBUfferAddr_low;
    
    uint draw_IndexCountPerInstance;
    uint draw_InstanceCount;
    uint draw_StartIndexLocation;
    int draw_BaseVertexLocation;
    uint draw_StartInstanceLocation;
};

cbuffer RootConstants : register(b0)
{
    uint commandCount; // The number of commands to be processed.
};

StructuredBuffer<IndirectCommand> inputCommands : register(t0); // SRV: Indirect commands
RWStructuredBuffer<IndirectCommand> outputCommands : register(u0); // UAV: Processed indirect commands

[numthreads(threadBlockSize, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint index = (groupId.x * threadBlockSize) + groupIndex;

    if (index < commandCount)
    {
        IndirectCommand command = inputCommands[index];
        outputCommands[index] = command;
    }
}
