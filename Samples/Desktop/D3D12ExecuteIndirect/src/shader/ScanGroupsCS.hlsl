#include"Common.hlsl"
#include"CullInstancePass_common.hlsl"

#define NOOF_THREADS NOOF_THREADS_SCAN_GROUPS

groupshared uint temp[NOOF_THREADS * 2];

StructuredBuffer<my_uint> groupSumArrayIn : register(t0);
RWStructuredBuffer<my_uint> groupSumArrayOut : register(u0);

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    ROOT_SIG_B1
    "SRV(t0), "
    "UAV(u0)"
)]
void scanGroupSums(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
    int tID = threadID.x;

    const int NoofElements = NOOF_THREADS * 2;

    int offset = 1;
    temp[2 * tID] = groupSumArrayIn[2 * tID].x;
    temp[2 * tID + 1] = groupSumArrayIn[2 * tID + 1].x;

    int d;

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

    if (tID == 0)
    {
        temp[NoofElements - 1] = 0;
    }

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

    groupSumArrayOut[2 * tID].x = temp[2 * tID];
    groupSumArrayOut[2 * tID + 1].x = temp[2 * tID + 1];
}