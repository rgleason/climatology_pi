import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('retain_packages', Path(__file__).parents[1] / 'retain-packages.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

XML = '''<plugin version="1"><name> Climatology </name><version> 2.0.1.0 </version>
<api-version>1.18</api-version><target>msvc-64</target><target-version>10</target-version>
<target-arch>x86_64</target-arch><tarball-url>https://example.invalid/--filename--</tarball-url>
<info-url>https://example.invalid/info</info-url></plugin>'''


class RetainedPackages(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'package directory'
        self.source.mkdir()
        self.output = Path(self.temp.name) / 'artifacts'
        self.archive = self.source / 'climatology_pi-2.0.1.0+ci test.tar.gz'
        self.archive.write_bytes(b'package bytes')
        self.xml = self.source / 'toolkit-different-basename.xml'
        self.xml.write_text(XML)

    def test_missing_archive_or_companion_xml_fails_before_staging(self):
        self.xml.unlink()
        with self.assertRaisesRegex(ValueError, 'companion XML'):
            module.retain(self.source, self.output)
        self.archive.unlink()
        with self.assertRaisesRegex(ValueError, 'No built plugin archive'):
            module.retain(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_finalizes_the_retained_copy_and_preserves_publication_input(self):
        original = self.xml.read_bytes()
        module.retain(self.source, self.output, 'https://example.invalid/artifacts')
        result = ET.parse(next(self.output.glob('*.xml'))).getroot()
        self.assertEqual(result.findtext('name'), 'Climatology')
        self.assertEqual(result.findtext('tarball-url'),
                         'https://example.invalid/artifacts/climatology_pi-2.0.1.0%2Bci%20test.tar.gz')
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.assertEqual(result.findtext('tarball-checksum'), digest)
        self.assertIn(digest, (self.output / 'SHA256SUMS').read_text())
        self.assertEqual(self.xml.read_bytes(), original)
        self.assertEqual((self.output / self.archive.name).read_bytes(), self.archive.read_bytes())

    def test_uses_the_circleci_job_artifact_location(self):
        job = 'f9cd543e-ab45-4235-952a-81d67f499896'
        with patch.dict('os.environ', {'CIRCLE_WORKFLOW_JOB_ID': job, 'CIRCLE_NODE_INDEX': '0'}):
            module.retain(self.source, self.output)
        result = ET.parse(next(self.output.glob('*.xml'))).getroot()
        self.assertTrue(result.findtext('tarball-url').startswith(
            f'https://output.circle-artifacts.com/output/job/{job}/artifacts/0/packages/'))

    def test_unresolved_url_and_incomplete_metadata_fail(self):
        with patch.dict('os.environ', {}, clear=True), self.assertRaisesRegex(ValueError, 'artifact base URL'):
            module.retain(self.source, self.output)
        self.xml.write_text('<plugin><tarball-url>https://example.invalid/file</tarball-url></plugin>')
        with self.assertRaisesRegex(ValueError, 'Missing name'):
            module.retain(self.source, self.output, 'https://example.invalid/artifacts')
        self.assertFalse(self.output.exists())

    def test_multiple_archives_must_have_unambiguous_xml_pairs(self):
        second = self.source / 'second.tar.gz'
        second.write_bytes(b'second package')
        (self.source / 'another.xml').write_text(XML)
        with self.assertRaisesRegex(ValueError, 'Cannot identify the companion XML'):
            module.retain(self.source, self.output, 'https://example.invalid/artifacts')
        self.xml.rename(self.archive.with_name(self.archive.name.removesuffix('.tar.gz') + '.xml'))
        (self.source / 'another.xml').rename(self.source / 'second.xml')
        module.retain(self.source, self.output, 'https://example.invalid/artifacts')
        self.assertEqual(len(list(self.output.glob('*.xml'))), 2)


if __name__ == '__main__':
    unittest.main()
