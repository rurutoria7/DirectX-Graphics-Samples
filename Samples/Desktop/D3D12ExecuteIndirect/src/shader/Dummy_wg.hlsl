#include "Common.hlsl"

GlobalRootSignature globalRS = { 
    "RootConstants(num32BitConstants=21, b0), SRV(t0), UAV(u0), UAV(u1)"
};

StructuredBuffer<Instance> inInstances : register(t0);
RWStructuredBuffer<my_uint> outInstances : register(u0);
RWStructuredBuffer<IndirectCommand> outCommands : register(u1);

cbuffer Constants : register(b0)
{
    int4 numInstance;
    float4x4 vp;
    uint numMeshes;
};

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.1) && (clipPos.x <= 1.1) &&
                  (clipPos.y >= -1.1) && (clipPos.y <= 1.1) &&
                  (clipPos.z >= 0) && (clipPos.z <= 1);
    return inside;
}

#define NOOF_THREADS 128

struct entryRecord
{
    uint pad;  // ignore
};

struct secondNodeInput
{
    uint gridSize;
};

struct thirdNodeInput
{
    uint entryRecordIndex;
};

static const uint c_numEntryRecords = 4;


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1,1,1)]
[NumThreads(1, 1, 1)]
void firstNode(
    DispatchNodeInputRecord< entryRecord> inputData,
    [MaxRecords(1)] NodeOutput<secondNodeInput> secondNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
        GroupNodeOutputRecords<secondNodeInput> outRecs = secondNode.GetGroupNodeOutputRecords(1);
        outRecs[0].gridSize = numInstance.x / NOOF_THREADS + 1;
        outRecs.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_KILL_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS, 1, 1)]
void secondNode(
    DispatchNodeInputRecord<secondNodeInput> inputData,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x == 0)
    {
        for (uint meshId = 0; meshId < numMeshes; meshId++)
        {
            outCommands[meshId].draw_InstanceCount = 0;
        }
    }
    DeviceMemoryBarrierWithGroupSync();

    uint idx = dispatchThreadID.x;
    if (idx >= (uint) numInstance.x)
        return;
    
    Instance inst = inInstances[idx];
    uint meshId = inst.materialIndex.x; // 使用 materialIndex.x 作為 mesh_id
    
    float3 pos = inst.world[3].xyz;
    float4 clipPos = mul(float4(pos, 1.0f), vp);

    if (isInFrustum(clipPos))
    {
        outInstances[idx].x = 1;        
        InterlockedAdd(outCommands[meshId].draw_InstanceCount, 1);
    }
    else
    {
        outInstances[idx].x = 0;
    }
}
