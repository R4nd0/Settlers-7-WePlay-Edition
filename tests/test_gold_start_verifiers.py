"""Emulator boundaries using tiny authored x86 programs, never game binaries."""
from pathlib import Path
import contextlib
import io
import struct
import sys
from types import SimpleNamespace as Record
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/gold'))
try:
    import verify_start_fixture as target
except ImportError:
    target=None


@unittest.skipUnless(target,'Unicorn x86 test runtime is required')
class StartVerifierBoundaryTests(unittest.TestCase):
    def fixture(self,program):
        data=bytearray(0x3000);data[0x1000:0x1000+len(program)]=program
        return Record(FILE_HEADER=Record(Machine=0x14c),
            OPTIONAL_HEADER=Record(ImageBase=0x24000000,SizeOfImage=len(data)),
            sections=[Record(VirtualAddress=0x1000,Misc_VirtualSize=0x100,Characteristics=0x20000000)],
            DIRECTORY_ENTRY_EXPORT=Record(symbols=[Record(name=b'Case',address=0x1000)]),
            DIRECTORY_ENTRY_IMPORT=[],get_memory_mapped_image=lambda:bytes(data))

    def run_fixture(self,fixture,**options):
        with contextlib.redirect_stdout(io.StringIO()):target.run(fixture,b'Case',(),**options)

    def test_owned_compiled_program_returns_with_expected_stack_and_registers(self):
        self.run_fixture(self.fixture(b'\xb8\x01\0\0\0\xc3'))

    def test_original_image_and_foreign_mapped_stack_never_execute(self):
        image=Record(base=0x400000,size=0x1000,mapped=b'\xb8\x01\0\0\0\xc3'+bytes(4090))
        for address,options in ((image.base,dict(image=image)),(target.STACK+0x1000,{})):
            program=b'\xb8'+struct.pack('<I',address)+b'\xff\xe0'
            with self.subTest(address=address),self.assertRaisesRegex(RuntimeError,'execution excluded'):
                self.run_fixture(self.fixture(program),**options)

    def test_unreplaced_original_continuation_cannot_execute(self):
        for address in target.CONTINUATIONS:
            program=b'\xb8'+struct.pack('<I',address)+b'\xff\xe0'
            with self.subTest(address=address),self.assertRaisesRegex(RuntimeError,'execution excluded'):
                self.run_fixture(self.fixture(program))

    def test_unexpected_import_is_never_forwarded_to_windows(self):
        fixture=self.fixture(b'\xb8'+struct.pack('<I',target.STUBS+0x100)+b'\xff\xe0')
        fixture.DIRECTORY_ENTRY_IMPORT=[Record(imports=[Record(name=b'UnexpectedHostCall',address=0x24002000)])]
        with self.assertRaisesRegex(RuntimeError,'Unexpected emulated import'):
            self.run_fixture(fixture)

    def test_wrong_architecture_base_size_or_executable_section_fails_before_execution(self):
        for field,value in (('Machine',0x8664),('ImageBase',0x400000),('SizeOfImage',5*1024*1024),
                            ('VirtualAddress',0x10000000),('Misc_VirtualSize',0x4000)):
            fixture=self.fixture(b'\xc3')
            owner=(fixture.FILE_HEADER if field=='Machine' else fixture.sections[0]
                   if field in ('VirtualAddress','Misc_VirtualSize') else fixture.OPTIONAL_HEADER)
            setattr(owner,field,value)
            with self.subTest(field=field),self.assertRaises(ValueError):self.run_fixture(fixture)

    def test_missing_ambiguous_or_data_export_cannot_execute(self):
        for symbols in ([],[Record(name=b'Case',address=0x1000)]*2,[Record(name=b'Case',address=0x2000)]):
            fixture=self.fixture(b'\xc3');fixture.DIRECTORY_ENTRY_EXPORT.symbols=symbols
            with self.subTest(symbols=symbols),self.assertRaises(ValueError):self.run_fixture(fixture)


if __name__=='__main__':unittest.main()
