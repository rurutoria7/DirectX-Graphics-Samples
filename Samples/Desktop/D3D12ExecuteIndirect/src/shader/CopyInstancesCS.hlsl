#include"Common.hlsl"
#include "CullInstancePass_common.hlsl"

StructuredBuffer<Instance> instanceDataIn : register(t0);
StructuredBuffer<my_uint> instancePredicatesIn : register(t1);
StructuredBuffer<my_uint> groupSumArray : register(t2);
StructuredBuffer<my_uint> scannedInstancePredicates : register(t3);
RWStructuredBuffer<Instance> instanceDataOut : register(u0);

#define NOOF_THREADS NOOF_THREADS_COPY_INSTANCES

[numthreads(NOOF_THREADS, 1, 1)]
[RootSignature(
    ROOT_SIG_B1
    "SRV(t0), "                                
    "SRV(t1), "                                
    "SRV(t2), "                                
    "SRV(t3), "                                
    "UAV(u0)"                                  
)]
void copyInstanceData(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) 
{
    int tID = threadID.x;


    if (tID < noof_instances && instancePredicatesIn[tID].x)
    {
        uint groupSum = groupID.x > 0 ? groupSumArray[groupID.x].x : 0;
        uint instanceDataOutIndex;
        instanceDataOutIndex = scannedInstancePredicates[tID].x + groupSum;

        instanceDataOut[instanceDataOutIndex].world = instanceDataIn[tID].world;
        instanceDataOut[instanceDataOutIndex].materialIndex = instanceDataIn[tID].materialIndex;
    }
}
