"""Exact source edits for the isolated, hash-scoped water translation variant."""
import hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
# Microsoft source remains MIT licensed in the pinned source archives.
EDITS={'include/9on12Constants.h': ('f7ad3bea19ed6ee19e697984638e275de6f7d73e6232d24dcd9f45b40674d2ab',
                              [(1952,
                                '',
                                '            bool Read(UINT first,void* output,UINT count) const {\r\n'
                                '                const auto capacity=m_data.size()/m_sizePerElement;\r\n'
                                '                if(!output||first>=capacity||count>capacity-first)return '
                                'false;\r\n'
                                '                '
                                'memcpy(output,m_data.data()+first*m_sizePerElement,count*m_sizePerElement);return '
                                'true;\r\n'
                                '            }\r\n'),
                               (7801, '};', '};\r\n')]),
 'include/9on12Shader.h': ('8d125945ad161cfaa84dc88a64bae1e6c2f253331dafcad87012cb13b90dd84c',
                           [(9235, '', '        bool IsWaterCompat() const { return m_waterCompat; }\r\n'),
                            (11372, '', '        bool m_waterCompat = false;\r\n'),
                            (13952, '', '        bool IsWaterCompat() const { return m_waterCompat; }\r\n'),
                            (15772, '', '        bool m_waterCompat = false;\r\n'),
                            (16049, '};', '};\r\n')]),
 'src/9on12Device.cpp': ('9029f4b968e4e5bbf08cb0ac785be803f2c0e5a91bee5a37cb8fa6dbec053e1b',
                         [(96, '', '#include "on12_water_compat.h"\r\n'),
                          (20825, '', '        On12WaterCompat::DeviceReady();\r\n'),
                          (22508, '};', '};\r\n')]),
 'src/9on12PipelineState.cpp': ('1c00ea26e4f1f877c3728b083bfa163cc050f48b8f5e2c4cce4e2f48108939e1',
                                [(96, '', '#include "on12_water_compat.h"\r\n'),
                                 (16941,
                                  '',
                                  '        auto* waterVS=m_vertexStage.GetCurrentD3D9VertexShader();\r\n'
                                  '        auto* waterPS=m_pixelStage.GetCurrentD3D9PixelShader();\r\n'
                                  '        '
                                  'if(waterVS&&waterPS&&waterVS->IsWaterCompat()&&waterPS->IsWaterCompat()) '
                                  '{\r\n'
                                  '            std::uint32_t scattering[32]{},camera[24]{};\r\n'
                                  '            auto& '
                                  'values=device.GetConstantsManager().GetVertexShaderConstants().GetConstantBufferData(ShaderConv::CB_FLOAT);\r\n'
                                  '            '
                                  'if(values.Read(87,scattering,8)&&values.Read(12,camera,6))On12WaterCompat::Observe(true,scattering,camera);\r\n'
                                  '        }\r\n')]),
 'src/9on12Shader.cpp': ('253afd8e4cafcadb8843aba5388faed31f464ee95aed56d8f8820dd98422e2f2',
                         [(96, '', '#include "on12_water_compat.h"\r\n'),
                          (3990,
                           '        Shader(parentDevice, byteCode, byteCodeLength, hash) {}\r\n',
                           '        Shader(parentDevice, byteCode, byteCodeLength, hash) { m_waterCompat = '
                           'On12WaterCompat::Vertex(&byteCode, byteCodeLength); }\r\n'),
                          (4270,
                           '        Shader(parentDevice, byteCode, byteCodeLength, hash) {}\r\n',
                           '        Shader(parentDevice, byteCode, byteCodeLength, hash) { m_waterCompat = '
                           'On12WaterCompat::Pixel(&byteCode, byteCodeLength); }\r\n'),
                          (18170,
                           '            bool applyAnythingTimes0Equals0 = '
                           'RegistryConstants::g_cAnythingTimes0Equals0 || '
                           '(g_AppCompatInfo.AnythingTimes0Equals0ShaderMask & '
                           'D3D9ON12_VERTEX_SHADER_MASK);\r\n',
                           '            bool applyAnythingTimes0Equals0 = m_waterCompat || '
                           'RegistryConstants::g_cAnythingTimes0Equals0 || '
                           '(g_AppCompatInfo.AnythingTimes0Equals0ShaderMask & '
                           'D3D9ON12_VERTEX_SHADER_MASK);\r\n'),
                          (19604,
                           '',
                           '            if (m_waterCompat && SUCCEEDED(hr)) '
                           'On12WaterCompat::Converted();\r\n'),
                          (36461, '};', '};\r\n')]),
 'src/CMakeLists.txt': ('ee420fd01affabeee166faf6e24a984c348a4d1b852d3c99cc84f1836ae41234',
                        [(893, '', 'target_link_libraries(d3d9on12 bcrypt)\r\n')]),
 'src/d3d9on12.def': ('ef574d34c8ce0c8951a55dcb7681180b6a9c254bb7118ccef94d2f884b6381d0',
                      [(83,
                        '    SetAppCompatData',
                        '    SetAppCompatData\r\n    WePlayOn12WaterQuery=_WePlayOn12WaterQuery@4\r\n')])}
ADDED={'include/on12_water_compat.h':'native/on12_water_compat.h',
       'src/on12_water_compat.cpp':'native/on12_water_compat.cpp'}
INPUTS=('tools/graphics_lab/on12_water_patch.py',*ADDED.values())

def patched(name,raw):
    pin,edits=EDITS[name]
    if hashlib.sha256(raw).hexdigest()!=pin:raise ValueError('Water patch requires its exact pinned upstream source.')
    text=raw.decode('utf-8')
    for offset,before,after in reversed(edits):
        if text[offset:offset+len(before)]!=before:raise ValueError('Water patch context changed.')
        text=text[:offset]+after+text[offset+len(before):]
    return text.encode('utf-8')

def apply(directory):
    for name in EDITS:
        path=directory/'sources/D3D9On12'/name;path.write_bytes(patched(name,path.read_bytes()))
    for name,source in ADDED.items():
        path=directory/'sources/D3D9On12'/name
        with path.open('xb')as output:output.write((ROOT/source).read_bytes())
