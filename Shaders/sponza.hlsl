//======================================================================================
// Textured OBJ shader for Sponza: material color, diffuse texture, tiling and UV scroll.
//======================================================================================

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
};

cbuffer cbPerMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float4 gSpecularAlbedo;
    float4 gAmbientColor;
    float4 gEmissiveColor;
    // xy = tiling, zw = animated UV offset.
    float4 gUVTilingOffset;
    // x = has diffuse texture, y = has specular texture, z = has normal texture.
    float4 gMaterialFlags;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinearWrap : register(s0);

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC    : TEXCOORD;
};

static const float3 gLightDir[2] =
{
    normalize(float3(1.0f, 2.0f, 1.0f)),
    normalize(float3(-1.0f, 1.0f, -1.0f))
};

static const float3 gLightColor[2] =
{
    float3(1.0f, 0.95f, 0.9f),
    float3(0.8f, 0.9f, 1.0f)
};

static const float gLightIntensity[2] = { 0.9f, 0.5f };

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.TexC = vin.TexC * gUVTilingOffset.xy + gUVTilingOffset.zw;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);
    float3 V = normalize(float3(0.0f, 100.0f, 0.0f) - pin.PosW);

    float4 texColor = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC);
    float3 baseColor = gDiffuseAlbedo.rgb;

    if (gMaterialFlags.x > 0.5f)
        baseColor *= texColor.rgb;

    float3 ambient = baseColor * gAmbientColor.rgb;
    float3 emissive = gEmissiveColor.rgb;
    float3 Lo = 0.0f;

    [unroll]
    for (int i = 0; i < 2; ++i)
    {
        float3 L = gLightDir[i];
        float3 H = normalize(L + V);

        float NdotL = max(0.0f, dot(N, L));
        float NdotH = max(0.0f, dot(N, H));

        float3 diffuse = baseColor * NdotL;

        float shininess = gSpecularAlbedo.w;
        float3 specular = gSpecularAlbedo.rgb * pow(NdotH, shininess) * (NdotL > 0.0f ? 1.0f : 0.0f);

        Lo += (diffuse + specular) * gLightColor[i] * gLightIntensity[i];
    }

    float3 color = ambient * 0.3f + Lo + emissive;
    color = pow(saturate(color), 1.0f / 2.2f);

    return float4(color, gDiffuseAlbedo.a);
}
