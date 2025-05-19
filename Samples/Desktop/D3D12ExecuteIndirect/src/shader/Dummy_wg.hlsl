#include "Common.hlsl"
#include "CullInstancePass_common.hlsl"


GlobalRootSignature globalRS = { 
    ROOT_SIG_B1 "SRV(t0), UAV(u0), UAV(u1), UAV(u2), UAV(u3), UAV(u4), UAV(u5)"
};

StructuredBuffer<Instance> inInstances : register(t0);
globallycoherent RWStructuredBuffer<my_uint> outInstances : register(u0);
globallycoherent RWStructuredBuffer<IndirectCommand> outCommands : register(u1);
globallycoherent RWStructuredBuffer<my_uint> groupSumArray : register(u2);
globallycoherent RWStructuredBuffer<my_uint> scannedInstancePredicates : register(u3);
globallycoherent RWStructuredBuffer<Instance> instanceDataOut : register(u4);
globallycoherent RWStructuredBuffer<my_uint> scannedGroupSumArray : register(u5);

bool isInFrustum(float4 clipPos)
{
    clipPos /= clipPos.w;
    bool inside = (clipPos.x >= -1.1) && (clipPos.x <= 1.1) &&
                  (clipPos.y >= -1.1) && (clipPos.y <= 1.1) &&
                  (clipPos.z >= 0) && (clipPos.z <= 1);
    return inside;
}


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

struct [NodeTrackRWInputSharing] fourthNodeInput
{
    uint gridSize : SV_DispatchGrid;
};

struct [NodeTrackRWInputSharing] fifthNodeInput
{
    uint gridSize : SV_DispatchGrid;
};

struct [NodeTrackRWInputSharing] sixthNodeInput
{
    uint gridSize : SV_DispatchGrid;
};

static const uint c_numEntryRecords = 6;


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
        outRecs[0].gridSize = noof_instances / NOOF_THREADS_KILL_INSTANCES + 1;
        outRecs.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_KILL_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS_KILL_INSTANCES, 1, 1)]
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
        outRecs[0].gridSize = noof_instances_pow_of_2 / NOOF_THREADS_SCAN_INSTANCES / 2;
        outRecs.OutputComplete();
    }
}

groupshared uint temp[NOOF_THREADS_SCAN_INSTANCES * 2];

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_SCAN_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS_SCAN_INSTANCES, 1, 1)]
void thirdNode(
    RWDispatchNodeInputRecord<thirdNodeInput> inputData,
    [MaxRecords(1)] NodeOutput<fourthNodeInput> fourthNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    int tID = dispatchThreadID.x;
    int groupTID = threadIndex;
    
    int offset = 1;
    temp[2 * groupTID] = outInstances[2 * tID].x;
    temp[2 * groupTID + 1] = outInstances[2 * tID + 1].x;

    int d;
    const int NoofElements = 2 * NOOF_THREADS_SCAN_INSTANCES;

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
        groupSumArray[dispatchThreadID.x / NOOF_THREADS_SCAN_INSTANCES].x = temp[NoofElements - 1];
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

    Barrier(NODE_INPUT_MEMORY, DEVICE_SCOPE|GROUP_SYNC);
    if (threadIndex == 0 && inputData.FinishedCrossGroupSharing())
    {
        GroupNodeOutputRecords<fourthNodeInput> outRecs = fourthNode.GetGroupNodeOutputRecords(1);
        outRecs[0].gridSize = 1;
        outRecs.OutputComplete();
    }
}

