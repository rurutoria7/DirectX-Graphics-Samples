#include "Common.hlsl"
#include "CullInstancePass_common.hlsl"


GlobalRootSignature globalRS = { 
    ROOT_SIG_B1 "SRV(t0), UAV(u0), UAV(u1), UAV(u2), UAV(u3)"
};

StructuredBuffer<Instance> inInstances : register(t0);
globallycoherent RWStructuredBuffer<my_uint> outInstances : register(u0);
globallycoherent RWStructuredBuffer<IndirectCommand> outCommands : register(u1);
globallycoherent RWStructuredBuffer<my_uint> groupSumArray : register(u2);
globallycoherent RWStructuredBuffer<my_uint> scannedInstancePredicates : register(u3);

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.1) && (clipPos.x <= 1.1) &&
                  (clipPos.y >= -1.1) && (clipPos.y <= 1.1) &&
                  (clipPos.z >= 0) && (clipPos.z <= 1);
    return inside;
}

#define NOOF_THREADS 128
#define NOOF_THREADS_SCAN NOOF_THREADS_SCAN_INSTANCES

struct entryRecord
{
    uint pad;  // ignore
};

struct [NodeTrackRWInputSharing] secondNodeInput
{
    uint gridSize : SV_DispatchGrid;
};

struct [NodeTrackRWInputSharing] thirdNodeInput
{
    uint gridSize : SV_DispatchGrid;
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
        outRecs[0].gridSize = noof_instances / NOOF_THREADS + 1;
        outRecs.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_KILL_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS, 1, 1)]
void secondNode(
    RWDispatchNodeInputRecord<secondNodeInput> inputData,
    [MaxRecords(1)] NodeOutput<thirdNodeInput> thirdNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{

    uint idx = dispatchThreadID.x;
    if (idx < (uint) noof_instances)
    {
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

    DeviceMemoryBarrierWithGroupSync();
    Barrier(NODE_INPUT_MEMORY, DEVICE_SCOPE|GROUP_SYNC);
    if (threadIndex == 0 && inputData.FinishedCrossGroupSharing())
    {
        GroupNodeOutputRecords<thirdNodeInput> outRecs = thirdNode.GetGroupNodeOutputRecords(1);
        outRecs[0].gridSize = MAX_NOOF_INSTANCES / NOOF_THREADS_SCAN / 2;
        outRecs.OutputComplete();
    }
}

groupshared uint temp[NOOF_THREADS_SCAN * 2];

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_SCAN,1,1)]
[NumThreads(NOOF_THREADS_SCAN, 1, 1)]
void thirdNode(
    RWDispatchNodeInputRecord<thirdNodeInput> inputData,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    int tID = dispatchThreadID.x;
    int groupTID = threadIndex;

    scannedInstancePredicates[tID].x = 0;
    scannedInstancePredicates[2 * tID].x = 0;
    scannedInstancePredicates[2 * tID + 1].x = 0;
    
    int offset = 1;
    temp[2 * groupTID] = outInstances[2 * tID].x;
    temp[2 * groupTID + 1] = outInstances[2 * tID + 1].x;

    int d;
    const int NoofElements = 2 * NOOF_THREADS_SCAN;

    //perform reduction
    for (d = NoofElements >> 1; d > 0; d >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        if (groupTID < d)
        {
            int ai = offset * (2 * groupTID + 1) - 1;
            int bi = offset * (2 * groupTID + 2) - 1;
            temp[bi] += temp[ai];
        }
        offset *= 2;
    }

    // clear the last element
    if (groupTID == 0)
    {
        groupSumArray[dispatchThreadID.x / NOOF_THREADS_SCAN].x = temp[NoofElements - 1];
        temp[NoofElements - 1] = 0;
    }

    //perform downsweep and build scan
    for (d = 1; d < NoofElements; d *= 2)
    {
        offset >>= 1;

        GroupMemoryBarrierWithGroupSync();

        if (groupTID < d)
        {
            int ai = offset * (2 * groupTID + 1) - 1;
            int bi = offset * (2 * groupTID + 2) - 1;
            int t = temp[ai];
            temp[ai] = temp[bi];
            temp[bi] += t;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    scannedInstancePredicates[2 * tID].x = temp[2 * groupTID]; // store to main memory
    scannedInstancePredicates[2 * tID + 1].x = temp[2 * groupTID + 1];

    DeviceMemoryBarrierWithGroupSync();
}
