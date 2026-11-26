from copy import deepcopy
import json,tempfile,unittest
from pathlib import Path
from tools.graphics_lab.shader_preset import candidate,Recorder

def report():
    return dict(format='s7-supersampling-hook-v1',overlay_profile=6,overlay_preset_revision=3,
                overlay_preset=[dict(profile=p,enabled=int(p==1),variant=0)for p in range(1,6)])

class PresetTests(unittest.TestCase):
    def test_candidate_keeps_all_families_without_approving_release(self):
        r=report();r['overlay_preset'][1].update(enabled=1,variant=2)
        c=candidate(r)
        self.assertEqual(c['selection']['vegetation'],dict(enabled=True,variant=2))
        self.assertEqual(len(c['selection']),5)
        self.assertFalse(c['selection_confirmed_by_user']);self.assertFalse(c['release_qualified'])

    def test_unknown_reordered_and_partial_choices_rejected(self):
        for change in ('profile','bool','variant','order','extra','partial','revision'):
            r=deepcopy(report())
            if change=='profile':r['overlay_profile']=2
            elif change=='bool':r['overlay_preset'][0]['enabled']=True
            elif change=='variant':r['overlay_preset'][2]['variant']=1
            elif change=='order':r['overlay_preset'].reverse()
            elif change=='extra':r['overlay_preset'][0]['path']='unknown.ps.bin'
            elif change=='partial':r['overlay_preset'].pop()
            else:r['overlay_preset_revision']=-1
            with self.subTest(change=change),self.assertRaises(ValueError):candidate(r)

    def test_recorder_retains_last_good_choice_on_partial_or_stale_report(self):
        with tempfile.TemporaryDirectory()as td:
            base=Path(td);native=base/'native.json';out=base/'choice.json';mirror=base/'last.json'
            rec=Recorder(native,out,mirror=mirror);r=report();native.write_text(json.dumps(r))
            self.assertTrue(rec.update());good=out.read_bytes();self.assertEqual(good,mirror.read_bytes())
            self.assertFalse(rec.update())
            native.write_text('{');self.assertFalse(rec.update());self.assertEqual(good,out.read_bytes())
            r['overlay_preset_revision']=2;native.write_text(json.dumps(r))
            self.assertFalse(rec.update());self.assertEqual(good,out.read_bytes())
            r['overlay_preset_revision']=4;r['overlay_preset'][4].update(enabled=1,variant=1)
            native.write_text(json.dumps(r));self.assertTrue(rec.update())
            self.assertTrue(json.loads(out.read_text())['selection']['ambient']['enabled'])
            self.assertFalse(out.with_name(out.name+'.tmp').exists())

if __name__=='__main__':unittest.main()
