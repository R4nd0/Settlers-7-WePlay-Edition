// SPDX-License-Identifier: MIT
// WePlay Community. Local HUD footprints, never whole edge strips.
#pragma once
uniform bool WePlayDepthReady < source = "bufready_depth"; > = false;
float WePlayHudRect(float2 uv,float2 lo,float2 hi)
{
    float2 feather=float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT)*2.0;
    float2 inside=smoothstep(lo-feather,lo,uv)*(1.0-smoothstep(hi,hi+feather,uv));
    return inside.x*inside.y;
}
float WePlayRefractionMask(float2 uv)
{
    float world=1.0;
    world*=1.0-WePlayHudRect(uv,float2(.003,.001),float2(.055,.060));
    world*=1.0-WePlayHudRect(uv,float2(.397,.001),float2(.604,.047));
    world*=1.0-WePlayHudRect(uv,float2(.300,.938),float2(.690,1.0));
    world*=1.0-WePlayHudRect(uv,float2(0.0,.973),float2(.095,1.0));
    world*=1.0-WePlayHudRect(uv,float2(.973,.963),float2(1.0,1.0));
    world*=1.0-WePlayHudRect(uv,float2(.951,.360),float2(1.0,.435));
    world*=1.0-WePlayHudRect(uv,float2(.951,.438),float2(1.0,.515));
    world*=1.0-WePlayHudRect(uv,float2(.951,.525),float2(1.0,.605));
    world*=1.0-WePlayHudRect(uv,float2(.951,.625),float2(1.0,.705));
    float2 minimap=(uv-float2(.945,.145))/float2(.068,.122);
    world*=smoothstep(.98,1.01,length(minimap));
    return world;
}
// Lighting and grading cover the entire frame. Only optical distortion uses
// HUD footprints; this avoids rectangular unprocessed patches in the world.
float WePlayWorldMask(float2 uv) { return 1.0; }
