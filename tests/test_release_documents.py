"""Release documents must not link to folders absent from the installer."""
from pathlib import Path
import sys,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/desktop'))
import build_rc2

class ReleaseDocumentTests(unittest.TestCase):
    def test_release_and_credit_links_are_versioned_and_targets_exist(self):
        for source in (build_rc2.RELEASE_GUIDE,ROOT/'README.md',ROOT/'CREDITS.md',ROOT/'docs/DISCORD-INTEGRATION.md'):
            with self.subTest(source=source.name):
                text=build_rc2.public_document(source)
                self.assertNotRegex(text,r'\]\((?!https?://|#)')
        text=build_rc2.public_document(build_rc2.RELEASE_GUIDE)
        self.assertIn('/blob/'+build_rc2.RELEASE_TAG+'/docs/VISUAL-QUALITY.md)',text)
        self.assertIn('/blob/'+build_rc2.RELEASE_TAG+'/docs/GRAPHICS-RC319-MEASUREMENTS.md)',text)
        self.assertIn('/blob/'+build_rc2.RELEASE_TAG+'/docs/RC3.21-INTERNET-JOIN.md)',text)

    def test_missing_or_external_file_targets_are_rejected(self):
        for target in ('missing-document.md','../../../../outside.md'):
            with self.subTest(target=target),patch.object(Path,'read_text',return_value='[broken]('+target+')'):
                with self.assertRaises(ValueError):build_rc2.public_document(build_rc2.RELEASE_GUIDE)

if __name__=='__main__':unittest.main()
