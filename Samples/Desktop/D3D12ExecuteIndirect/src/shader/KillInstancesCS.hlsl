#include "Common.hlsl"
#include "CullInstancePass_common.hlsl"

StructuredBuffer<Instance> inInstances : register(t0);
RWStructuredBuffer<my_uint> outInstances : register(u0);
RWStructuredBuffer<IndirectCommand> outCommands : register(u1);

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.1) && (clipPos.x <= 1.1) &&
                  (clipPos.y >= -1.1) && (clipPos.y <= 1.1) &&
                  (clipPos.z >= 0) && (clipPos.z <= 1);
    return inside;
}

#define NOOF_THREADS NOOF_THREADS_KILL_INSTANCES

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    ROOT_SIG_B1
    "SRV(t0), "
    "UAV(u0), "
    "UAV(u1)")]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{    
    uint idx = DTid.x;
    if (idx < noof_instances)
    {   
        Instance inst = inInstances[idx];
        uint meshId = inst.materialIndex.x;

        float3 pos = inst.world[3].xyz;
        float4 clipPos = mul(float4(pos, 1.0f), vp);

#ifdef DEBUG_CULL_DETERMINISTIC
        if (DEBUG_CULL_DETERMINISTIC(meshId, idx))
#else
        if (isInFrustum(clipPos))
#endif
        {
            outInstances[idx].x = 1;
            
            InterlockedAdd(outCommands[meshId].draw_InstanceCount, 1);
        }
        else
        {
            outInstances[idx].x = 0;
        }
    }

    DeviceMemoryBarrierWithGroupSync();
}
