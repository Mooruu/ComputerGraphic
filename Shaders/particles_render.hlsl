struct Particle { float3 Position; float Age; float3 Velocity; float Lifetime; float4 Color; float Size; float3 Padding; };
cbuffer cbParticle : register(b0) { float4x4 gViewProj; float3 gCameraRight; float gDeltaTime; float3 gCameraUp; float gTotalTime; };
StructuredBuffer<Particle> gParticles : register(t0);
struct VOut { uint Id:PARTICLEID; };
struct GOut { float4 Pos:SV_POSITION; float4 Color:COLOR; };
VOut VS(uint id:SV_VertexID){ VOut o; o.Id=id; return o; }
[maxvertexcount(4)] void GS(point VOut input[1], inout TriangleStream<GOut> stream){ Particle p=gParticles[input[0].Id]; float fade=saturate(1-p.Age/p.Lifetime); float3 r=gCameraRight*p.Size, u=gCameraUp*p.Size; float3 c[4]={p.Position-r-u,p.Position-r+u,p.Position+r-u,p.Position+r+u}; GOut o; o.Color=float4(p.Color.rgb*(0.4+fade),1); o.Pos=mul(float4(c[0],1),gViewProj);stream.Append(o);o.Pos=mul(float4(c[1],1),gViewProj);stream.Append(o);o.Pos=mul(float4(c[2],1),gViewProj);stream.Append(o);o.Pos=mul(float4(c[3],1),gViewProj);stream.Append(o); }
float4 PS(GOut i):SV_Target{return i.Color;}
