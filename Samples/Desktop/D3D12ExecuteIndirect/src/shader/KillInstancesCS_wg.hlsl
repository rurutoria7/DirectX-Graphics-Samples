#include "Common.hlsl"

StructuredBuffer<Instance> inInstances : register(t0);
RWStructuredBuffer<my_uint> outInstances : register(u0);
RWStructuredBuffer<IndirectCommand> outCommands : register(u1);

cbuffer Constants : register(b0)
{
    int4 numInstance;   // use only x
    float4x4 vp;
    uint numMeshes;     // 新增：網格總數
};

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.5) && (clipPos.x <= 1.5) &&
                  (clipPos.y >= -1.5) && (clipPos.y <= 1.5) &&
                  (clipPos.z >= -1) && (clipPos.z <= 1);
    return inside;
}

#define NOOF_THREADS 128

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    "RootConstants(num32BitConstants=21, b0), " // 更新常數數量
    "SRV(t0), "
    "UAV(u0), "
    "UAV(u1)")]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    if (DTid.x == 0)
    {
        for (uint meshId = 0; meshId < numMeshes; meshId++)
        {
            outCommands[meshId].draw_InstanceCount = 0;
        }
    }
    DeviceMemoryBarrierWithGroupSync();
    
    uint idx = DTid.x;
    if (idx >= (uint) numInstance.x)
        return;
    
    Instance inst = inInstances[idx];
    uint meshId = inst.materialIndex.x; // 使用 materialIndex.x 作為 mesh_id
    
    float3 pos = inst.world[3].xyz;
    float4 clipPos = mul(float4(pos, 1.0f), vp);

    if (isInFrustum(clipPos))
    {
        outInstances[idx].x = 1;
        
        // 更新對應 mesh_id 的 draw_InstanceCount
        InterlockedAdd(outCommands[meshId].draw_InstanceCount, 1);
    }
    else
    {
        outInstances[idx].x = 0;
    }
}
