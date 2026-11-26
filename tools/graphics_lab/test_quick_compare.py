"""Authored CPU cases only: no process opens, native workers, game or GPU."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib

from tools.graphics_lab import quick_compare as q


def bmp():
    pixels=bytes([1,2,3,255,4,5,6,255]*2)
    return b'BM'+struct.pack('<IHHI',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,2,2,1,32,0,len(pixels),0,0,0,0)+pixels


def metadata(variant=0,enabled=4,frame=10):
    pin=hashlib.sha256(bmp()).hexdigest()
    scene={'source':'original_river_shader_uniforms','frame':frame,'available':1,'context_valid':7,
        'vertex_sha256':q.compare.RIVER_VS,'pixel_sha256':q.compare.RIVER_PS,
        'target':[2,2],'viewport':[2,2],'catalog_sha256':'c'*64,
        'vs_constants_12_15':[0.0]*16,'vs16_eye':[1.0,2.0,3.0],
        'ps5_sun_direction':[0.0,0.0,1.0],'ps7_sun_specular':[1.0]*3}
    value={'format':'s7-native-frame-capture-v1','complete':True,'status':0,'frame':frame,'edition':2,
        'source':[2,2],'output':[2,2],'source_bmp_sha256':pin,'output_bmp_sha256':pin,
        'fallback_used':False,'filtered_api_succeeded':True,'presentation_path':'filtered_output',
        'present_hresult':0,'filtered_present_hresult':0,'readback_hresult':0,'scene_uniforms':scene,
        'effects_config_version':4,'effects_configured':1,'effects_active':enabled,'effects_drawn_active':enabled,
        'effects_revision':frame+3,'effects_drawn_revision':frame+3,'effects_frame_has_geometry':1,
        'effects_selected_variant':variant,'effects_drawn_variant':variant if enabled else None,'effects_drawn_frame':frame,
        'effects_variant_frame_applications':2 if enabled else 0,'effects_variant_frame_restorations':2 if enabled else 0,
        'effects_variant_frame_failed_draws':0,
        'leaf_palette':{'selected_variant':variant,'drawn_variant':variant if enabled else None,
            'configured_program_sha256':q.PROGRAMS[variant],
            'drawn_program_sha256':q.PROGRAMS[variant]if enabled else None,'drawn_frame':frame,
            'module_epoch':2,'variant_applications':[2]*3,'variant_restorations':[2]*3,
            'variant_successful_draws':[2]*3,'variant_failed_draws':[0]*3,'program_creations':[1]*3,
            **{key:0 for key in ('faulted','shader_failures','set_failures','restore_failures','exceptions','active_transactions','active_original_refs')}}}
    value.update({key:0 for key in q.FAULTS})
    return value


def water_metadata(variant=0,enabled=1,frame=10):
    value=metadata(variant,0,frame)
    value['effects_config_version']=1
    value['water']={'frame':frame,'epoch':frame+2,'configured':1,'requested':enabled,'enabled':enabled}
    value['water_palette']={'config_version':2,'selected_variant':variant,
        'drawn_variant':variant if enabled else None,'configured_program_sha256':q.WATER_PROGRAMS[variant],
        'drawn_program_sha256':q.WATER_PROGRAMS[variant]if enabled else None,'frame':frame,'epoch':frame+2,
        'completed_applications':2 if enabled else 0,'completed_restorations':2 if enabled else 0,
        'completed_successful_draws':2 if enabled else 0,'completed_failed_draws':0,
        'variant_applications':[2]*3,'variant_restorations':[2]*3,'variant_successful_draws':[2]*3,
        'variant_failed_draws':[0]*3,'program_creations':[1]*3,'cached_refs':0,
        **{key:0 for key in ('faulted','unsafe','shader_failures','set_failures','restore_failures',
            'exceptions','active_transactions','pending_original_refs')}}
    return value


class FakeSession:
    def __init__(self,case,clock):
        self.case=case;self.clock=clock;self.records=[];self.variant_index=0;self.enabled=0
        self.sequence=0;self.closed=False;self.pose_change=False;self.selection_mismatch=False;self.capture_cost=.1
    def variant(self,index,enabled,deadline,cleanup=False):
        if self.clock[0]>=deadline:raise TimeoutError('deadline')
        self.variant_index=index;self.enabled=enabled
        self.records.append({'variant':index,'enabled':enabled,'cleanup':cleanup})
        self.clock[0]+=.01
    def capture(self,deadline):
        if self.clock[0]+self.capture_cost>=deadline:
            self.clock[0]=deadline
            raise TimeoutError('capture deadline')
        self.clock[0]+=self.capture_cost;self.sequence+=1
        meta=metadata(self.variant_index,self.enabled,self.sequence)
        if self.pose_change and self.sequence==2:meta['scene_uniforms']['vs16_eye'][0]+=1
        if self.selection_mismatch and self.enabled:meta['effects_drawn_variant']=(self.variant_index+1)%3
        for stage in ('source','output'):(self.case/f'capture-{self.sequence}-{stage}.bmp').write_bytes(bmp())
        return self.sequence,meta
    def close(self):self.closed=True
    def check(self):pass


class FakeWaterSession(FakeSession):
    def capture(self,deadline):
        if self.clock[0]+self.capture_cost>=deadline:
            self.clock[0]=deadline
            raise TimeoutError('capture deadline')
        self.clock[0]+=self.capture_cost;self.sequence+=1
        meta=water_metadata(self.variant_index,self.enabled,self.sequence)
        if self.pose_change and self.sequence==2:meta['scene_uniforms']['vs16_eye'][0]+=1
        if self.selection_mismatch and self.enabled:meta['water_palette']['drawn_variant']=(self.variant_index+1)%3
        for stage in ('source','output'):(self.case/f'capture-{self.sequence}-{stage}.bmp').write_bytes(bmp())
        return self.sequence,meta


class Tests(unittest.TestCase):
    def setUp(self):
        small=patch.object(q,'CAPTURE_SIZES',([2,2],[2,2]));small.start();self.addCleanup(small.stop)
        self.temp=tempfile.TemporaryDirectory(dir=q.guard.ROOT/'work');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.case=self.root/'case';self.case.mkdir()
        (self.case/'hook-initialized.private.json').write_text(json.dumps({'observer_catalog_sha256':'c'*64}))
        self.clock=[0.0];self.session=FakeSession(self.case,self.clock)
    def run_case(self,seconds=30,profile='leaf-palette'):
        programs=q.WATER_PROGRAMS if profile=='water-palette'else q.PROGRAMS
        pins=[{'variant':i,'name':q.VARIANTS[i],'sha256':programs[i]}for i in range(3)]
        with patch.object(q.guard,'_lab',side_effect=Path),patch.object(q,'palette_pins',return_value=pins):
            return q.run(self.case,self.root/'dll',self.root/'out',seconds=seconds,profile=profile,
                session_factory=lambda *args,**kwargs:self.session,clock=lambda:self.clock[0])
    def test_healthy_batch_has_seven_frames_three_controls_and_final_off(self):
        result=self.run_case()
        self.assertTrue(result['comparison_complete']);self.assertTrue(result['final_off_acknowledged'])
        self.assertTrue(result['final_off_captured']);self.assertEqual(len(result['frames']),7)
        self.assertEqual([(f['variant'],f['enabled'])for f in result['frames']],list(q.PLAN))
        self.assertEqual(len(result['triplets']),3);self.assertEqual(len(result['warmup']),3)
        self.assertEqual(self.session.records[-1],{'variant':0,'enabled':0,'cleanup':True})
        self.assertTrue(self.session.closed);self.assertFalse(result['quality_assessed']);self.assertFalse(result['performance_assessed'])
        self.assertTrue((self.root/'out/Vergleich.html').exists())
    def test_camera_change_rejects_triplet_and_still_turns_off(self):
        self.session.pose_change=True;result=self.run_case()
        self.assertFalse(result['comparison_complete']);self.assertEqual(len(result['frames']),3)
        self.assertIn('Camera',result['error']['message']);self.assertTrue(result['final_off_acknowledged'])
    def test_selected_program_mismatch_rejects_and_final_off(self):
        self.session.selection_mismatch=True;result=self.run_case()
        self.assertFalse(result['comparison_complete']);self.assertTrue(result['final_off_acknowledged'])
        self.assertEqual(self.session.enabled,0)
    def test_deadline_stops_batch_but_reserves_bounded_off_cleanup(self):
        self.session.capture_cost=20;result=self.run_case()
        self.assertFalse(result['comparison_complete']);self.assertEqual(result['error']['type'],'TimeoutError')
        self.assertTrue(result['final_off_acknowledged']);self.assertLessEqual(result['batch_seconds'],35)
    def test_pressure_and_failed_monitor_are_observations_while_foreign_or_stale_owner_is_rejected(self):
        files={'owned':{'pid':123},'record_sha256':'a'*64}
        value={'format':'s7-memory-live-v1','pid':123,'owned_record_sha256':'a'*64,'sampled_monotonic_ns':10_000_000_000,
               'guardian':{'monitor_failed':False,'recovery_required':False,'max_pressure':0,'samples':1}}
        q.memory_healthy(value,files,11_000_000_000)
        for change in ({'sampled_monotonic_ns':1},{'sampled_monotonic_ns':12_000_000_000},{'pid':124},{'owned_record_sha256':'b'*64}):
            with self.subTest(change=change),self.assertRaises(ValueError):q.memory_healthy({**value,**change},files,11_000_000_000)
        for key,new in (('monitor_failed',True),('recovery_required',True),('max_pressure',1),('samples',0)):
            changed=deepcopy(value);changed['guardian'][key]=new
            with self.subTest(key=key):q.memory_healthy(changed,files,11_000_000_000)
    def test_frame_proof_rejects_fault_stale_frame_wrong_sha_and_unbalanced_draws(self):
        q.capture_proof(metadata(),0,4,q.PROGRAMS[0],'c'*64)
        for key,value in (('effects_drawn_frame',9),('effects_variant_frame_restorations',1),('effects_unsafe',1)):
            changed=metadata();changed[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):q.capture_proof(changed,0,4,q.PROGRAMS[0],'c'*64)
        changed=metadata();changed['leaf_palette']['drawn_program_sha256']='d'*64
        with self.assertRaises(ValueError):q.capture_proof(changed,0,4,q.PROGRAMS[0],'c'*64)
    def test_lossless_png_exact_rgb_no_resampling(self):
        raw=q.png(bmp());offset=8;payload=b''
        while offset<len(raw):
            count=struct.unpack_from('>I',raw,offset)[0];kind=raw[offset+4:offset+8]
            if kind==b'IDAT':payload+=raw[offset+8:offset+8+count]
            offset+=count+12
        self.assertEqual(zlib.decompress(payload),(b'\0'+bytes([3,2,1,6,5,4]))*2)
        with self.assertRaises(ValueError):q.png(bmp(),[1,1,2,2])
    def test_batch_limit_is_fixed_and_output_is_never_reused(self):
        for seconds in (29,61,30.0,True):
            with self.assertRaises(ValueError):self.run_case(seconds)
        (self.root/'out').mkdir()
        with self.assertRaises(ValueError):self.run_case()

    def test_fixed_stage_wire_paths_programs_and_recipes_are_bound(self):
        raw=bytearray(4120);struct.pack_into('<6I',raw,0,4120,4,0,3840,2160,0)
        blobs=[bytes([i+1])*1372 for i in range(3)]
        pins=tuple(hashlib.sha256(blob).hexdigest()for blob in blobs)
        stage={'format':'s7-recipient-material-stage-v4','config_version':4,'artifact_root':str(self.case),'palette_variants':[]}
        for index,pin in enumerate(pins):
            path=self.case/'LocalData/MaterialShaders'/q.RECIPES[index]/(pin+'.ps.bin')
            path.parent.mkdir(parents=True);path.write_bytes(blobs[index])
            text=(str(path)+'\0').encode('utf-16-le');raw[24+index*1024:24+index*1024+len(text)]=text
            stage['palette_variants'].append({'index':index,'name':q.VARIANTS[index],
                'recipe':q.RECIPES[index],'sha256':pin,'bytes':1372})
        (self.case/'effects-config.bin').write_bytes(raw)
        (self.case/'checkpoint.private.json').write_text(json.dumps({'leaf_palette_requested':True}))
        target=self.case/'material-stage.private.json';target.write_text(json.dumps(stage))
        with patch.object(q,'PROGRAMS',pins),patch.object(q.guard,'_lab',side_effect=Path):
            self.assertEqual([row['sha256']for row in q.palette_pins(self.case)],list(pins))
            stage['palette_variants'][2]['sha256']=pins[0];target.write_text(json.dumps(stage))
            with self.assertRaises(ValueError):q.palette_pins(self.case)
            stage['palette_variants'][2]['sha256']=pins[2];stage['artifact_root']=str(self.root)
            target.write_text(json.dumps(stage))
            with self.assertRaises(ValueError):q.palette_pins(self.case)

    def test_queued_variant_waits_for_actual_matching_state_not_worker_ack_alone(self):
        session=q.CaseSession.__new__(q.CaseSession)
        session.case=self.case;session.sequence=0;session.records=[];session.owner=Mock();session.check=Mock()
        session.files={'build':{'files':{q.guard.load_hook.DLL_NAME:'d'*64}}}
        ready={'effects_pending':0,'effects_config_version':4,'effects_selected_variant':1,'effects_active':4,
            'effects_drawn_active':4,'effects_frame_has_geometry':0,'effects_revision':2,'effects_drawn_revision':2,
            'effects_drawn_variant':1,**{key:0 for key in q.FAULTS}}
        states=iter([{**ready,'effects_pending':1},ready]);original_replace=q.os.replace;original_read=q.read_json
        def publish(source,dest):
            original_replace(source,dest)
            (self.case/'command-1.private.json').write_text(json.dumps({'succeeded':True,'export':'WePlayNativeGraphicsConfigureEffects',
                'worker_code_rx':True,'original_entry_guarded':True,'dll_sha256':'d'*64}))
        def read(path):return next(states)if path.name=='hook-state.private.json'else original_read(path)
        with patch.object(q.os,'replace',side_effect=publish),patch.object(q,'read_json',side_effect=read),\
             patch.object(q.time,'monotonic',return_value=0),patch.object(q.time,'sleep')as sleep:
            self.assertEqual(session.variant(1,4,10),1)
        sleep.assert_called_once_with(.05)
        self.assertEqual(json.loads((self.case/'native-command.json').read_text()),
            {'operation':'variant','sequence':1,'variant':1,'enabled':4})
        self.assertEqual(session.records[-1]['drawn_state'],ready)

    def test_cleanup_failure_is_reported_without_turning_into_success(self):
        original=self.session.variant
        def change(index,enabled,deadline,cleanup=False):
            if cleanup:raise RuntimeError('closed before OFF acknowledgement')
            return original(index,enabled,deadline,cleanup)
        self.session.variant=change;result=self.run_case()
        self.assertTrue(result['comparison_complete']);self.assertFalse(result['final_off_acknowledged'])
        self.assertIn('closed before',result['final_off_error']['message']);self.assertTrue(self.session.closed)

    def test_water_profile_seven_frames_uses_boolean_mask_and_final_off(self):
        self.session=FakeWaterSession(self.case,self.clock);result=self.run_case(profile='water-palette')
        self.assertTrue(result['comparison_complete']);self.assertEqual(result['profile'],'water-palette')
        self.assertEqual([(f['variant'],f['enabled'])for f in result['frames']],
            [(variant,1 if enabled else 0)for variant,enabled in q.PLAN])
        self.assertEqual(self.session.records[-1],{'variant':0,'enabled':0,'cleanup':True})
        self.assertEqual(len(self.session.records),8);self.assertEqual(result['warmup'],[])
        self.assertFalse(result['runtime_qualified']);self.assertFalse(result['quality_assessed'])
        text=(self.root/'out/Vergleich.html').read_text(encoding='utf8')
        self.assertIn('Drei Wasser-Shader',text);self.assertIn('bereits abgenommene Wasser B',text)

    def test_water_wrong_program_unbalanced_frame_and_stale_epoch_reject(self):
        q.capture_proof(water_metadata(),0,1,q.WATER_PROGRAMS[0],'c'*64,'water-palette')
        for key,value in (('drawn_program_sha256','a'*64),('completed_restorations',1),
                          ('completed_successful_draws',0),('completed_successful_draws',1),('completed_failed_draws',1),
                          ('frame',9),('epoch',0),('pending_original_refs',1),('cached_refs',1)):
            meta=water_metadata();meta['water_palette'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):q.capture_proof(meta,0,1,q.WATER_PROGRAMS[0],'c'*64,'water-palette')
        meta=water_metadata(enabled=0);meta['water_palette']['drawn_variant']=0
        with self.assertRaises(ValueError):q.capture_proof(meta,0,0,q.WATER_PROGRAMS[0],'c'*64,'water-palette')

    def test_water_selection_mismatch_or_camera_move_finally_turns_off(self):
        for reason in ('selection_mismatch','pose_change'):
            with self.subTest(reason=reason):
                self.session=FakeWaterSession(self.case,self.clock);setattr(self.session,reason,True)
                output=self.root/reason
                with patch.object(q.guard,'_lab',side_effect=Path),patch.object(q,'palette_pins',return_value=[
                        {'variant':i,'name':q.VARIANTS[i],'sha256':q.WATER_PROGRAMS[i]}for i in range(3)]):
                    result=q.run(self.case,self.root/'dll',output,profile='water-palette',seconds=30,
                        session_factory=lambda *a,**kw:self.session,clock=lambda:self.clock[0])
                self.assertFalse(result['comparison_complete']);self.assertTrue(result['final_off_acknowledged'])

    def test_water_wire_stage_binds_all_three_private_programs(self):
        raw=bytearray(3096);struct.pack_into('<6I',raw,0,3096,2,3840,2160,0,0)
        blobs=[bytes([i+4])*3840 for i in range(3)];pins=tuple(hashlib.sha256(blob).hexdigest()for blob in blobs)
        stage={'format':'s7-recipient-water-palette-stage-v2','config_version':2,
            'artifact_root':str(self.case),'palette_variants':[]}
        for index,pin in enumerate(pins):
            path=self.case/'LocalData/MaterialShaders'/q.WATER_RECIPES[index]/(pin+'.ps.bin')
            path.parent.mkdir(parents=True);path.write_bytes(blobs[index])
            text=(str(path)+'\0').encode('utf-16-le');raw[24+index*1024:24+index*1024+len(text)]=text
            stage['palette_variants'].append({'index':index,'name':q.WATER_NAMES[index],
                'recipe':q.WATER_RECIPES[index],'sha256':pin,'bytes':3840})
        config=self.case/'water-palette-config.bin';config.write_bytes(raw)
        (self.case/'checkpoint.private.json').write_text(json.dumps({'water_palette_requested':True}))
        target=self.case/'water-palette-stage.private.json';target.write_text(json.dumps(stage))
        with patch.object(q,'WATER_PROGRAMS',pins),patch.object(q.guard,'_lab',side_effect=Path):
            self.assertEqual([r['sha256']for r in q.palette_pins(self.case,'water-palette')],list(pins))
            struct.pack_into('<I',raw,4,1);config.write_bytes(raw)
            with self.assertRaises(ValueError):q.palette_pins(self.case,'water-palette')
            struct.pack_into('<I',raw,4,2);config.write_bytes(raw)
            stage['palette_variants'][2]['sha256']=pins[0];target.write_text(json.dumps(stage))
            with self.assertRaises(ValueError):q.palette_pins(self.case,'water-palette')

    def test_water_queue_uses_fixed_export_and_boolean_variant_command(self):
        session=q.CaseSession.__new__(q.CaseSession);session.profile='water-palette'
        session.case=self.case;session.sequence=0;session.records=[];session.owner=Mock();session.check=Mock()
        session.files={'build':{'files':{q.guard.load_hook.DLL_NAME:'d'*64}}}
        state={**{key:0 for key in q.FAULTS},'water':{'pending':0,'config_version':2,
            'selected_variant':2,'drawn_variant':2,'enabled':1,'epoch':3,'faulted':0,'unsafe':0}}
        original_replace=q.os.replace;original_read=q.read_json
        def publish(source,dest):
            original_replace(source,dest)
            (self.case/'command-1.private.json').write_text(json.dumps({'succeeded':True,'export':'WePlayNativeGraphicsConfigureWater',
                'worker_code_rx':True,'original_entry_guarded':True,'dll_sha256':'d'*64}))
        def read(path):
            if path.name=='hook-state.private.json':raise AssertionError('Water must wait for capture proof, not a periodic report.')
            return original_read(path)
        with patch.object(q.os,'replace',side_effect=publish),patch.object(q,'read_json',side_effect=read),\
             patch.object(q.time,'monotonic',return_value=0):
            self.assertEqual(session.variant(2,1,10),1)
            with self.assertRaises(ValueError):session.variant(2,4,10)
        self.assertEqual(json.loads((self.case/'native-command.json').read_text()),
            {'operation':'variant','sequence':1,'variant':2,'enabled':1})

    def test_water_cleanup_transport_ack_with_pending_on_is_not_applied_off(self):
        pending={**{key:0 for key in q.FAULTS},'water':{'pending':1,'config_version':2,'configured':1,
            'selected_variant':0,'requested':1,'enabled':1,'epoch':2,**{key:0 for key in (
                'faulted','unsafe','set_failures','restore_failures','exceptions','pending_original_refs')}}}
        applied=deepcopy(pending);applied['water'].update(pending=0,requested=0,enabled=0,epoch=3)
        for completes in (False,True):
            session=q.CaseSession.__new__(q.CaseSession);session.profile='water-palette';session.case=self.case
            session.records=[{}];session._queue=Mock(return_value=7);session.check=Mock()
            with self.subTest(completes=completes),patch.object(q,'read_json',side_effect=[pending,applied]if completes else None,
                    return_value=pending),patch.object(q.time,'sleep')as sleep,\
                    patch.object(q.time,'monotonic',side_effect=[0,1,6]if not completes else [0,1]):
                if completes:
                    self.assertEqual(session.variant(0,0,5,cleanup=True),7)
                    self.assertEqual(session.records[-1]['applied_off_state'],applied)
                else:
                    with self.assertRaises(TimeoutError):session.variant(0,0,5,cleanup=True)
                    self.assertNotIn('applied_off_state',session.records[-1])
            session._queue.assert_called_once_with('variant',5,variant=0,enabled=0,cleanup=True)


if __name__=='__main__':unittest.main()
