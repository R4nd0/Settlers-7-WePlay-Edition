import hashlib
import io
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/midsummer'), str(ROOT / 'tools/lan'), str(ROOT / 'tools/installer')]
spec = importlib.util.spec_from_file_location('midsummer_profile', ROOT / 'tools/midsummer/profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
import bba
import archive_append
import reshade_runtime


def environment_fixture():
    root = ET.Element('EnvironmentSet')
    ET.SubElement(root, 'Name').text = 'Neutral'
    ET.SubElement(root, 'Duration').text = '1200000.'
    for name, numbers in {**profile.CURVES, 'WindStrength': (.35, .45)}.items():
        item = ET.SubElement(root, name)
        for field, value in (('Duration', '1200000.'), ('Loop', 'true'), ('CurrentTime', '17'),
                             ('LastUpdatetTime', '17'), ('StartTime', '0')):
            ET.SubElement(item, field).text = value
        keys = ET.SubElement(item, 'KeyFrames')
        for at, number in zip((0, 1200000), numbers):
            key = ET.SubElement(keys, 'Key')
            ET.SubElement(key, 'Time').text = str(at)
            value = ET.SubElement(key, 'Value')
            if isinstance(number, tuple):
                for channel, x in zip('RGBA', number):
                    ET.SubElement(value, channel).text = str(x)
            else:
                value.text = str(number)
            ET.SubElement(key, 'TangentToPrev').text = '0.'
            ET.SubElement(key, 'TangentToNext').text = '0.'
    return ET.tostring(root)


def archive_fixture():
    raw = bytearray(b'BAF\x06' + b'\0' * 28)
    struct.pack_into('<I', raw, 8, 16)
    entries, records = bytearray(), []
    # Force collisions at the desired new resource's native table slot.
    names = ['Config/Environment/Neutral.xml']
    wanted = bba.crc32(profile.RESOURCE.replace('/', '\\').encode()) % 32
    for i in range(10000):
        name = 'Fixture/%d.xml' % i
        if bba.crc32(name.replace('/', '\\').encode()) % 32 == wanted:
            names.append(name)
        if len(names) == 4:
            break
    for name in names:
        data = environment_fixture() if name == names[0] else b'<fixture/>'
        spelled = name.replace('/', '\\').encode()
        size = len(spelled) + 1
        padded = ((size + 3) & ~3) + (4 if size % 4 == 0 else 0)
        encoded = bba._gzip_xor(bba.canonical_gzip(data), size, True)
        values = [0] * 16
        values[2:5] = len(data), bba.crc32(data), 17
        values[6], values[8:11] = len(raw), [len(encoded), bba.crc32(encoded), bba.MIB]
        values[12:16] = size, name.rfind('/') + 1, 0xffffffff, 0xffffffff
        offset = len(entries)
        records.append((spelled, offset))
        entry = struct.pack('<16I', *values) + spelled + b'\0' * (padded - len(spelled))
        entries.extend(entry)
        if name != names[-1]:
            struct.pack_into('<I', entries, offset + 60, len(entries))
        raw.extend(encoded)
    table = [(0, 0)] * 32
    for spelled, offset in records:
        hashed = bba.crc32(spelled)
        slot = hashed % len(table)
        while table[slot][0]:
            slot = (slot + 1) % len(table)
        table[slot] = hashed, offset
    directory = bytearray(272)
    struct.pack_into('<3I', directory, 0, 64, 272, 276 + len(entries))
    directory.extend(struct.pack('<I', len(names)) + entries + struct.pack('<I', len(table)))
    for row in table:
        directory.extend(struct.pack('<2I', *row))
    encoded = bba.crypt(directory, bba.FILE_KEYS, 1532281822, 8, True)
    header = struct.pack('<4I', len(raw), 0, len(encoded), bba.crc32(encoded))
    raw[16:32] = bba.crypt(header, bba.HEAD_KEYS, 1231853475, 7, True)
    return bytes(raw + encoded)


class MidsummerResourceTests(unittest.TestCase):
    def test_curves_loop_brightly_and_preserve_unrelated_wind(self):
        source = environment_fixture()
        result = ET.fromstring(profile.environment(source))
        self.assertEqual(result.findtext('Duration'), '1200000')
        for name, ends in profile.CURVES.items():
            keys = result.findall(name + '/KeyFrames/Key')
            self.assertEqual([float(k.findtext('Time')) for k in keys], [0, 600000, 1200000])
            self.assertEqual(ET.tostring(keys[0].find('Value')), ET.tostring(keys[-1].find('Value')))
            self.assertEqual(result.findtext(name + '/Loop'), 'true')
        self.assertEqual([float(k.findtext('Value')) for k in result.findall('SunElevation/KeyFrames/Key')], [14, 48, 14])
        self.assertGreaterEqual(min(float(k.findtext('Value')) for k in result.findall('HDRExposure/KeyFrames/Key')), 2.6)
        self.assertEqual([float(k.findtext('Value')) for k in result.findall('WindStrength/KeyFrames/Key')], [.35, .45])
        scaled = ET.fromstring(profile.environment(source, period_ms=12000))
        self.assertEqual(scaled.findtext('SunElevation/KeyFrames/Key[2]/Time'), '6000')

    def test_resource_addition_keeps_old_payloads_and_native_collision_lookup(self):
        raw = archive_fixture()
        original = bba.Archive(raw)
        with patch.object(profile, 'SOURCE_SHA256', hashlib.sha256(raw).hexdigest()):
            result = profile.build(raw)
        added = bba.Archive(result)
        self.assertEqual(len(added.entries), len(original.entries) + 1)
        for name in original.entries:
            self.assertEqual(original.read(name), added.read(name))
        # Native lookup stops at the first empty slot; a round-trip parser by
        # itself would not catch the arbitrary-empty-slot bug.
        hashed = bba.crc32(profile.RESOURCE.replace('/', '\\').encode())
        count = struct.unpack_from('<I', added.directory, added.hash_table_offset)[0]
        slot = hashed % count
        occupied = 0
        while True:
            h, at = struct.unpack_from('<2I', added.directory, added.hash_table_offset + 4 + slot * 8)
            self.assertNotEqual(h, 0)
            if h == hashed:
                self.assertEqual(at + 276, added.entries[profile.RESOURCE].directory_offset)
                break
            occupied += 1
            slot = (slot + 1) % count
        self.assertGreaterEqual(occupied, 3)
        last = list(original.entries.values())[-1]
        self.assertEqual(added.entries[last.name].values[15] + 276, added.entries[profile.RESOURCE].directory_offset)

    def test_rejects_unknown_sources_duplicates_and_invalid_documents(self):
        with self.assertRaises(ValueError):
            profile.build(archive_fixture())
        raw = archive_fixture()
        with self.assertRaises(ValueError):
            archive_append.append_resource(raw, 'Config/Environment/Neutral.xml', b'x')
        for name in ('../escape.xml', 'Config/Environment/../escape.xml', 'Config/Environment/a:b.xml'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                archive_append.append_resource(raw, name, b'x')
        for period in (True, 0, 500, '12000', float('nan')):
            with self.subTest(period=period), self.assertRaises(ValueError):
                profile.environment(environment_fixture(), period_ms=period)


class ReShadePreparationTests(unittest.TestCase):
    def test_normal_exit_defaults_allow_a_second_launch(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as path:
            root = Path(path)
            dll = b'invented x86 runtime fixture'
            packed = io.BytesIO()
            with zipfile.ZipFile(packed, 'w') as archive:
                archive.writestr('ReShade32.dll', dll)
                archive.writestr('ReShade32.json', '{"layer":{"name":"VK_LAYER_reshade","library_path":".\\\\ReShade32.dll"}}')
            setup = root / 'setup.exe'
            setup.write_bytes(packed.getvalue())
            with patch.object(reshade_runtime, 'SETUP_SHA256', hashlib.sha256(packed.getvalue()).hexdigest()), \
                 patch.object(reshade_runtime, 'DLL_SHA256', hashlib.sha256(dll).hexdigest()), \
                 patch('graphics_profile.ensure_closed'):
                folder = reshade_runtime.prepare(root, setup=setup)
                (folder / 'ReShade.ini').write_bytes(reshade_runtime.CONFIG + b'\n[STYLE]\nAlpha=1.0\n')
                self.assertEqual(reshade_runtime.verify(root), folder)
                with patch('urllib.request.urlopen', side_effect=AssertionError('second download')):
                    self.assertEqual(reshade_runtime.prepare(root), folder)

    def test_invalid_download_never_installs_or_replaces_a_renderer(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as path:
            root = Path(path)
            (root / 'LocalData').mkdir()
            setup = root / 'unknown.exe'
            setup.write_bytes(b'not the official installer')
            with patch('graphics_profile.ensure_closed'), self.assertRaises(ValueError):
                reshade_runtime.prepare(root, setup=setup)
            self.assertFalse((root / reshade_runtime.FOLDER).exists())
            self.assertFalse((root / 'Base/Bin/d3d9.dll').exists())

    def test_layer_activation_is_child_only_and_preserves_unrelated_layers(self):
        parent = {'PATH': 'fixture', 'VK_LAYER_PATH': 'other', 'VK_INSTANCE_LAYERS': 'other-layer'}
        with patch.object(reshade_runtime, 'verify', return_value=Path('fixture-tone')):
            env = reshade_runtime.launch_environment(Path('root'), parent)
        self.assertEqual(parent, {'PATH': 'fixture', 'VK_LAYER_PATH': 'other', 'VK_INSTANCE_LAYERS': 'other-layer'})
        self.assertEqual(env['VK_INSTANCE_LAYERS'].split(os.pathsep), ['other-layer', 'VK_LAYER_reshade'])
        self.assertEqual(env['VK_LAYER_PATH'], 'fixture-tone' + os.pathsep + 'other')


try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None


@unittest.skipIf(LuaRuntime is None, 'Lua 5.1 runtime not installed')
class MidsummerLuaTests(unittest.TestCase):
    def backend(self, missing=False):
        lua = LuaRuntime()
        lua.execute('''C={selected=1,loads=0,blends=0,updates=0,sets=0,notices=0,handle=11}
G={game={started=true},Game={InMapMode=function() return true end},GFX={},ui={}}
G.GFX.EnvSetLoad=function(name) assert(name=="WePlayMidsummer");C.loads=C.loads+1;return C.handle end
G.GFX.EnvSetBlend=function(handle,duration) C.blends=C.blends+1;C.duration=duration end
G.ui.messageBox=function() C.notices=C.notices+1 end
G.weather={mode=3,modes={skirmish=3,campaign=2,menu=4},neutral=1,id=1,isCreated=function() return true end,
 update=function() C.updates=C.updates+1 end,set=function() C.sets=C.sets+1 end,setByName=function() C.sets=C.sets+1 end}
settings={id={weather=12},get=function(id) assert(id==12);return C.selected end}
options={weatherModes={midsummer=5}}
''')
        lua.globals().C.handle = 1 if missing else 11
        lua.execute((ROOT / 'tools/midsummer/runtime.lua').read_text(encoding='utf-8'),
                    lua.globals().G, lua.globals().options, lua.globals().settings)
        return lua

    def test_on_off_campaign_cues_and_menu_scope(self):
        lua = self.backend()
        lua.execute('G.weather.update();assert(C.updates==1 and C.loads==0)')
        lua.execute('C.selected=5;G.weather.update();G.weather.update();G.weather.set(99);G.weather.setByName("Night")')
        self.assertEqual((lua.globals().C.loads, lua.globals().C.blends, lua.globals().C.sets), (1, 1, 0))
        self.assertEqual(lua.globals().C.duration, 5000)
        lua.execute('C.selected=1;G.weather.update();assert(C.sets==1 and C.updates==2);G.weather.mode=4;C.selected=5;G.weather.update()')
        self.assertEqual(lua.globals().C.updates, 3)

    def test_native_neutral_fallback_is_detected_once(self):
        lua = self.backend(missing=True)
        lua.execute('C.selected=5;G.weather.update();G.weather.update()')
        self.assertEqual((lua.globals().C.loads, lua.globals().C.notices, lua.globals().C.sets), (1, 1, 1))


if __name__ == '__main__':
    unittest.main()
