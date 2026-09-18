cbuffer cbShadow : register(b3) { float4x4 gLightViewProj; };
struct VSIn { float3 Pos : POSITION; row_major float4x4 World : TEXCOORD1; };
float4 VS(VSIn v) : SV_POSITION { return mul(mul(float4(v.Pos, 1.0f), v.World), gLightViewProj); }
