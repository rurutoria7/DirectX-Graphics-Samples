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
struct my_uint
{
    uint x;
};
struct Instance
{
    float4x4 world;
    int4 materialIndex;
};
struct IndirectCommand
{
    uint vbv0_BufferLocation_high; // high 32 bit of uint64_t
    uint vbv0_BufferLocation_low;
    uint vbv0_SizeInBytes;
    uint vbv0_StrideInBytes;
    
    uint vbv1_BufferLocation_high;
    uint vbv1_BufferLocation_low;
    uint vbv1_SizeInBytes;
    uint vbv1_StrideInBytes;
    
    uint ibv_BufferLocation_high;
    uint ibv_BufferLocation_low;
    uint ibv_SizeInBytes;
    uint ibv_Format;
    
    uint constantBUfferAddr_high;
    uint constantBUfferAddr_low;
    
    uint draw_IndexCountPerInstance;
    uint draw_InstanceCount;
    uint draw_StartIndexLocation;
    int draw_BaseVertexLocation;
    uint draw_StartInstanceLocation;
};

StructuredBuffer<Instance> instanceDataIn : register(t0);
StructuredBuffer<my_uint> instancePredicatesIn : register(t1);
StructuredBuffer<my_uint> groupSumArray : register(t2);
StructuredBuffer<my_uint> scannedInstancePredicates : register(t3);

RWStructuredBuffer<Instance> instanceDataOut : register(u0);

#define NOOF_THREADS 4

[numthreads(NOOF_THREADS, 1, 1)]
void copyInstanceData(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) 
{
    int tID = threadID.x;

    uint groupSum = groupID.x > 0 ? groupSumArray[groupID.x].x : 0;
    uint instanceDataOutIndex;

    if (instancePredicatesIn[tID].x)
    {
        instanceDataOutIndex = scannedInstancePredicates[tID].x + groupSum;

        instanceDataOut[instanceDataOutIndex].world = instanceDataIn[tID].world;
        instanceDataOut[instanceDataOutIndex].materialIndex = instanceDataIn[tID].materialIndex;
    }
}
