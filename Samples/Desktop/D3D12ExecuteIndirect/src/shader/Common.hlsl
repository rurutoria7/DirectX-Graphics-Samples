//cbuffer cbPerView : register(b0)
//{
//    matrix View;
//    matrix Projection;
//    matrix ViewProjection;
//    matrix InvViewProjection;
//    matrix InvProjection;
//    matrix ViewProjectionPrevious;
//    matrix ProjectionPrevious;
//    float4 CameraPosition;
//    float2 Jitter;
//    float2 CameraRange;
//    float2 ShadowUVScale;
//    float2 ShadowUVOffset;
//    float DPDirection;
//    float Time;
//    float EnableLodDebugRendering;
//    float cbPerView_pad;
//};

//float4 ClipPosFromDepth(float2 uv, float depth)
//{
//    float4 clipPos = float4(uv * 2 - 1, depth, 1);
//    clipPos.y = -clipPos.y;

//    return clipPos;
//}

//float3 ViewPosFromDepth(float2 uv, float depth)
//{
//    float4 clipPos = ClipPosFromDepth(uv, depth);

//    float4 viewPos = mul(clipPos, InvProjection);
//    viewPos.xyz /= viewPos.w;

//    return viewPos.xyz;
//}

//float3 WorldPosFromDepth(float2 uv, float depth)
//{
//    float4 clipPos = ClipPosFromDepth(uv, depth);

//    float4 worldPos = mul(clipPos, InvViewProjection);
//    worldPos.xyz /= worldPos.w; 

//    return worldPos.xyz;
//}

//float3 UVZFromClipPos(float4 clipPos)
//{
//    float3 uvz = clipPos.xyz / clipPos.w;
//    uvz.xy = uvz.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
//    return uvz;
//}

//float4x4 ConvertVectorsToMatrix(float4 vector0, float4 vector1, float4 vector2)
//{
//    return float4x4(
//		vector0.x, vector1.x, vector2.x, 0.0,
//		vector0.y, vector1.y, vector2.y, 0.0,
//		vector0.z, vector1.z, vector2.z, 0.0,
//		vector0.w, vector1.w, vector2.w, 1.0
//		);
//}

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

#define INSTANCE_COMPACTION_SCAN_BLOCK 128
