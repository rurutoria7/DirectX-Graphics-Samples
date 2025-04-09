
#include"Common.hlsl"

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


#define NOOF_THREADS 1024

groupshared uint temp[NOOF_THREADS * 2];

struct my_uint { uint x; };

StructuredBuffer<my_uint> groupSumArrayIn : register(t0);
RWStructuredBuffer<my_uint> groupSumArrayOut : register(u0);

[numthreads(NOOF_THREADS, 1, 1)]
void scanGroupSums(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
    int tID = threadID.x;

	//if (tID >= NoofGroups)
	//	return;

    const int NoofElements = 2 * NOOF_THREADS;

    int offset = 1;
    temp[2 * tID] = groupSumArrayIn[2 * tID].x; // load input into shared memory
    temp[2 * tID + 1] = groupSumArrayIn[2 * tID + 1].x;

    int d;

	//perform reduction
    for (d = NoofElements >> 1; d > 0; d >>= 1)
    {
        GroupMemoryBarrierWithGroupSync();

        if (tID < d)
        {
            int ai = offset * (2 * tID + 1) - 1;
            int bi = offset * (2 * tID + 2) - 1;
            temp[bi] += temp[ai];
        }
        offset *= 2;
    }

	// clear the last element
    if (tID == 0)
    {
        temp[NoofElements - 1] = 0;
    }

	//perform downsweep and build scan
    for (d = 1; d < NoofElements; d *= 2)
    {
        offset >>= 1;

        GroupMemoryBarrierWithGroupSync();

        if (tID < d)
        {
            int ai = offset * (2 * tID + 1) - 1;
            int bi = offset * (2 * tID + 2) - 1;
            int t = temp[ai];
            temp[ai] = temp[bi];
            temp[bi] += t;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    groupSumArrayOut[2 * tID].x = temp[2 * tID]; // store to main memory
    groupSumArrayOut[2 * tID + 1].x = temp[2 * tID + 1];
}