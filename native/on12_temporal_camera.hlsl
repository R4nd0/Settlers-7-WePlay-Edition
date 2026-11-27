// R32F clipW-normalized scene depth -> view distance and camera-only motion.
// Confidence must be supplied by the scene: dynamic/unclassified geometry is
// zero. This pass does not invent velocities for moving trees or characters.
Texture2D<float> NormalizedDepth : register(t0);
Texture2D<float> StaticConfidence : register(t1);
cbuffer CameraParameters : register(b0){
    row_major float4x4 PreviousClipFromXyw;
    uint Width;uint Height;float2 Jitter;
    float DepthScale;float2 RasterOrigin;float Reserved;
}
struct Vertex {float4 position:SV_Position;};
struct Result {float4 motion:SV_Target0;float depth:SV_Target1;};
Vertex VS(uint id:SV_VertexID){Vertex v;float2 uv=float2((id<<1)&2,id&2);v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v;}
Result PS(Vertex v){
    int2 pixel=int2(v.position.xy);Result output;output.motion=0;output.depth=0;
    float normalized=NormalizedDepth.Load(int3(pixel,0));
    if(!isfinite(normalized)||normalized<=0||normalized>=1)return output;
    float z=normalized*DepthScale;if(!isfinite(z)||z<=0)return output;output.depth=z;
    float confidence=StaticConfidence.Load(int3(pixel,0));if(!isfinite(confidence)||confidence<=0)return output;
    float2 current=float2(pixel)-Jitter;
    float2 ndc=(current+RasterOrigin)/float2(Width,Height)*float2(2,-2)+float2(-1,1);
    float4 clip=mul(PreviousClipFromXyw,float4(ndc*z,z,1));
    if(!all(isfinite(clip))||clip.w<=0||clip.z<0||clip.z>clip.w)return output;
    float2 previous=(clip.xy/clip.w*float2(0.5,-0.5)+0.5)*float2(Width,Height)-RasterOrigin;
    if(!all(isfinite(previous)))return output;
    output.motion=float4(previous-current,clip.w,saturate(confidence));return output;
}
