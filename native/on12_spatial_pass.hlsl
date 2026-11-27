// Native D3D12 spatial AA and resampling. No original game shader code.
Texture2D<float4> Scene : register(t0);
SamplerState LinearClamp : register(s0);
cbuffer Parameters : register(b0) { float2 InvSourceSize; float Strength; float Reconstruct; }
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
float Luma(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }
float4 CubicWeights(float t) {
    float t2 = t * t, t3 = t2 * t;
    return float4(-0.5 * t + t2 - 0.5 * t3, 1 - 2.5 * t2 + 1.5 * t3,
                  0.5 * t + 2 * t2 - 1.5 * t3, -0.5 * t2 + 0.5 * t3);
}
float4 ReconstructColor(float2 uv) {
    // Cubic reconstruction with a local range clamp. The clamp prevents
    // negative-lobe ringing at hard edges and preserves flat-color regions.
    int2 dimensions = int2(round(1.0 / InvSourceSize));
    float2 position = uv / InvSourceSize - 0.5;
    int2 base = int2(floor(position));
    float2 phase = position - floor(position);
    float4 wx = CubicWeights(phase.x), wy = CubicWeights(phase.y);
    float4 result = 0, low = 1, high = 0;
    [unroll] for (int y = 0; y < 4; ++y) {
        [unroll] for (int x = 0; x < 4; ++x) {
            float4 value = Scene.Load(int3(clamp(base + int2(x - 1, y - 1), int2(0, 0), dimensions - 1), 0));
            result += value * wx[x] * wy[y];
            if (x >= 1 && x <= 2 && y >= 1 && y <= 2) { low = min(low, value); high = max(high, value); }
        }
    }
    return clamp(result, low, high);
}
float4 PS(Vertex input) : SV_Target {
    float4 center = Reconstruct != 0 ? ReconstructColor(input.uv) : Scene.SampleLevel(LinearClamp, input.uv, 0);
    if (Strength == 0) return center;
    float3 nw = Scene.SampleLevel(LinearClamp, input.uv + float2(-1, -1) * InvSourceSize, 0).rgb;
    float3 ne = Scene.SampleLevel(LinearClamp, input.uv + float2( 1, -1) * InvSourceSize, 0).rgb;
    float3 sw = Scene.SampleLevel(LinearClamp, input.uv + float2(-1,  1) * InvSourceSize, 0).rgb;
    float3 se = Scene.SampleLevel(LinearClamp, input.uv + float2( 1,  1) * InvSourceSize, 0).rgb;
    float a = Luma(nw), b = Luma(ne), c = Luma(sw), d = Luma(se), m = Luma(center.rgb);
    float low = min(m, min(min(a, b), min(c, d)));
    float high = max(m, max(max(a, b), max(c, d)));
    if (high - low < max(0.03125, high * 0.125)) return center;
    float2 direction = float2(-((a + b) - (c + d)), (a + c) - (b + d));
    float reduce = max((a + b + c + d) * 0.03125, 0.0078125);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + reduce), -8, 8) * InvSourceSize;
    float3 narrow = 0.5 * (Scene.SampleLevel(LinearClamp, input.uv - direction / 6, 0).rgb
                          + Scene.SampleLevel(LinearClamp, input.uv + direction / 6, 0).rgb);
    float3 wide = 0.5 * narrow + 0.25 * (Scene.SampleLevel(LinearClamp, input.uv - direction * 0.5, 0).rgb
                                      + Scene.SampleLevel(LinearClamp, input.uv + direction * 0.5, 0).rgb);
    float value = Luma(wide);
    float3 filtered = value < low || value > high ? narrow : wide;
    return float4(lerp(center.rgb, filtered, Strength), center.a);
}