groupshared uint temp_scan[NOOF_THREADS_SCAN_GROUPS * 2];

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1,1,1)]
[NumThreads(NOOF_THREADS_SCAN_GROUPS, 1, 1)]
void fourthNode(
    RWDispatchNodeInputRecord<fourthNodeInput> inputData,
    [MaxRecords(1)] NodeOutput<fifthNodeInput> fifthNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    int tID = dispatchThreadID.x;
    const int NoofElements = NOOF_THREADS_SCAN_GROUPS * 2;

    int offset = 1;
    temp_scan[2 * threadIndex] = groupSumArray[2 * tID].x;
    temp_scan[2 * threadIndex + 1] = groupSumArray[2 * tID + 1].x;

    int d;
    for (d = NoofElements >> 1; d > 0; d >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        if (threadIndex < d)
        {
            int ai = offset * (2 * threadIndex + 1) - 1;
            int bi = offset * (2 * threadIndex + 2) - 1;
            temp_scan[bi] += temp_scan[ai];
        }
        offset *= 2;
    }

    if (threadIndex == 0)
    {
        temp_scan[NoofElements - 1] = 0;
    }

    for (d = 1; d < NoofElements; d *= 2)
    {
        offset >>= 1;

        GroupMemoryBarrierWithGroupSync();

        if (threadIndex < d)
        {
            int ai = offset * (2 * threadIndex + 1) - 1;
            int bi = offset * (2 * threadIndex + 2) - 1;
            int t = temp_scan[ai];
            temp_scan[ai] = temp_scan[bi];
            temp_scan[bi] += t;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    scannedGroupSumArray[2 * tID].x = temp_scan[2 * threadIndex];
    scannedGroupSumArray[2 * tID + 1].x = temp_scan[2 * threadIndex + 1];

    DeviceMemoryBarrierWithGroupSync();

    Barrier(NODE_INPUT_MEMORY, DEVICE_SCOPE|GROUP_SYNC);
    if (threadIndex == 0 && inputData.FinishedCrossGroupSharing())
    {
        GroupNodeOutputRecords<fifthNodeInput> outRecs = fifthNode.GetGroupNodeOutputRecords(1);
        outRecs[0].gridSize = 1 + noof_instances / NOOF_THREADS_COPY_INSTANCES;
        outRecs.OutputComplete();
    }
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_COPY_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS_COPY_INSTANCES, 1, 1)]
void fifthNode(
    RWDispatchNodeInputRecord<fifthNodeInput> inputData,
    [MaxRecords(1)] NodeOutput<sixthNodeInput> sixthNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID,
    uint groupID : SV_GroupID)
{
    int tID = dispatchThreadID.x;

    if (tID < noof_instances && outInstances[tID].x)
    {
        uint groupSum = groupID.x > 0 ? scannedGroupSumArray[groupID.x].x : 0;
        uint instanceDataOutIndex = scannedInstancePredicates[tID].x + groupSum;

        instanceDataOut[instanceDataOutIndex].world = inInstances[tID].world;
        instanceDataOut[instanceDataOutIndex].materialIndex = inInstances[tID].materialIndex;
    }

    DeviceMemoryBarrierWithGroupSync();
    Barrier(NODE_INPUT_MEMORY, DEVICE_SCOPE|GROUP_SYNC);
    if (threadIndex == 0 && inputData.FinishedCrossGroupSharing())
    {
        GroupNodeOutputRecords<sixthNodeInput> outRecs = sixthNode.GetGroupNodeOutputRecords(1);
        outRecs[0].gridSize = 1;
        outRecs.OutputComplete();
    }
}

groupshared uint temp_commands[NOOF_THREADS_SCAN_COMMANDS * 2];

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1,1,1)]
[NumThreads(NOOF_THREADS_SCAN_COMMANDS, 1, 1)]
void sixthNode(
    RWDispatchNodeInputRecord<sixthNodeInput> inputData,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    int tID = dispatchThreadID.x;
    const int NoofElements = 2 * NOOF_THREADS_SCAN_COMMANDS;

    int offset = 1;
    temp_commands[2 * threadIndex] = outCommands[2 * tID].draw_InstanceCount;
    temp_commands[2 * threadIndex + 1] = outCommands[2 * tID + 1].draw_InstanceCount;

    int d;
    for (d = NoofElements >> 1; d > 0; d >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        if (threadIndex < d)
        {
            int ai = offset * (2 * threadIndex + 1) - 1;
            int bi = offset * (2 * threadIndex + 2) - 1;
            temp_commands[bi] += temp_commands[ai];
        }
        offset *= 2;
    }

    if (threadIndex == 0)
    {
        temp_commands[NoofElements - 1] = 0;
    }

    for (d = 1; d < NoofElements; d *= 2)
    {
        offset >>= 1;

        GroupMemoryBarrierWithGroupSync();

        if (threadIndex < d)
        {
            int ai = offset * (2 * threadIndex + 1) - 1;
            int bi = offset * (2 * threadIndex + 2) - 1;
            int t = temp_commands[ai];
            temp_commands[ai] = temp_commands[bi];
            temp_commands[bi] += t;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    outCommands[2 * tID].draw_StartInstanceLocation = temp_commands[2 * threadIndex];
    outCommands[2 * tID + 1].draw_StartInstanceLocation = temp_commands[2 * threadIndex + 1];

    DeviceMemoryBarrierWithGroupSync();
}
