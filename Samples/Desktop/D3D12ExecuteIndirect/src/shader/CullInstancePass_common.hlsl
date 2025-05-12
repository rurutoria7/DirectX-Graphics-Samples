cbuffer OcclusionPassCB : register(b1)
{
    float4x4 vp;
    unsigned int noof_instances;
    unsigned int noof_instances_pow_of_2;
    unsigned int noof_drawcalls;
    float pad;
}; 

#define ROOT_SIG_B1 "RootConstants(num32BitConstants=20, b1), "
