#include"Common.hlsl"
#include"CullInstancePass_common.hlsl"

RWStructuredBuffer<IndirectCommand> inoutCommands : register(u0);

#define NOOF_THREADS NOOF_THREADS_SCAN_COMMANDS

groupshared uint temp[NOOF_THREADS * 2];

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    ROOT_SIG_B1
    "UAV(u0)"
)]
void calcInstanceOffsets(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
	int tID = threadID.x;

    const int NoofElements = 2 * NOOF_THREADS;

	int offset = 1;
	temp[2 * tID] = inoutCommands[2 * tID].draw_InstanceCount;
	temp[2 * tID + 1] = inoutCommands[2 * tID + 1].draw_InstanceCount;
	// temp[2 * tID] = 2*tID >= noof_drawcalls ? 0 :  inoutCommands[ 2 * tID ].draw_InstanceCount;			// load input into shared memory
	// temp[2 * tID + 1] = 2*tID + 1 >= noof_drawcalls ? 0 : inoutCommands[ 2 * tID + 1 ].draw_InstanceCount;

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

	inoutCommands[2 * tID].draw_StartInstanceLocation = temp[2 * tID];
	inoutCommands[2 * tID + 1].draw_StartInstanceLocation = temp[2 * tID + 1];
}
