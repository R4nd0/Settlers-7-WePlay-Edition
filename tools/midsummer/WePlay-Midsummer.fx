// WePlay Community. Original SDR grading, edge smoothing and depth contacts.
// All three are independent. No temporal history, RNG or game state is used.
texture2D Scene : COLOR;
sampler2D SceneSampler { Texture = Scene; MinFilter = LINEAR; MagFilter = LINEAR; };
texture2D Geometry : DEPTH;
sampler2D GeometrySampler { Texture = Geometry; MinFilter = POINT; MagFilter = POINT; };
uniform bool DepthReady < source = "bufready_depth"; > = false;
uniform float Strength < ui_type = "slider"; ui_min = 0.0; ui_max = 1.0;
    ui_label = "Natural tone"; > = 0.65;
uniform float EdgeStrength < ui_type = "slider"; ui_min = 0.0; ui_max = 1.0;
    ui_label = "Edge smoothing"; > = 0.0;
uniform float ContactStrength < ui_type = "slider"; ui_min = 0.0; ui_max = 0.5;
    ui_label = "Geometry contact shading (requires depth)"; > = 0.0;
uniform int DebugView < hidden = true; > = 0;

void FullscreenVS(uint vertex : SV_VertexID, out float4 position : SV_Position,
                  out float2 uv : TEXCOORD0)
{
    uv = float2((vertex << 1) & 2, vertex & 2);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float Luma(float3 color) { return dot(color, float3(0.2126, 0.7152, 0.0722)); }
float3 ColorAt(float2 uv) { return tex2D(SceneSampler, uv).rgb; }
float RawDepth(float2 uv) { return tex2Dlod(GeometrySampler, float4(uv, 0, 0)).r; }
// S7 perspective depth is qualified with the private depth-view fixture.
float ViewDepth(float2 uv) { return rcp(max(0.000001, 1.0 - RawDepth(uv))); }

float WorldMask(float2 uv)
{
    // Preserve the stock resource bar, right portraits/minimap and bottom HUD.
    return smoothstep(.08, .11, uv.y) * (1.0 - smoothstep(.86, .90, uv.y))
        * smoothstep(.015, .03, uv.x) * (1.0 - smoothstep(.88, .91, uv.x));
}

float3 SmoothEdges(float2 uv, float3 center)
{
    float2 pixel = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    float n=Luma(ColorAt(uv-float2(0,pixel.y)));
    float s=Luma(ColorAt(uv+float2(0,pixel.y)));
    float w=Luma(ColorAt(uv-float2(pixel.x,0)));
    float e=Luma(ColorAt(uv+float2(pixel.x,0)));
    float light=Luma(center);
    float hi=max(light,max(max(n,s),max(w,e)));
    float lo=min(light,min(min(n,s),min(w,e)));
    float contrast=hi-lo;
    // Bounded subpixel filtering along the local gradient. Low-contrast
    // texture detail is retained instead of blurring the complete image.
    float2 gradient=float2(e-w,s-n);
    float2 step=gradient * rsqrt(max(dot(gradient,gradient),0.000001)) * pixel * .65;
    float3 filtered=(ColorAt(uv-step)+2.0*center+ColorAt(uv+step))*.25;
    float weight=smoothstep(max(.075,hi*.12),max(.11,hi*.24),contrast);
    return lerp(center,filtered,weight*EdgeStrength);
}

float Contacts(float2 uv)
{
    // Missing/cleared/sky depth is a strict no-op, including standard ReShade
    // multiplayer depth gating. No runtime detection bypass is used.
    if (!DepthReady || ContactStrength <= 0.0) return 0.0;
    float raw=RawDepth(uv);
    if (raw <= .000001 || raw >= .999999) return 0.0;
    float z=ViewDepth(uv);
    float2 pixel=float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT);
    float2 dz=float2(ddx(z),ddy(z));
    // Silhouette derivatives must not create a black foreground outline.
    if (max(abs(dz.x),abs(dz.y)) > z*.035) return 0.0;
    float occlusion=0.0;
    [unroll] for (int i=0;i<8;++i)
    {
        float angle=(i+.5)*.78539816339;
        float2 direction=float2(cos(angle),sin(angle));
        [unroll] for (int j=1;j<=2;++j)
        {
            float2 offset=direction*(j*3.0);
            float sampleRaw=RawDepth(uv+offset*pixel);
            float sampleZ=ViewDepth(uv+offset*pixel);
            float plane=z+dot(dz,offset);
            float separation=(plane-sampleZ)/max(z,1.0);
            float nearContact=smoothstep(.0015,.012,separation)
                * (1.0-smoothstep(.035,.08,abs(separation)));
            occlusion+=nearContact * (sampleRaw>.000001 && sampleRaw<.999999 ? 1.0:0.0);
        }
    }
    return occlusion/16.0;
}

float4 MidsummerPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 original=tex2D(SceneSampler,uv);
    if (DebugView==1) return DepthReady ? float4(saturate((1.0-RawDepth(uv))*64.0).xxx,1.0):float4(1,0,1,1);
    float contacts=Contacts(uv)*WorldMask(uv);
    if (DebugView==2) return DepthReady ? float4(contacts.xxx,1.0):float4(1,0,1,1);
    float3 color=original.rgb;
    if (EdgeStrength>0.0) color=lerp(color,SmoothEdges(uv,color),WorldMask(uv));
    color*=1.0-ContactStrength*contacts;
    float3 toned=saturate(color);
    // A restrained cool white balance counters the stock ambient's warm cast
    // even where lowering direct-sun exposure reveals more ambient lighting.
    toned*=float3(.965,1.0,1.04);
    float luma=Luma(toned);
    float shadow=(1.0-luma)*(1.0-luma);
    toned+=(1.0-toned)*float3(.014,.020,.028)*shadow;
    toned=lerp(Luma(toned).xxx,toned,.95);
    toned/=1.0+max(toned-.62,0.0)*.13;
    return float4(lerp(color,saturate(toned),Strength),original.a);
}

technique WePlay_Midsummer < ui_label = "WePlay visual quality"; >
{
    pass Grade { VertexShader = FullscreenVS; PixelShader = MidsummerPS; }
}
