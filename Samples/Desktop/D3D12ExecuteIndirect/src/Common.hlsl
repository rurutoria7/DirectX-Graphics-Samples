//--------------------------------------------------------------------------------------
// per view
//--------------------------------------------------------------------------------------
cbuffer cbPerView : register(b0)
{
    matrix View;
    matrix Projection;
    matrix ViewProjection;
    matrix InvViewProjection;
    matrix InvProjection;
    matrix ViewProjectionPrevious;
    matrix ProjectionPrevious;
    float4 CameraPosition;
    float2 Jitter;
    float2 CameraRange;
    float2 ShadowUVScale;
    float2 ShadowUVOffset;
    float DPDirection;
    float Time;
    float EnableLodDebugRendering;
    float cbPerView_pad;
};

#if MSAA > 1
#define TextureLoad(tex, addr, subSampleIdx) tex.Load(uint2(addr), subSampleIdx)
//#define TextureLoad(tex, addr) tex.Load(uint2(addr), 0)
#else
#define TextureLoad(tex, addr) tex[uint2(addr)]
#endif

struct InstanceDataIn
{
    float3 BboxMin;
    uint BboxSizes;

    uint DrawcallID;

    float3 pad;
};

struct InstanceDataOut
{
    float4 World_Row1;
    float4 World_Row2;
    float4 World_Row3;

    uint MeshLOD_MeshIndex_DrawcallID;

    float3 pad;
};


float4 ClipPosFromDepth(float2 uv, float depth)
{
    float4 clipPos = float4(uv * 2 - 1, depth, 1);
    clipPos.y = -clipPos.y;

    return clipPos;
}

float3 ViewPosFromDepth(float2 uv, float depth)
{
    float4 clipPos = ClipPosFromDepth(uv, depth);

    float4 viewPos = mul(clipPos, InvProjection);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

float3 WorldPosFromDepth(float2 uv, float depth)
{
    float4 clipPos = ClipPosFromDepth(uv, depth);

    float4 worldPos = mul(clipPos, InvViewProjection);
    worldPos.xyz /= worldPos.w; 

    return worldPos.xyz;
}

float3 UVZFromClipPos(float4 clipPos)
{
    float3 uvz = clipPos.xyz / clipPos.w;
    uvz.xy = uvz.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    return uvz;
}

float4x4 ConvertVectorsToMatrix(float4 vector0, float4 vector1, float4 vector2)
{
    return float4x4(
		vector0.x, vector1.x, vector2.x, 0.0,
		vector0.y, vector1.y, vector2.y, 0.0,
		vector0.z, vector1.z, vector2.z, 0.0,
		vector0.w, vector1.w, vector2.w, 1.0
		);
}
