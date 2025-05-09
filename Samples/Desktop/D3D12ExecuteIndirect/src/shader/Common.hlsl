#include "../defines.h"

struct Instance
{
    float4x4 world;
    int4 materialIndex;
};

struct my_uint
{
    uint x;
};

struct IndirectCommand
{    
    uint constantBUfferAddr_high;       // 1
    uint constantBUfferAddr_low;        // 2
    
    uint draw_IndexCountPerInstance;    // 3
    uint draw_InstanceCount;            // 4
    uint draw_StartIndexLocation;       // 5
    int draw_BaseVertexLocation;        // 6
    uint draw_StartInstanceLocation;    // 7

    uint _padding;                   // pad 28 bytes to 32 bytes
};
