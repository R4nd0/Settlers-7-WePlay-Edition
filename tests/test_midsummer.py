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
from contextlib import contextmanager, ExitStack
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/midsummer'), str(ROOT / 'tools/lan'), str(ROOT / 'tools/installer')]
spec = importlib.util.spec_from_file_location('midsummer_profile', ROOT / 'tools/midsummer/profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
import bba
import archive_append
import reshade_runtime


def archive_fixture_manifest():
    return b'{"layer":{"name":"VK_LAYER_reshade","library_path":".\\\\ReShade32.dll"}}'


def environment_fixture():
    root = ET.Element('EnvironmentSet')
    ET.SubElement(root, 'Name').text = 'Neutral'
    ET.SubElement(root, 'Duration').text = '1200000.'
    for name, numbers in {**profile.CURVES, **dict.fromkeys(profile.AZIMUTH_CURVES, (45., 45.)), 'WindStrength': (.35, .45)}.items():
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
        self.assertGreaterEqual(min(float(k.findtext('Value')) for k in result.findall('HDRExposure/KeyFrames/Key')), 2.45)
        self.assertEqual([float(k.findtext('Value')) for k in result.findall('WindStrength/KeyFrames/Key')], [.35, .45])
        scaled = ET.fromstring(profile.environment(source, period_ms=12000))
        self.assertEqual(scaled.findtext('SunElevation/KeyFrames/Key[2]/Time'), '6000')

    def test_sun_light_and_visible_disc_orbit_with_a_matching_loop_direction(self):
        for duration in (12000, profile.PERIOD_MS):
            result = ET.fromstring(profile.environment(environment_fixture(), period_ms=duration))
            for name in profile.AZIMUTH_CURVES:
                keys = result.findall(name + '/KeyFrames/Key')
                self.assertEqual([float(k.findtext('Time')) for k in keys], [duration*x for x in (0, .25, .5, .75, 1)])
                angles = [float(k.findtext('Value')) for k in keys]
                self.assertEqual(angles, [0, 90, 180, 270, 360])
                self.assertEqual(angles[0] % 360, angles[-1] % 360)
                self.assertTrue(all(float(k.findtext(tag)) == 0 for k in keys for tag in ('TangentToPrev', 'TangentToNext')))
                self.assertEqual(result.findtext(name + '/Loop'), 'true')

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
    @contextmanager
    def prepared(self, options):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as path, ExitStack() as stack:
            root=Path(path);clock=root/reshade_runtime.CLOCK_FILE
            clock.parent.mkdir(parents=True);clock.write_bytes(b'guarded clock fixture')
            dll=b'guarded runtime fixture';manifest=archive_fixture_manifest();packed=io.BytesIO()
            with zipfile.ZipFile(packed,'w') as archive:
                archive.writestr('ReShade32.dll',dll);archive.writestr('ReShade32.json',manifest)
            setup=root/'setup.exe';setup.write_bytes(packed.getvalue())
            for name,value in {'SETUP_SHA256':packed.getvalue(),'ADDON_SETUP_SHA256':packed.getvalue(),
                               'DLL_SHA256':dll,'ADDON_DLL_SHA256':dll,'MANIFEST_SHA256':manifest,
                               'CLOCK_SHA256':clock.read_bytes()}.items():
                stack.enter_context(patch.object(reshade_runtime,name,hashlib.sha256(value).hexdigest()))
            closed=stack.enter_context(patch('graphics_profile.ensure_closed'))
            folder=reshade_runtime.prepare(root,setup=setup,options=options)
            stack.enter_context(patch('urllib.request.urlopen',side_effect=AssertionError('unexpected second download')))
            yield root,folder,closed

    def test_smaa_alone_loads_only_spatial_aa_without_clock_or_other_effects(self):
        options = {'enhanced_aa': True}
        with self.prepared(options) as (root, folder, closed):
            preset = (folder/'WePlay-Midsummer.ini').read_bytes()
            self.assertEqual(preset.splitlines()[:2], [b'Techniques=SMAA@SMAA.fx',
                                                     b'TechniqueSorting=SMAA@SMAA.fx'])
            self.assertNotIn(b'WePlay', preset)
            self.assertNotIn(b'DH_UBER', preset)
            self.assertIn(b'PredicationEnabled=0', preset)
            self.assertEqual({p.name for p in (folder/'Shaders').iterdir()},
                             {'SMAA.fx', 'SMAA.fxh', 'ReShade.fxh', 'ReShadeUI.fxh'})
            self.assertEqual({p.name for p in (folder/'Textures').iterdir()},
                             {'AreaTex.png', 'SearchTex.png'})
            self.assertFalse((folder/'ManagedAddons').exists())
            self.assertFalse(list((folder/'NoAddons').iterdir()))
            self.assertIn(b'DepthCopyBeforeClears=0', (folder/'ReShade.ini').read_bytes())
            (root/reshade_runtime.CLOCK_FILE).unlink()
            self.assertEqual(reshade_runtime.verify(root, options), folder)
            (folder/'WePlay-Midsummer.ini').write_bytes(preset.replace(b'\n', b'\r\n'))
            reshade_runtime.launch_environment(root, {}, options)
            self.assertEqual((folder/'WePlay-Midsummer.ini').read_bytes(), preset)
            (folder/'Textures/AreaTex.png').write_bytes(b'changed lookup')
            with self.assertRaises(ValueError):
                reshade_runtime.prepare(root, options=options)

    def test_smaa_remains_independent_in_every_effect_combination(self):
        for mask in range(16):
            options = {name: bool(mask & (1 << i)) for i, name in enumerate(reshade_runtime.OPTIONS)}
            preset = reshade_runtime._preset(options)
            techniques = preset.splitlines()[0].decode().removeprefix('Techniques=').split(',')
            self.assertEqual(techniques.count('SMAA@SMAA.fx'), int(options['enhanced_aa']))
            if any(options[name] for name in ('midsummer_tone', 'contact_shadows', 'maximum_fx')):
                self.assertIn(b'EdgeStrength=0.000000', preset)

    def test_runtime_preset_rewrites_can_be_saved_and_launched_repeatedly(self):
        for maximum in (False,True):
            options=dict(midsummer_tone=True,enhanced_aa=True,contact_shadows=True,maximum_fx=maximum)
            with self.subTest(maximum=maximum),self.prepared(options) as (root,folder,closed):
                preset=folder/'WePlay-Midsummer.ini';expected=preset.read_bytes()
                rewritten=expected.replace(b'[WePlay-Midsummer.fx]',b'[WePlay-Midsummer.fx]\nAtmosphereGameTime=0.000000\nAtmospherePhaseReady=0').replace(b'\n',b'\r\n')
                preset.write_bytes(rewritten)
                with self.assertRaises(ValueError):reshade_runtime.verify(root,options)
                self.assertEqual(reshade_runtime.prepare(root,options=options),folder)
                self.assertEqual(preset.read_bytes(),expected)
                backup=folder/'WePlay-Midsummer.before-repair.ini'
                self.assertEqual(backup.read_bytes(),rewritten)
                preset.write_bytes(rewritten+b'\r\n; next exit\r\n')
                environment=reshade_runtime.launch_environment(root,{},options)
                self.assertIn('VK_LAYER_reshade',environment['VK_INSTANCE_LAYERS'])
                self.assertEqual(preset.read_bytes(),expected)
                self.assertEqual(backup.read_bytes(),rewritten,'retain first backup without growing per-exit history')
                self.assertEqual(list(folder.glob('*.before-repair.ini')),[backup])
                self.assertEqual(reshade_runtime.verify(root,options),folder)

    def test_repair_checks_modules_shaders_and_external_addons_before_writing(self):
        options=dict(maximum_fx=True)
        for name in ('ReShade32.dll','ReShade32.json','Shaders/WePlay-Midsummer.fx','NoAddons/unknown.addon32',
                     'ManagedAddons/WePlayAtmosphere.addon32'):
            with self.subTest(file=name),self.prepared(options) as (root,folder,closed):
                preset=folder/'WePlay-Midsummer.ini';rewritten=preset.read_bytes()+b'\n; normalized\n';preset.write_bytes(rewritten)
                (folder/name).write_bytes(b'changed module or shader')
                with self.assertRaises(ValueError):reshade_runtime.prepare(root,options=options)
                self.assertEqual(preset.read_bytes(),rewritten)
                self.assertFalse((folder/'WePlay-Midsummer.before-repair.ini').exists())

    def test_repair_never_rewrites_a_running_games_preset(self):
        with self.prepared({'midsummer_tone':True}) as (root,folder,closed):
            preset=folder/'WePlay-Midsummer.ini';rewritten=preset.read_bytes()+b'\n; runtime write\n';preset.write_bytes(rewritten)
            closed.side_effect=ValueError('game is running')
            with self.assertRaises(ValueError):reshade_runtime.prepare(root,options={'midsummer_tone':True})
            self.assertEqual(preset.read_bytes(),rewritten)
            self.assertFalse((folder/'WePlay-Midsummer.before-repair.ini').exists())

    def test_missing_generated_preset_is_rebuilt_after_asset_verification(self):
        with self.prepared({'midsummer_tone':True}) as (root,folder,closed):
            preset=folder/'WePlay-Midsummer.ini';expected=preset.read_bytes();preset.unlink()
            self.assertEqual(reshade_runtime.prepare(root,options={'midsummer_tone':True}),folder)
            self.assertEqual(preset.read_bytes(),expected)

    def test_normal_exit_defaults_allow_a_second_launch(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as path:
            root = Path(path)
            clock = root / reshade_runtime.CLOCK_FILE
            clock.parent.mkdir(parents=True)
            clock.write_bytes(b'read-only clock fixture')
            dll = b'invented x86 runtime fixture'
            packed = io.BytesIO()
            with zipfile.ZipFile(packed, 'w') as archive:
                archive.writestr('ReShade32.dll', dll)
                archive.writestr('ReShade32.json', archive_fixture_manifest())
            setup = root / 'setup.exe'
            setup.write_bytes(packed.getvalue())
            with patch.object(reshade_runtime, 'SETUP_SHA256', hashlib.sha256(packed.getvalue()).hexdigest()), \
                 patch.object(reshade_runtime, 'DLL_SHA256', hashlib.sha256(dll).hexdigest()), \
                 patch.object(reshade_runtime, 'ADDON_SETUP_SHA256', hashlib.sha256(packed.getvalue()).hexdigest()), \
                 patch.object(reshade_runtime, 'ADDON_DLL_SHA256', hashlib.sha256(dll).hexdigest()), \
                 patch.object(reshade_runtime, 'MANIFEST_SHA256', hashlib.sha256(archive_fixture_manifest()).hexdigest()), \
                 patch.object(reshade_runtime, 'CLOCK_SHA256', hashlib.sha256(clock.read_bytes()).hexdigest()), \
                 patch('graphics_profile.ensure_closed'):
                folder = reshade_runtime.prepare(root, setup=setup)
                (folder / 'ReShade.ini').write_bytes(reshade_runtime.CONFIG + b'\n[STYLE]\nAlpha=1.0\n')
                self.assertEqual(reshade_runtime.verify(root), folder)
                with patch('urllib.request.urlopen', side_effect=AssertionError('second download')):
                    self.assertEqual(reshade_runtime.prepare(root), folder)
                for selection, expected in (({'enhanced_aa':True}, b'Techniques=SMAA@SMAA.fx'),
                                            ({'contact_shadows':True}, b'ContactStrength=0.220000'),
                                            ({'maximum_fx':True}, b'MaximumStrength=1.000000')):
                    with patch('urllib.request.urlopen',side_effect=AssertionError('redundant combination download')):
                        other=reshade_runtime.prepare(root,options=selection)
                    self.assertNotEqual(folder,other)
                    preset=(other/'WePlay-Midsummer.ini').read_bytes()
                    self.assertIn(expected,preset)
                    self.assertEqual(b',WePlay_Maximum@' in preset,selection.get('maximum_fx',False))
                    if not selection.get('enhanced_aa'):
                        self.assertIn(b'Strength=0.000000',preset)
                    self.assertEqual(reshade_runtime.verify(root,selection),other)
                    if selection.get('maximum_fx'):
                        self.assertIn(b'AddonPath=.\\ManagedAddons',(other/'ReShade.ini').read_bytes())
                        bridge=other/'ManagedAddons/WePlayAtmosphere.addon32'
                        self.assertEqual(bridge.read_bytes(),clock.read_bytes())
                        rogue=bridge.with_name('external.addon32');rogue.write_bytes(b'external')
                        with self.assertRaises(ValueError):reshade_runtime.verify(root,selection)
                        rogue.unlink()
                        saved_bridge=bridge.read_bytes();bridge.write_bytes(b'changed clock')
                        with self.assertRaises(ValueError):reshade_runtime.verify(root,selection)
                        bridge.write_bytes(saved_bridge)
                        clock.write_bytes(b'changed source clock')
                        with self.assertRaises(ValueError):reshade_runtime.verify(root,selection)
                        clock.write_bytes(saved_bridge)
                        self.assertIn(b'DH_UBER_RT@dh_uber_rt.fx',preset)
                        self.assertNotIn(b'SMAA@SMAA.fx',preset)
                        self.assertEqual((other/'Licenses/DH-LICENSE.txt').read_bytes(),
                                         reshade_runtime._asset('effects/DH-LICENSE.txt').read_bytes())
                        lookup=other/'Textures/AreaTex.png'
                        saved=lookup.read_bytes();lookup.write_bytes(b'tampered lookup')
                        with self.assertRaises(ValueError):reshade_runtime.verify(root,selection)
                        lookup.write_bytes(saved)
                        self.assertEqual(reshade_runtime.verify(root,selection),other)
                    self.assertEqual(reshade_runtime.verify(root),folder)
                with self.assertRaises(ValueError):
                    reshade_runtime.prepare(root,options={'enhanced_aa':'yes'})
                (folder/'ReShade32.json').write_text('{"layer":{"name":"VK_LAYER_reshade","library_path":".\\\\ReShade32.dll","changed":true}}')
                with self.assertRaises(ValueError):reshade_runtime.verify(root)

    def test_contacts_choose_the_official_addon_runtime_only_when_explicit(self):
        import install
        self.assertEqual(install.MAXIMUM_ASSETS,reshade_runtime.MAXIMUM_ASSETS)
        self.assertEqual(reshade_runtime._runtime({'enhanced_aa':True})[0],reshade_runtime.URL)
        self.assertEqual(reshade_runtime._runtime({'contact_shadows':True})[0],reshade_runtime.ADDON_URL)
        self.assertEqual(reshade_runtime._runtime({'maximum_fx':True})[0],reshade_runtime.ADDON_URL)
        preset=reshade_runtime._preset({'maximum_fx':True,'enhanced_aa':True})
        self.assertIn(b'SMAA@SMAA.fx',preset)
        self.assertIn(b'EdgeStrength=0.000000',preset)
        techniques=preset.splitlines()[0]
        self.assertLess(techniques.index(b'WePlay_Midsummer@'),techniques.index(b'DH_UBER_RT@'))
        self.assertLess(techniques.index(b'WePlay_Maximum@'),techniques.index(b'SMAA@'))
        self.assertTrue(techniques.endswith(b'SMAA@SMAA.fx'))
        self.assertNotEqual(reshade_runtime._runtime({'enhanced_aa':True})[2],
                            reshade_runtime._runtime({'contact_shadows':True})[2])
        self.assertIn(b'PerformanceMode=0',reshade_runtime.CONFIG)
        self.assertIn(b'AddonPath=.\\NoAddons',reshade_runtime.CONFIG)
        self.assertIn(b'AddonPath=.\\NoAddons',reshade_runtime._config({'contact_shadows':True}))

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
        with patch.object(reshade_runtime, 'prepare', return_value=Path('fixture-tone')):
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
