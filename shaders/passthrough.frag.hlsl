// passthrough.frag.hlsl - Direct low-latency presentation sampling

Texture2D captureTexture : register(t0);
SamplerState captureSampler : register(s0);

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return captureTexture.Sample(captureSampler, input.texCoord);
}
