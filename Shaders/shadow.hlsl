cbuffer cbShadow : register(b3) { float4x4 gLightViewProj; };
struct VSIn { float3 Pos : POSITION; };
float4 VS(VSIn v) : SV_POSITION { return mul(float4(v.Pos, 1.0f), gLightViewProj); }
