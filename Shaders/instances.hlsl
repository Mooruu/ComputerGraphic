cbuffer cbObject : register(b0) { float4x4 gViewProj; float4x4 gUnusedWorld; float4 gCameraPosition; };
cbuffer cbMaterial : register(b1) { float4 gDiffuse; float4 gSpecular; float4 gAmbient; float4 gEmissive; float4 gUV; float4 gFlags; };

struct VSIn {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    row_major float4x4 World : TEXCOORD1;
    float4 Color : TEXCOORD5;
};
struct VSOut { float4 Pos : SV_POSITION; float3 WorldPos : POSITION; float3 Normal : NORMAL; float4 Color : COLOR; };
struct GBufferOut { float4 AlbedoSpec : SV_Target0; float4 WorldPos : SV_Target1; float4 Normal : SV_Target2; };

VSOut VS(VSIn v) {
    VSOut o;
    float4 worldPosition = mul(float4(v.Pos, 1.0f), v.World);
    o.Pos = mul(worldPosition, gViewProj);
    o.WorldPos = worldPosition.xyz;
    o.Normal = normalize(mul(v.Normal, (float3x3)v.World));
    o.Color = v.Color;
    return o;
}

GBufferOut PS(VSOut p) {
    GBufferOut o;
    o.AlbedoSpec = float4(p.Color.rgb * gDiffuse.rgb, 24.0f);
    o.WorldPos = float4(p.WorldPos, 1.0f);
    o.Normal = float4(normalize(p.Normal), 1.0f);
    return o;
}
