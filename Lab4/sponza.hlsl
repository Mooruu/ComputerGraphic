//======================================================================================
// SPONZA SHADER — чистая версия, без тумана, с правильным контрастом
//======================================================================================

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
}

struct VertexIn
{
    float3 PosL  : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC  : TEXCOORD;
};

struct VertexOut
{
    float4 PosH  : SV_POSITION;
    float3 PosW  : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC  : TEXCOORD;
};

// Два основных источника света (меньше — чётче)
static const float3 gLightDir[2] = {
    normalize(float3(1.0f, 2.0f, 1.0f)),   // основной
    normalize(float3(-1.0f, 1.0f, -1.0f))  // заполняющий
};

static const float3 gLightColor[2] = {
    float3(1.0f, 0.95f, 0.9f),   // тёплый
    float3(0.8f, 0.9f, 1.0f)     // холодный
};

static const float gLightIntensity[2] = { 0.9f, 0.5f };

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.TexC = vin.TexC;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Нормализация
    float3 N = normalize(pin.NormalW);
    
    // Базовый цвет камня
    float3 baseColor = float3(0.75f, 0.75f, 0.75f);
    
    // Накопление освещения
    float3 Lo = 0;
    
    [unroll]
    for (int i = 0; i < 2; i++)
    {
        float3 L = gLightDir[i];
        float NdotL = max(0.2f, dot(N, L));  // минимум 0.2 — чтобы тени не были чёрными
        
        float3 diffuse = baseColor * NdotL;
        Lo += diffuse * gLightColor[i] * gLightIntensity[i];
    }
    
    // Фоновое освещение (минимальное)
    float3 ambient = baseColor * 0.15f;
    
    // Финальный цвет
    float3 color = ambient + Lo;
    
    // Лёгкая гамма-коррекция
    color = pow(color, 1.0f / 2.0f);
    
    return float4(color, 1.0f);
}