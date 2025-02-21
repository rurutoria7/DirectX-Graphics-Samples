
cbuffer SceneConstantBuffer : register(b0)
{
    float4 diffuseColor;
    uint4 color;
    float4x4 mvp;
    float4 padding[40];
};

float4x4 RotationMatrix(float3 angles)
{
    float cx = cos(angles.x), sx = sin(angles.x);
    float cy = cos(angles.y), sy = sin(angles.y);
    float cz = cos(angles.z), sz = sin(angles.z);

    float4x4 rx = float4x4(
        1, 0, 0, 0,
        0, cx, -sx, 0,
        0, sx, cx, 0,
        0, 0, 0, 1
    );

    float4x4 ry = float4x4(
        cy, 0, sy, 0,
        0, 1, 0, 0,
        -sy, 0, cy, 0,
        0, 0, 0, 1
    );

    float4x4 rz = float4x4(
        cz, -sz, 0, 0,
        sz, cz, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );

    return mul(mul(rz, ry), rx);
}

struct VSInput
{
    float3 position : POSITION; // ¦ì¸m (R32G32B32_FLOAT)
    float3 normal : NORMAL; // ªk½u (R32G32B32_FLOAT)
    float2 texcoord : TEXCOORD; // UV (R32G32_FLOAT)
};

struct PSInput
{
    float4 position : SV_POSITION;
    uint color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

PSInput main(VSInput input)
{
    
    PSInput result;
    result.position = float4(input.position.x * 0.1f, input.position.y * 0.1f, input.position.z * 0.1f, 1.0f);
    result.position = mul(result.position, mvp);
    result.uv = input.texcoord;
    return result;
    
    //result.position = mul(float4(input.position, 1.0f), projection);
    //result.color = 1;
    //result.uv = input.texcoord;
    //result.normal = input.normal;
    //return result;
    
    /*
    result.position = mul(position + offset, projection);

    float intensity = saturate((4.0f - result.position.z) / 2.0f);
    result.color = color.x;

    return result;
    */
}