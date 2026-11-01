#!/usr/bin/env python3
"""Validate retained CI artifacts and assemble explicit public release assets."""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tarfile
import xml.etree.ElementTree as ET

from windows_abi import validate as validate_windows_abi

SOURCE = Path(__file__).resolve().parents[2]
VERSION = '2.0.1.0'
BASELINE = 'cd00282e6ea2784a6d78ccfe47fed713269ad87e'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def collect(roots, destination, expected):
    assert not destination.exists() or not any(destination.iterdir()), 'Use an empty asset directory'
    selected = {}
    for root in roots:
        for archive in sorted(root.rglob('*.tar.gz')):
            if archive.name in selected:
                assert archive.read_bytes() == selected[archive.name].read_bytes(), archive.name
            selected[archive.name] = archive
    assert len(selected) == expected, (len(selected), expected)
    manifest = json.loads((SOURCE / 'data/dataset-manifest.json').read_text())
    schema = SOURCE / 'ci/public-packages/ocpn-plugin.xsd'
    catalogue = ET.Element('plugins', version='1')
    records = []
    for name, archive in sorted(selected.items()):
        metadata = archive.with_name(name.removesuffix('.tar.gz') + '.xml')
        summary = archive.with_name(name.removesuffix('.tar.gz') + '.json')
        record = json.loads(summary.read_text())
        digest = sha(archive.read_bytes())
        assert record['file'] == name and record['sha256'] == digest
        assert record['bytes'] == archive.stat().st_size
        assert record['plugin_version'] == VERSION and record['baseline_revision'] == BASELINE
        assert record['dataset_version'] == manifest['dataset_version']
        if record['target'] == 'msvc-64':
            assert record.get('wx_version') == '3.3.4', 'Legacy wx329 packages must stay outside the catalogue collection'
            assert name.endswith('-wx334.tar.gz'), name
        subprocess.run(['xmllint', '--noout', '--schema', str(schema), str(metadata)], check=True, capture_output=True)
        plugin = ET.parse(metadata).getroot()
        assert plugin.findtext('version') == VERSION
        assert plugin.findtext('tarball-checksum') == digest
        assert plugin.findtext('tarball-url') == record['url']
        assert plugin.findtext('target') == record['target']
        assert plugin.findtext('target-version') == record['target_version']
        assert plugin.findtext('target-arch') == record['architecture']
        with tarfile.open(archive) as tar:
            members = tar.getmembers()
            paths = [PurePosixPath(m.name) for m in members]
            assert all(m.isfile() for m in members), 'No links or special entries'
            assert all(not p.is_absolute() and '..' not in p.parts for p in paths)
            assert len(set(p.parts[0] for p in paths)) == 1
            root = paths[0].parts[0]
            embedded = ET.fromstring(tar.extractfile(root + '/metadata.xml').read())
            external = copy.deepcopy(plugin)
            external.remove(external.find('tarball-checksum'))
            assert [(e.tag, e.text.strip() if e.text else '') for e in embedded] == [(e.tag, e.text.strip() if e.text else '') for e in external]
            provenance_paths = [m.name for m in members if m.name.endswith('/data/package-provenance.json')]
            assert len(provenance_paths) == 1
            provenance = json.load(tar.extractfile(provenance_paths[0]))
            for key, value in provenance.items():
                assert record[key] == value, key
            data_root = provenance_paths[0].removesuffix('/package-provenance.json')
            for output in manifest['outputs']:
                data = tar.extractfile(data_root + '/' + output['file']).read()
                assert len(data) == output['bytes'] and sha(data) == output['sha256'], output['file']
            assert tar.extractfile(data_root + '/COPYING').read()
            binaries = [m for m in members if m.name.endswith(('/libclimatology_pi.so', '/climatology_pi.dll', '/libclimatology_pi.dylib'))]
            assert len(binaries) == 1
            binary = tar.extractfile(binaries[0]).read()
            assert sha(binary) == record['binary_sha256']
            arch = record['architecture']
            if binary[:4] == b'\x7fELF':
                assert struct.unpack_from('<H', binary, 18)[0] == {'armhf':40, 'arm64':183, 'aarch64':183, 'x86_64':62}[arch]
            elif binary[:2] == b'MZ':
                pe = struct.unpack_from('<I', binary, 0x3c)[0]
                assert struct.unpack_from('<H', binary, pe+4)[0] == {'x86':0x14c, 'x86_64':0x8664}[arch]
                if arch == 'x86_64':
                    validate_windows_abi(binary, record['wx_version'])
            else:
                assert arch == 'arm64;x86_64'
            assert sum(m.name.endswith('.mo') for m in members) == record['translations'] >= 30
            assert any('/UserIcons/' in m.name for m in members)
        destination.mkdir(parents=True, exist_ok=True)
        for path in (archive, metadata, summary):
            shutil.copyfile(path, destination / path.name)
        records.append(record)
        catalogue.append(plugin)
    ET.indent(catalogue)
    (destination / f'climatology-{VERSION}-catalogue.xml').write_bytes(ET.tostring(catalogue, encoding='utf-8', xml_declaration=True) + b'\n')
    (destination / 'package-index.json').write_text(json.dumps(dict(plugin_version=VERSION, dataset_version=manifest['dataset_version'], packages=records), indent=2) + '\n')
    checksums = [f'{sha(path.read_bytes())}  {path.name}' for path in sorted(destination.iterdir()) if path.is_file()]
    (destination / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
    print(f'Validated {len(records)} platform packages and {len(records)*len(manifest["outputs"])} dataset output hashes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('roots', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--expected', type=int, default=23)
    args = parser.parse_args()
    collect(args.roots, args.output, args.expected)
