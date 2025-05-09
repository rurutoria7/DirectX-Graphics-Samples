#include "Common.hlsl"
#include "CullInstancePass_common.hlsl"

RWStructuredBuffer<my_uint> is_inst_alive_buffer : register(u0);       // noof instance
RWStructuredBuffer<my_uint> scanned_group_sum_buffer : register(u1);   // noof instance
RWStructuredBuffer<my_uint> inst_newpos_buffer : register(u2);         // noof instance
RWStructuredBuffer<my_uint> group_sum_buffer : register(u3);           // noof instance
RWStructuredBuffer<IndirectCommand> command_buffer : register(u4);

#define NOOF_THREADS NOOF_THREADS_CLEAR_BUFFER

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    ROOT_SIG_B1
    "UAV(u0), "
    "UAV(u1), "
    "UAV(u2), "
    "UAV(u3), "
    "UAV(u4)"
)]

void main
(
    uint3 threadID : SV_DispatchThreadID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 groupID : SV_GroupID
)
{
    int tID = threadID.x;

    if (tID < noof_instances_pow_of_2)
    {
        scanned_group_sum_buffer[tID].x = 0;
        group_sum_buffer[tID].x = 0;
        inst_newpos_buffer[tID].x = 0;
        is_inst_alive_buffer[tID].x = 0;
    }

    if (tID < noof_drawcalls)
    {
        command_buffer[tID].draw_InstanceCount = 0;
        command_buffer[tID].draw_StartInstanceLocation = 0;
    }
}