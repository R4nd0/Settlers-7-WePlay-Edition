"""Exact native temporal edits layered on the pinned water/worker source variant."""
import hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PINS={
    'include/9on12Shader.h':'fe6548aed53e7e581ef5241a4a3fd180068cf4e485619bad4c2c66ceb97922cb',
    'src/9on12Shader.cpp':'3a301a2b207516d13f94a84770fd9dbee0f138b589d0d575598b8e26565dd5cc',
    'src/9on12Device.cpp':'7809e2911db7d87ee0a031b81552fd02ee15208d82499942b4448af714a571b9',
    'include/9on12VertexStage.h':'f65f6a8196caebd29470b2e278a09a527f5390331dfec5a052eb30f54b65849b',
    'src/9on12VertexStage.cpp':'a8a56dbf066905e471f99eb63eeb451ba304c284400a5a7ca86b61ba9540033d',
    'src/9on12PipelineState.cpp':'3baac1a34ae2d185c86e928e1b9c9ba171d0c5182c3cc2d1646f5e43292af807',
    'src/d3d9on12.def':'dcc565accc8ee9c56b14d77e124ba39be23da6c72147b945c0eb98ddbdaa3b37',
}
EDITS={
 'include/9on12Shader.h':[
    ('        // Get the shader for pre-Transformed and Lit vertices (essentially a pass through).',
     '        bool HasSceneProjection() const { return m_sceneProjection; }\r\n\r\n        // Get the shader for pre-Transformed and Lit vertices (essentially a pass through).'),
    ('        bool m_waterCompat = false;\r\n    };\r\n\r\n    class GeometryShader',
     '        bool m_waterCompat = false;\r\n        bool m_sceneProjection = false;\r\n    };\r\n\r\n    class GeometryShader')],
 'src/9on12Shader.cpp':[
    ('#include "on12_water_compat.h"','#include "on12_water_compat.h"\r\n#include "on12_scene_projection.h"'),
    ('m_waterCompat = On12WaterCompat::Vertex(&byteCode, byteCodeLength);',
     'm_waterCompat = On12WaterCompat::Vertex(&byteCode, byteCodeLength); m_sceneProjection = On12Graphics::SceneProjectionShader(&byteCode, byteCodeLength);')],
 'src/9on12Device.cpp':[
    ('        On12WaterCompat::DeviceReady();',
     '        On12WaterCompat::DeviceReady();\r\n        m_pipelineState.GetVertexStage().GetTemporalState().Register(&GetDevice(), this, [](void* cookie) {\r\n            static_cast<Device*>(cookie)->GetPipelineState().MarkPipelineStateNeeded();\r\n        });')],
 'include/9on12VertexStage.h':[
    ('#pragma once','#pragma once\r\n#include "on12_native_temporal.h"'),
    ('        VertexShader *GetCurrentD3D9VertexShader() { return m_pCurrentVS; }',
     '        VertexShader *GetCurrentD3D9VertexShader() { return m_pCurrentVS; }\r\n        On12NativeTemporal::State& GetTemporalState() { return m_temporal; }'),
    ('        VSCBExntensionWrapper   m_VSExtension;',
     '        VSCBExntensionWrapper   m_VSExtension;\r\n        On12NativeTemporal::State m_temporal;\r\n        bool m_temporalOffsetActive = false;\r\n        float m_temporalBaseScaleX = 0, m_temporalBaseScaleY = 0;')],
 'src/9on12VertexStage.cpp':[
    ('        m_VSExtension.SetViewportScale(\r\n            1.0f / m_currentViewPort.Width,\r\n            -1.0f / m_currentViewPort.Height);',
     '        m_temporalBaseScaleX = 1.0f / m_currentViewPort.Width;\r\n        m_temporalBaseScaleY = -1.0f / m_currentViewPort.Height;\r\n        m_VSExtension.SetViewportScale(m_temporalBaseScaleX, m_temporalBaseScaleY);'),
    ('        if (m_dirtyFlags.PointSize)\r\n',
     '        if (m_temporal.Enabled() || m_temporalOffsetActive)\r\n        {\r\n            float x = m_temporalBaseScaleX, y = m_temporalBaseScaleY;\r\n            const bool scene = !inputLayout.VerticesArePreTransformed() && m_pCurrentVS && m_pCurrentVS->HasSceneProjection();\r\n            m_temporalOffsetActive = m_temporal.Adjust(scene, m_currentViewPort.Width, m_currentViewPort.Height, x, y);\r\n            m_VSExtension.SetViewportScale(x, y);\r\n        }\r\n\r\n        if (m_dirtyFlags.PointSize)\r\n')],
 'src/9on12PipelineState.cpp':[
    ('        if (!m_bNeedsPipelineState) return S_OK;',
     '        m_vertexStage.GetTemporalState().ObserveOwner();\r\n        if (!m_bNeedsPipelineState) return S_OK;')],
 'src/d3d9on12.def':[
    ('    WePlayOn12WaterQuery=_WePlayOn12WaterQuery@4',
     '    WePlayOn12WaterQuery=_WePlayOn12WaterQuery@4\r\n    WePlayOn12TemporalControl=_WePlayOn12TemporalControl@4\r\n    WePlayOn12TemporalQuery=_WePlayOn12TemporalQuery@4')],
}
ADDED={'include/on12_native_temporal.h':'native/on12_native_temporal.h',
       'include/on12_scene_projection.h':'native/on12_scene_projection.h',
       'src/on12_native_temporal.cpp':'native/on12_native_temporal.cpp'}
INPUTS=('tools/graphics_lab/on12_temporal_patch.py','native/on12_native_temporal_gpu.cpp','native/on12_jitter_program.h',*ADDED.values())


def patched(name,raw):
    # The original Microsoft files remain MIT licensed in the pinned archive.
    if hashlib.sha256(raw).hexdigest()!=PINS[name]:raise ValueError('Native temporal edit requires the exact water/worker source.')
    value=raw.decode('utf-8')
    for before,after in EDITS[name]:
        if value.count(before)!=1:raise ValueError('Native temporal source context is missing or ambiguous.')
        value=value.replace(before,after,1)
    return value.encode('utf-8')


def apply(directory):
    source=directory/'sources/D3D9On12'
    for name in EDITS:
        path=source/name;path.write_bytes(patched(name,path.read_bytes()))
    for name,original in ADDED.items():
        path=source/name
        if path.exists():raise ValueError('Native temporal source already exists.')
        path.write_bytes((ROOT/original).read_bytes())
