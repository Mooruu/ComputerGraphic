struct Particle { float3 Position; float Age; float3 Velocity; float Lifetime; float4 Color; float Size; float3 Padding; };
cbuffer cbParticle : register(b0) { float4x4 gViewProj; float3 gCameraRight; float gDeltaTime; float3 gCameraUp; float gTotalTime; };
ConsumeStructuredBuffer<Particle> gInput : register(u0);
AppendStructuredBuffer<Particle> gOutput : register(u1);
[numthreads(256,1,1)] void CS(uint id:SV_DispatchThreadID) { Particle p=gInput.Consume(); p.Age+=gDeltaTime; p.Velocity+=float3(0,-3.5,0)*gDeltaTime; p.Position+=p.Velocity*gDeltaTime; if(p.Lifetime<0.1 || p.Lifetime>10.0 || p.Age>=p.Lifetime || p.Position.y<0){ uint h=id*1664525u+(uint)(gTotalTime*1000); float a=(h%6283)/1000.0; p.Position=float3(0,2.0,0); p.Velocity=float3(cos(a)*3.0,5.0+(h%200)/100.0,sin(a)*3.0); p.Age=0; p.Lifetime=1.8+(h%170)/100.0; p.Color=float4(1.0,0.25+(h%50)/100.0,0.03,1); p.Size=0.10+(h%20)/100.0; } gOutput.Append(p); }
