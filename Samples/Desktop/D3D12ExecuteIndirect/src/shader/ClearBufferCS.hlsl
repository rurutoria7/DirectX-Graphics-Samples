#include "Common.hlsl"

cbuffer ClearBufferCB : register(b1)
{
    uint scanned_group_sum_buffer_noof_elements;
    uint group_sum_buffer_noof_elements;
    uint inst_newpos_buffer_noof_elements;
    uint is_inst_alive_buffer_noof_elements;
}

RWStructuredBuffer<my_uint> is_inst_alive_buffer : register(u0);       // noof instance
RWStructuredBuffer<my_uint> scanned_group_sum_buffer : register(u1);   // noof instance
RWStructuredBuffer<my_uint> inst_newpos_buffer : register(u2);         // noof instance
RWStructuredBuffer<my_uint> group_sum_buffer : register(u3);           // noof instance

#define NOOF_THREADS 64

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    "RootConstants(num32BitConstants=2, b1), "
    "UAV(u0), "
    "UAV(u1), "
    "UAV(u2), "
    "UAV(u3), "
)]

void main
(
    uint3 threadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupID : SV_GroupID
)
{
    int tID = threadID.x;

    if (tID < scanned_group_sum_buffer_noof_elements)
        scanned_group_sum_buffer[tID].x = 0;

    if (tID < group_sum_buffer_noof_elements)
        group_sum_buffer[tID].x = 0;

    if (tID < inst_newpos_buffer_noof_elements)
        inst_newpos_buffer[tID].x = 0;

    if (tID < is_inst_alive_buffer_noof_elements)
        is_inst_alive_buffer[tID].x = 0;
}