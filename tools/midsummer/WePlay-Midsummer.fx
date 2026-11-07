// WePlay Community. Original, depth-independent SDR grading for ReShade FX.
// Lighting and phase come from the native environment; this pass is static.
texture2D Scene : COLOR;
sampler2D SceneSampler { Texture = Scene; };
uniform float Strength < ui_type = "slider"; ui_min = 0.0; ui_max = 1.0;
    ui_label = "Mittsommer tone strength"; > = 0.65;

void FullscreenVS(uint vertex : SV_VertexID, out float4 position : SV_Position,
                  out float2 uv : TEXCOORD0)
{
    uv = float2((vertex << 1) & 2, vertex & 2);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 MidsummerPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 original = tex2D(SceneSampler, uv);
    float3 color = saturate(original.rgb);
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    // Gentle shadow lift, restrained saturation and a soft highlight shoulder.
    color += (1.0 - color) * (0.025 * (1.0 - luma) * (1.0 - luma));
    color = lerp(luma.xxx, color, 0.96);
    color /= 1.0 + max(color - 0.65, 0.0) * 0.08;
    return float4(lerp(original.rgb, saturate(color), Strength), original.a);
}

technique WePlay_Midsummer < ui_label = "WePlay Mittsommer"; >
{
    pass Grade { VertexShader = FullscreenVS; PixelShader = MidsummerPS; }
}
