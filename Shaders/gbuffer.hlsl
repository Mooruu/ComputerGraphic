cbuffer cbObject : register(b0) { float4x4 gWorldViewProj; float4x4 gWorld; };
cbuffer cbMaterial : register(b1) { float4 gDiffuse; float4 gSpecular; float4 gAmbient; float4 gEmissive; float4 gUV; float4 gFlags; };
Texture2D gDiffuseMap : register(t0); SamplerState gSampler : register(s0);
struct VSIn { float3 Pos : POSITION; float3 Normal : NORMAL; float2 UV : TEXCOORD; };
struct VSOut { float4 Pos : SV_POSITION; float3 WorldPos : POSITION; float3 Normal : NORMAL; float2 UV : TEXCOORD; };
VSOut VS(VSIn v) { VSOut o; o.Pos=mul(float4(v.Pos,1),gWorldViewProj); o.WorldPos=mul(float4(v.Pos,1),gWorld).xyz; o.Normal=normalize(mul(v.Normal,(float3x3)gWorld)); o.UV=v.UV*gUV.xy+gUV.zw; return o; }
struct GBufferOut { float4 AlbedoSpec : SV_Target0; float4 WorldPos : SV_Target1; float4 Normal : SV_Target2; };
GBufferOut PS(VSOut p) { GBufferOut o; float3 albedo=gDiffuse.rgb; if(gFlags.x>0.5) albedo*=gDiffuseMap.Sample(gSampler,p.UV).rgb; o.AlbedoSpec=float4(albedo, max(gSpecular.w,1)); o.WorldPos=float4(p.WorldPos,1); o.Normal=float4(normalize(p.Normal),1); return o; }
