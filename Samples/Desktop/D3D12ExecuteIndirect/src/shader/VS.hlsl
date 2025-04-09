cbuffer SceneConstantBuffer : register(b0)
{
    float4 diffuseColor;
    int4 textureID;
    float4x4 mvp;
    float4 padding[40];
};

struct VertexInput
{
    float3 position : POSITION; // ¦ì¸m (R32G32B32_FLOAT)
    float3 normal : NORMAL; // ªk½u (R32G32B32_FLOAT)
    float2 texcoord : TEXCOORD; // UV (R32G32_FLOAT)
};

struct InstanceInput
{
    float4x4 world : WORLD;
    int4 materialId : MATERIAL_IDX;
};

struct PSInput
{
    float4 position : SV_POSITION;
    uint color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

PSInput main(VertexInput in_vert, InstanceInput in_inst)
{
    
    PSInput result;
    result.position = float4(in_vert.position, 1.0f);
    result.position = mul(result.position, in_inst.world);
    result.position = mul(result.position, mvp);
    result.uv = in_vert.texcoord;
    return result;
    
}