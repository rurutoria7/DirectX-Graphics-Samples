//SamplerState g_sampler : register(s0);

//cbuffer SceneConstantBuffer : register(b0)
//{
//    float4 diffuseColor;
//    uint4 color;
//    float4x4 projection;
//    float4 padding[40];
//};

struct PSInput
{
    float4 position : SV_POSITION;
    uint color : COLOR;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
    
    //Texture2D<float4> myTexture = ResourceDescriptorHeap[3];
    //float4 color = myTexture.Sample(g_sampler, input.uv);
    //float4 diffuse = diffuseColor * color;
    //return diffuse;
}
