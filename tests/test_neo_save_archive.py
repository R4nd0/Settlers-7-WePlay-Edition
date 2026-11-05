"""Cross-language archive/codec checks using only synthetic saved-game data."""
from pathlib import Path
import gzip
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/lan'))
import bba

EXE=Path(os.environ.get('S7_NEO_ARCHIVE_TEST_EXE',ROOT/'work/neo-resume-fix/native-lab/neo_save_archive_tests.exe'))

def cache(value):
    def string(s):
        raw=s.encode()+b'\0'
        return b'Str_'+struct.pack('<I',len(raw))+raw
    entries=[] if value is None else [('weplay_neo_logistics_v1',value)]
    return b'bin1'+struct.pack('<3I',1,0,1)+b'Coll'+struct.pack('<2I',0,len(entries))+b''.join(
        struct.pack('<I',1)+string(k)+string(v) for k,v in entries)+b'coll'

def pickle_value(payload):
    # The original serializer escapes physical newlines with a backslash.
    escaped=payload.replace('\\','\\\\').replace('"','\\"').replace('\n','\\\n')
    return 'return{{["version"]=1,["payload"]="'+escaped+'",},}'

def archive(value,turn=600):
    data=bytearray(80);records=[]
    for name,raw in [('SaveGameCache.s7',cache(value)),('SavegameInfo.xml',f'<root><LogicTurn>{turn}</LogicTurn></root>'.encode())]:
        key=name.encode()+b'\0';packed=bba._gzip_xor(gzip.compress(raw,mtime=0),len(key),True)
        row=[0]*16;row[2]=len(raw);row[3]=bba.crc32(raw);row[4]=17;row[6]=len(data)
        row[8]=len(packed);row[9]=bba.crc32(packed);row[10]=1048576;row[12]=len(key)
        padded=((len(key)+3)&~3)+(4 if len(key)%4==0 else 0)
        records.append(struct.pack('<16I',*row)+key+b'\0'*(padded-len(key)))
        data+=packed
    directory=bytearray(272)+struct.pack('<I',len(records))+b''.join(records)
    struct.pack_into('<3I',directory,0,0,272,len(directory));directory+=struct.pack('<I',0)
    encoded=bba.crypt(directory,bba.FILE_KEYS,1532281822,8,True)
    header=struct.pack('<4I',len(data),0,len(encoded),bba.crc32(encoded))+bytes(48)
    data[:16]=b'BAF\6'+struct.pack('<3I',7,64,0)
    data[16:80]=bba.crypt(header,bba.HEAD_KEYS,1231853475,7,True)
    return bytes(data+encoded)

@unittest.skipUnless(EXE.is_file(),'Build native NEO archive tests first')
class SavedArchiveTests(unittest.TestCase):
    def run_reader(self,data,enabled=True):
        folder=ROOT/'work/neo-resume-fix/test-temp';folder.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as d:
            path=Path(d)/'s1002.s7s';path.write_bytes(data)
            return subprocess.run([str(EXE),str(path),'1' if enabled else '0'],capture_output=True,text=True,timeout=10)

    def test_legacy_next_turn_preserves_preferred_and_deferred_intents(self):
        payload='NEO1\n601\n2\n65537,601,1\n65538,10,-1\n'
        result=self.run_reader(archive(pickle_value(payload)))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,'NEO2\n601\n600\n2\n65537,601,1\n65538,10,-1\n')

    def test_new_saved_clock_must_match_native_archive(self):
        data=archive(pickle_value('NEO2\n601\n600\n1\n65537,601,-1\n'))
        self.assertEqual(self.run_reader(data).returncode,0)
        self.assertNotEqual(self.run_reader(archive(pickle_value('NEO2\n601\n599\n0\n'))).returncode,0)

    def test_missing_state_never_enables_fifo(self):
        data=archive(None)
        self.assertNotEqual(self.run_reader(data).returncode,0)
        self.assertEqual(self.run_reader(data,False).stdout,'NEO0\n')
        self.assertNotEqual(self.run_reader(archive(''),False).returncode,0)

    def test_wrong_clock_or_selected_mode_is_rejected(self):
        self.assertNotEqual(self.run_reader(archive(pickle_value('NEO1\n602\n0\n'))).returncode,0)
        self.assertNotEqual(self.run_reader(archive(pickle_value('NEO0\n'))).returncode,0)
        self.assertNotEqual(self.run_reader(archive(pickle_value('NEO1\n600\n0\n'))).returncode,0)

    def test_save_source_is_parsed_without_executing_lua(self):
        for value in ['return (function() error("executed") end)()',
                      'return{{["version"]=2,["payload"]="NEO0\\n",},}',
                      'return{{["version"]=1,["payload"]="NEO0\\n",["extra"]=1,},}']:
            with self.subTest(value=value):self.assertNotEqual(self.run_reader(archive(value)).returncode,0)

    def test_truncation_and_corruption_are_rejected(self):
        data=archive(pickle_value('NEO1\n601\n0\n'))
        self.assertEqual(self.run_reader(data).returncode,0)
        for cut in [0,79,100,len(data)-1]:
            with self.subTest(cut=cut):self.assertNotEqual(self.run_reader(data[:cut]).returncode,0)
        bad=bytearray(data);bad[110]^=128
        self.assertNotEqual(self.run_reader(bad).returncode,0)

    def test_large_complete_ledger_survives(self):
        marks=''.join(f'{65536+i},599,{1 if i%2 else -1}\n' for i in range(65536))
        payload=f'NEO2\n601\n600\n65536\n{marks}'
        result=self.run_reader(archive(pickle_value(payload)))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,payload)

if __name__=='__main__':unittest.main()
