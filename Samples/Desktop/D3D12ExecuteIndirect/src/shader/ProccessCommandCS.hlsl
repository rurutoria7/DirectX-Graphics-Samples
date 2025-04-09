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
#include "Common.hlsl"

#define NOOF_THREADS 64

cbuffer RootConstants : register(b0)
{
    uint commandCount; // The number of commands to be processed.
};

StructuredBuffer<IndirectCommand> inputCommands : register(t0); // SRV: Indirect commands
RWStructuredBuffer<IndirectCommand> outputCommands : register(u0); // UAV: Processed indirect commands

[numthreads(NOOF_THREADS, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint index = (groupId.x * NOOF_THREADS) + groupIndex;

    if (index < commandCount)
    {
        IndirectCommand command = inputCommands[index];
        outputCommands[index] = command;
    }
}
