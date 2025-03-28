
#include"Common.hlsl"

// number of 32 bit: 4 + 3 + 4 + 1 = 12
cbuffer OcclusionPassCB : register(b1)
{
    float4 RTSize;
    float MaxMipLevel;
    float ActivateCulling;
    float MipBias;
    unsigned int NoofInstances;
    unsigned int NoofInstancesPowOf2;
    unsigned int NoofDrawcalls;
    unsigned int NoofGroups;
    float pad;
};

struct my_uint
{
    uint x;
};

StructuredBuffer<my_uint> instancePredicatesIn : register(t0);
RWStructuredBuffer<my_uint> groupSumArray : register(u0);
RWStructuredBuffer<my_uint> scannedInstancePredicates : register(u1);

#define NOOF_THREADS 64

// Based on Parallel Prefix Sum (Scan) with CUDA by Mark Harris
groupshared uint temp[NOOF_THREADS * 2];

[numthreads(NOOF_THREADS, 1, 1)]
void scanInstancePredicates(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
    int tID = threadID.x;
    int groupTID = groupThreadID.x;

    scannedInstancePredicates[tID].x = 0;
    scannedInstancePredicates[2 * tID].x = 0;
    scannedInstancePredicates[2 * tID + 1].x = 0;
    
    int offset = 1;
    temp[2 * groupTID] = instancePredicatesIn[2 * tID].x; // load input into shared memory
    temp[2 * groupTID + 1] = instancePredicatesIn[2 * tID + 1].x;

    int d;
    const int NoofElements = 2 * NOOF_THREADS;

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
        groupSumArray[groupID.x].x = temp[NoofElements - 1];
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
}
