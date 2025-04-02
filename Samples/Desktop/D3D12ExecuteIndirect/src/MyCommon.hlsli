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

struct Instance
{
    float4x4 world;
    int4 materialIndex;
    int4 mesh_index;
};
