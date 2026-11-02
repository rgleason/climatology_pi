"""The published archive must be identical to the package qualified by the host."""
import hashlib
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).parents[1]))
import cloudsmith_publication as publication
from windows_pe_fixture import pe_image


class CompletePackagePublication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.repository = 'opencpn/climatology-beta'
        self.version = publication.VERSION + '+123.abcdef0'
        self.archive = self.directory / ('climatology_pi-' + publication.VERSION + '-msvc-64-10-x86_64-wx334.tar.gz')
        self.xml = self.archive.with_name(self.archive.name[:-len('.tar.gz')] + '.xml')
        plugin = ET.Element('plugin', version='1')
        for key, value in {'name': 'Climatology', 'version': publication.VERSION,
                           'target': 'msvc-64', 'target-version': '10', 'target-arch': 'x86_64',
                           'tarball-url': publication.download_url(self.repository, self.version, self.archive.name)}.items():
            ET.SubElement(plugin, key).text = value
        with tarfile.open(self.archive, 'w:gz') as tar:
            for name, data in {'package/metadata.xml': ET.tostring(plugin),
                               'package/plugins/climatology_pi.dll': pe_image()}.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
        ET.SubElement(plugin, 'tarball-checksum').text = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.xml.write_bytes(ET.tostring(plugin))

    def plan(self):
        return publication.publication_plan(self.directory, self.repository, self.version)

    def test_production_beta_alpha_and_fork_pr_routing(self):
        for extra, level in (({'CIRCLE_BRANCH': 'master'}, 'beta'),
                             ({'CIRCLE_BRANCH': 'master', 'CIRCLE_TAG': 'v2.0.2.0'}, 'prod'),
                             ({'CIRCLE_BRANCH': 'feature'}, 'alpha'),
                             ({'CIRCLE_BRANCH': 'pull/95', 'CIRCLE_PULL_REQUEST': 'https://github.com/rgleason/climatology_pi/pull/95'}, 'beta')):
            repository, version = publication.context(dict(CIRCLE_SHA1='abcdef0', CIRCLE_BUILD_NUM='123', **extra))
            self.assertEqual(repository, 'opencpn/climatology-' + level)
            self.assertEqual(version, extra.get('CIRCLE_TAG', self.version))

    def test_plan_preserves_tested_archive_and_xml_bytes(self):
        before = (self.archive.read_bytes(), self.xml.read_bytes())
        plan = self.plan()
        self.assertEqual((self.archive.read_bytes(), self.xml.read_bytes()), before)
        self.assertEqual([p['name'] for p in plan['objects']],
                         [self.archive.name[:-len('.tar.gz')] + '-' + kind for kind in ('tarball', 'metadata')])
        self.assertEqual(plan['objects'][0]['sha256'], hashlib.sha256(before[0]).hexdigest())
        self.assertIn('%2B123.abcdef0', plan['objects'][0]['url'])

    def test_changed_tarball_wrong_url_and_legacy_package_are_rejected(self):
        self.archive.write_bytes(self.archive.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.plan()
        self.setUp()
        with self.assertRaisesRegex(ValueError, 'metadata mismatch: tarball-url'):
            publication.publication_plan(self.directory, 'opencpn/climatology-alpha', self.version)
        self.archive.rename(self.archive.with_name(self.archive.name.replace('wx334', 'wx329')))
        self.xml.rename(self.xml.with_name(self.xml.name.replace('wx334', 'wx329')))
        with self.assertRaisesRegex(ValueError, 'primary wxWidgets'):
            self.plan()

    def test_publisher_uploads_tarball_before_xml_and_verifies_both_downloads(self):
        plan = self.plan()
        outputs = [self.archive.read_bytes(), self.xml.read_bytes()]
        with patch.object(publication, 'existing_objects', return_value=[]), \
                patch.object(publication.subprocess, 'run') as run, \
                patch.object(publication.urllib.request, 'urlopen') as urlopen:
            run.return_value.returncode = 0
            urlopen.side_effect = [io.BytesIO(data) for data in outputs]
            publication.publish(plan)
            self.assertEqual([call.args[0][-1] for call in run.call_args_list], [str(self.archive), str(self.xml)])
            self.assertTrue(all('--no-republish' in call.args[0] for call in run.call_args_list))

    def test_idempotent_retry_does_not_replace_published_bytes(self):
        plan = self.plan()
        existing = [dict(name=p['name'], version=p['version'], checksum_sha256=p['sha256']) for p in plan['objects']]
        with patch.object(publication, 'existing_objects', return_value=existing), \
                patch.object(publication.subprocess, 'run') as run, \
                patch.object(publication.urllib.request, 'urlopen') as urlopen:
            urlopen.side_effect = [io.BytesIO(self.archive.read_bytes()), io.BytesIO(self.xml.read_bytes())]
            publication.publish(plan)
            run.assert_not_called()
            existing[0]['checksum_sha256'] = 'different'
            with self.assertRaisesRegex(ValueError, 'replace different published bytes'):
                publication.publish(plan)


if __name__ == '__main__':
    unittest.main()
