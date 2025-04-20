#include"Common.hlsl"

cbuffer OcclusionPassCB : register(b1)
{
	float4		RTSize;
	float		MaxMipLevel;
	float		ActivateCulling;
	float		MipBias;
	unsigned int NoofInstances;
	unsigned int NoofInstancesPowOf2;
	unsigned int NoofDrawcalls;
	unsigned int NoofGroups;
	float		pad;
};

RWStructuredBuffer<IndirectCommand> inoutCommands : register(u0);

#define NOOF_THREADS 512

groupshared uint temp[NOOF_THREADS * 2];

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    "RootConstants(num32BitConstants=12, b1), "
    "UAV(u0)"
)]
void calcInstanceOffsets(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
	int tID = threadID.x;

    const int NoofElements = 2 * NOOF_THREADS;

	int offset = 1;
	temp[2 * tID] = 2*tID >= NoofDrawcalls ? 0 :  inoutCommands[ 2 * tID ].draw_InstanceCount;			// load input into shared memory
	temp[2 * tID + 1] = 2*tID + 1 >= NoofDrawcalls ? 0 : inoutCommands[ 2 * tID + 1 ].draw_InstanceCount;

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

    if (2 * tID < NoofDrawcalls)    
    {
        inoutCommands[2 * tID].draw_StartInstanceLocation = temp[2 * tID];
    }
    if (2 * tID + 1 < NoofDrawcalls)
    {
        inoutCommands[2 * tID + 1].draw_StartInstanceLocation = temp[2 * tID + 1];
    }
}
