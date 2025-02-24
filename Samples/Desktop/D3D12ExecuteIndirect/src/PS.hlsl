
SamplerState g_sampler : register(s0);

cbuffer SceneConstantBuffer : register(b0)
{
    float4 diffuseColor;
    int4 textureID;
    float4x4 mvp;
    float4 padding[40];
};
struct PSInput
{
    float4 position : SV_POSITION;
    uint color : COLOR;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    if (textureID.x < 0)
    {
        return diffuseColor;
    }
    
    Texture2D<float4> myTexture = ResourceDescriptorHeap[textureID.x];
    float4 color = myTexture.Sample(g_sampler, input.uv);
    float4 diffuse = diffuseColor * color;
    return diffuse;
}
