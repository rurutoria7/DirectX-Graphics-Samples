#include "Common.hlsl"

StructuredBuffer<Instance> inInstances : register(t0);
RWStructuredBuffer<my_uint> outInstances : register(u0);
RWStructuredBuffer<IndirectCommand> outCommands : register(u1);

cbuffer Constants : register(b0)
{
    int4 numInstance;   // use only x
    float4x4 vp;
};

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.1) && (clipPos.x <= 1.1) &&
                  (clipPos.y >= -1.1) && (clipPos.y <= 1.1) &&
                  (clipPos.z >= -0.5) && (clipPos.z <= 1.00001);
    return inside;
}

#define NOOF_THREADS 128

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    "RootConstants(num32BitConstants=20, b0), "
    "SRV(t0), "
    "UAV(u0), "
    "UAV(u1)"
)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint idx = DTid.x;
    if (idx >= (uint) numInstance.x)
        return;
    
    if (idx == 0)
    {
        outCommands[0].draw_InstanceCount = 0;
    }
    
    uint groupIdx = groupId.x;
    
    Instance inst = inInstances[idx];

    float3 pos = inst.world[3].xyz;
    float4 clipPos = mul(float4(pos, 1.0f), vp);

    if (isInFrustum(clipPos))
    {
        outInstances[idx].x = 1;
        
        // [TOODOO] only works for single drawcall
        InterlockedAdd(outCommands[0].draw_InstanceCount, 1);
    }
    else
    {
        outInstances[idx].x = 0;
    }
}
