// Authored temporal resolve. Scene color only; UI is composed afterwards.
Texture2D<float4> Current : register(t0);
Texture2D<float4> History : register(t1);       // Linear RGB, never quantized LDR.
Texture2D<float4> Motion : register(t2);        // prev-current pixels, expected previous Z, confidence.
Texture2D<float> Depth : register(t3);          // Current-view linear positive Z.
Texture2D<float> HistoryDepth : register(t4);   // Previous-view linear positive Z.
Texture2D<float> Reactive : register(t5);
SamplerState LinearClamp : register(s0);
cbuffer Parameters : register(b0) {
    uint Width; uint Height; uint HistoryValid; uint ColorSrgb;
    float2 Jitter; float2 PreviousJitter;
    float HistoryMax; float HistoryMin; float DepthRelative; float DepthAbsolute;
    float VarianceGamma; float MotionScale; float ReactiveScale; float Reserved;
}
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
struct Result {float4 color:SV_Target0;float4 history:SV_Target1;float depth:SV_Target2;};
Vertex VS(uint id:SV_VertexID){Vertex v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float3 Decode(float3 c){return ColorSrgb?float3(c.x<=0.04045?c.x/12.92:pow(max(0,(c.x+0.055)/1.055),2.4),c.y<=0.04045?c.y/12.92:pow(max(0,(c.y+0.055)/1.055),2.4),c.z<=0.04045?c.z/12.92:pow(max(0,(c.z+0.055)/1.055),2.4)):c;}
float3 Encode(float3 c){c=saturate(c);return ColorSrgb?float3(c.x<=0.0031308?12.92*c.x:1.055*pow(max(c.x,0),1.0/2.4)-0.055,c.y<=0.0031308?12.92*c.y:1.055*pow(max(c.y,0),1.0/2.4)-0.055,c.z<=0.0031308?12.92*c.z:1.055*pow(max(c.z,0),1.0/2.4)-0.055):c;}
float3 ToYCoCg(float3 c){return float3(dot(c,float3(0.25,0.5,0.25)),0.5*c.r-0.5*c.b,-0.25*c.r+0.5*c.g-0.25*c.b);}
float3 ToRgb(float3 c){return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
Result PS(Vertex v){
    int2 pixel=int2(v.position.xy),size=int2(Width,Height);
    float4 current=Current.Load(int3(pixel,0));float3 current_linear=Decode(current.rgb),resolved=current_linear;
    float z=Depth.Load(int3(pixel,0));
    if(HistoryValid){
        float4 motion=Motion.Load(int3(pixel,0));float reactive=Reactive.Load(int3(pixel,0));
        float2 previous=v.position.xy+motion.xy+PreviousJitter-Jitter;
        bool valid=isfinite(z)&&z>0&&all(isfinite(motion))&&motion.z>0&&motion.w>0&&isfinite(reactive)
            &&all(previous>=0.5)&&all(previous<=float2(size)-0.5);
        if(valid){
            int2 base=int2(floor(previous-0.5));float2 fraction=frac(previous-0.5);float4 history=0;
            [unroll]for(int by=0;by<2;++by)[unroll]for(int bx=0;bx<2;++bx){
                float weight=(bx?fraction.x:1-fraction.x)*(by?fraction.y:1-fraction.y);
                if(weight>0){int2 tap=clamp(base+int2(bx,by),int2(0,0),size-1);
                    float old_z=HistoryDepth.Load(int3(tap,0));float4 c=History.Load(int3(tap,0));
                    valid=valid&&isfinite(old_z)&&old_z>0&&abs(old_z-motion.z)<=max(DepthAbsolute,DepthRelative*motion.z)
                        &&all(isfinite(c))&&all(c>=0)&&all(c<=1);history+=c*weight;
                }
            }
            if(valid){
                float3 low=1,high=-1,mean=0,squares=0;
                [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
                    float3 c=ToYCoCg(Decode(Current.Load(int3(clamp(pixel+int2(x,y),int2(0,0),size-1),0)).rgb));
                    low=min(low,c);high=max(high,c);mean+=c/9;squares+=c*c/9;
                }
                float3 sigma=sqrt(max(0,squares-mean*mean));
                float3 clipped=clamp(ToYCoCg(history.rgb),max(low,mean-VarianceGamma*sigma),min(high,mean+VarianceGamma*sigma));
                float difference=abs(clipped.x-ToYCoCg(current_linear).x)/(high.x-low.x+0.001);
                float weight=clamp(HistoryMax/(1+2*difference+length(motion.xy)/MotionScale),HistoryMin,HistoryMax)
                    *(1-saturate(reactive*ReactiveScale))*saturate(motion.w);
                resolved=saturate(lerp(current_linear,ToRgb(clipped),weight));
            }
        }
    }
    Result output;output.color=float4(Encode(resolved),current.a);output.history=float4(resolved,current.a);output.depth=isfinite(z)&&z>0?z:0;return output;
}
