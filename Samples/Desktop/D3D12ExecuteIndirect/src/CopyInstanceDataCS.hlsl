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
    float pad;  // num of 32 bit: 4 + 3 + 4 + 1 = 12
};
struct my_bool
{
    bool x;
};
struct my_uint
{
    uint x;
};
// [toodo] make sure these structure are the same as in the c++ code
StructuredBuffer<InstanceDataOut> instanceDataIn : register(t0);
StructuredBuffer<my_bool> instancePredicatesIn : register(t1);
StructuredBuffer<my_uint> groupSumArray : register(t2);
StructuredBuffer<my_uint> scannedInstancePredicates : register(t3);

RWStructuredBuffer<InstanceDataOut> instanceDataOut : register(u0);
RWStructuredBuffer<my_uint> drawcallIDDataOut : register(u1);

#define NOOF_THREADS 128

[numthreads(NOOF_THREADS, 1, 1)]
void copyInstanceData(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
    int tID = threadID.x;

    uint groupSum = groupID.x > 0 ? groupSumArray[groupID.x].x : 0;
    uint instanceDataOutIndex;

	//scatter results
    if (instancePredicatesIn[tID].x == true)
    {
        instanceDataOutIndex = scannedInstancePredicates[tID].x + groupSum;

		//copy transform
        instanceDataOut[instanceDataOutIndex].World_Row1 = instanceDataIn[tID].World_Row1;
        instanceDataOut[instanceDataOutIndex].World_Row2 = instanceDataIn[tID].World_Row2;
        instanceDataOut[instanceDataOutIndex].World_Row3 = instanceDataIn[tID].World_Row3;

		//copy packed uint with mesh lod/index and drawcall_ID
        instanceDataOut[instanceDataOutIndex].MeshLOD_MeshIndex_DrawcallID = instanceDataIn[tID].MeshLOD_MeshIndex_DrawcallID;

		//write to the drawcall ID stream for this instance
        //drawcallIDDataOut[instanceDataOutIndex].x = instanceDataIn[tID].MeshLOD_MeshIndex_DrawcallID & 0xFFFF;
    }
}