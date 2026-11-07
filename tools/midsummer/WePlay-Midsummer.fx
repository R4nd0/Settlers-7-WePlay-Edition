// WePlay Community. Original SDR grading, edge smoothing and depth contacts.
// Independent options. GPU-only atmosphere; no game state or game RNG is used.
#include "WePlay-World.fxh"
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
uniform float MaximumStrength < hidden = true; > = 0.0;
uniform float AtmosphereTime < source = "timer"; >;
uniform float HazeStrength < ui_type = "slider"; ui_min = 0.0; ui_max = 0.6;
    ui_label = "Distance haze"; > = 0.42;
uniform float HeatStrength < ui_type = "slider"; ui_min = 0.0; ui_max = 2.0;
    ui_label = "Warm surface heat shimmer (pixels)"; > = 0.85;

texture2D BloomHorizontal { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
sampler2D BloomHorizontalSampler { Texture = BloomHorizontal; MinFilter = LINEAR; MagFilter = LINEAR; };
texture2D BloomSoft { Width = BUFFER_WIDTH / 4; Height = BUFFER_HEIGHT / 4; Format = RGBA16F; };
sampler2D BloomSoftSampler { Texture = BloomSoft; MinFilter = LINEAR; MagFilter = LINEAR; };

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
    return WePlayWorldMask(uv);
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

// Optional cinematic profile. Intermediate buffers are used only when this
// technique is selected. Every geometric operation keeps the same HUD mask.
float3 Highlight(float2 uv)
{
    float3 c=ColorAt(uv);
    return c*smoothstep(.68,.95,Luma(c))*WorldMask(uv);
}
float4 BloomHorizontalPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 p=float2(BUFFER_RCP_WIDTH*4.0,0);
    float3 c=Highlight(uv)*.28;
    c+=(Highlight(uv+p)+Highlight(uv-p))*.22;
    c+=(Highlight(uv+p*2.5)+Highlight(uv-p*2.5))*.14;
    return float4(c,1.0);
}
float4 BloomVerticalPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 p=float2(0,BUFFER_RCP_HEIGHT*4.0);
    float3 c=tex2D(BloomHorizontalSampler,uv).rgb*.28;
    c+=(tex2D(BloomHorizontalSampler,uv+p).rgb+tex2D(BloomHorizontalSampler,uv-p).rgb)*.22;
    c+=(tex2D(BloomHorizontalSampler,uv+p*2.5).rgb+tex2D(BloomHorizontalSampler,uv-p*2.5).rgb)*.14;
    return float4(c,1.0);
}
float4 CinematicPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 original=tex2D(SceneSampler,uv);
    float mask=WorldMask(uv)*MaximumStrength;
    float2 pixel=float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT);
    float3 center=original.rgb;
    float3 nearMean=(ColorAt(uv+float2(pixel.x*2,0))+ColorAt(uv-float2(pixel.x*2,0))
        +ColorAt(uv+float2(0,pixel.y*2))+ColorAt(uv-float2(0,pixel.y*2)))*.25;
    float3 broadMean=(ColorAt(uv+float2(pixel.x*9,0))+ColorAt(uv-float2(pixel.x*9,0))
        +ColorAt(uv+float2(0,pixel.y*9))+ColorAt(uv-float2(0,pixel.y*9)))*.25;
    // Bounded fine sharpening and a wider local-contrast component.
    float3 detail=clamp((center-nearMean)*.22+(center-broadMean)*.16,-.035,.035);
    float3 color=center+detail;
    float light=Luma(color);
    // A restrained S curve, cool low values and warm highlights give the
    // cinematic option a visibly different grade without crushing dark detail.
    color=(color-.45)*1.10+.45;
    color+=float3(-.012,.002,.022)*(1.0-light)*(1.0-light);
    color+=float3(.028,.012,-.006)*smoothstep(.45,.9,light);
    color=lerp(Luma(color).xxx,color,1.035);
    color+=tex2D(BloomSoftSampler,uv).rgb*.15;
    color/=1.0+max(color-.82,0.0)*.20;
    return float4(lerp(original.rgb,saturate(color),mask),original.a);
}

float4 AtmospherePS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 original=tex2D(SceneSampler,uv);
    if(!DepthReady || MaximumStrength<=0.0) return original;
    float raw=RawDepth(uv);
    if(raw<=.000001 || raw>=.999999) return original;
    float z=ViewDepth(uv);
    float mask=WorldMask(uv)*MaximumStrength;
    float3 c=original.rgb;
    float light=Luma(c);
    // Screen-space proxy for warm lit ground, deliberately excludes green
    // foliage and cool water. This is optical refraction, not material physics.
    float warm=smoothstep(.015,.15,c.r-c.b)*(1.0-smoothstep(.015,.09,c.g-c.r));
    float lit=smoothstep(.26,.62,light);
    float distanceBand=smoothstep(24.0,70.0,z)*(1.0-smoothstep(500.0,1500.0,z));
    float edge=1.0-smoothstep(.004,.025,max(abs(ddx(z)),abs(ddy(z)))/max(z,1.0));
    float time=AtmosphereTime*.001;
    float2 ripple=float2(sin(uv.y*281.0+time*2.1)+sin(uv.y*137.0-time*1.7),
                        sin(uv.x*173.0+uv.y*91.0-time*1.9))*.5;
    float2 offset=ripple*float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT)
        *HeatStrength*warm*lit*distanceBand*edge*mask*WePlayRefractionMask(uv);
    float2 at=clamp(uv+offset,0.0,1.0);
    // Never pull a foreground silhouette or HUD across a depth discontinuity.
    float compatible=(abs(ViewDepth(at)-z)<max(z*.006,1.0) ? 1.0:0.0)*WePlayRefractionMask(at);
    c=lerp(c,ColorAt(at),compatible);
    // Calibrated to S7's conventional depth: the close/middle/far castle
    // fixture spans roughly 40--200 in these inverse-depth units.
    float opticalDepth=max(z-45.0,0.0)/180.0;
    float haze=(1.0-exp2(-opticalDepth))*HazeStrength;
    // Broad warm forward scattering in sunlit scenery, cooler air in shade.
    float3 air=lerp(float3(.62,.71,.79),float3(.87,.76,.60),smoothstep(.3,.75,light));
    c=lerp(c,air,saturate(haze)*mask);
    return float4(c,original.a);
}

technique WePlay_Maximum < ui_label = "WePlay Maximum - cinematic"; >
{
    pass BloomX { VertexShader=FullscreenVS; PixelShader=BloomHorizontalPS; RenderTarget=BloomHorizontal; }
    pass BloomY { VertexShader=FullscreenVS; PixelShader=BloomVerticalPS; RenderTarget=BloomSoft; }
    pass Cinematic { VertexShader=FullscreenVS; PixelShader=CinematicPS; }
    pass Atmosphere { VertexShader=FullscreenVS; PixelShader=AtmospherePS; }
}
