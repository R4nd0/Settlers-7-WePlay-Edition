#pragma once
namespace GoldGraphics {
// Verified x86 DXVK payload already used by the Gold preparation tool.
constexpr char RendererHash[] = "265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa";
// Gold creates a 1024x768 profile with reduced effects when this is absent.
// These are Gold's native option names; no History-only renderer hooks.
constexpr char Defaults[] =
    "[Display]\r\nBillboardClouds = 0\r\nClutter = 1\r\nParticles = 1\r\nSky = 1\r\n"
    "\r\n[Effects]\r\nAntialiasing = 1\r\nDepthOfField = 0\r\nFog = 1\r\n"
    "Multisampling = 0\r\nParallax = 1\r\nSSAO = 1\r\nShadows = 1\r\nSunShafts = 1\r\n"
    "\r\n[Quality]\r\nFullScreen = 0\r\nScreenWidth = 1920\r\nScreenHeight = 1080\r\n"
    "ClutterQuality = 2\r\nDistanceCulling = 2\r\nObjectQuality = 2\r\nShaderQuality = 2\r\n"
    "ShadowQuality = 2\r\nTerrainDetailMaps = 1\r\nTextureQuality = 2\r\nWaterQuality = 2\r\n";
}
