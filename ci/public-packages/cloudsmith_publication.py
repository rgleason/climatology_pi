"""Publish complete, already qualified x64 packages without repacking them.

The older toolkit uploader assembles CPack archives during deployment. These
packages already contain their metadata and must retain their tested bytes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

from release import VERSION
from windows_abi import validate as validate_windows_abi


def digest(data):
    return hashlib.sha256(data).hexdigest()


def context(env=None):
    env = os.environ if env is None else env
    branch, tag = env.get('CIRCLE_BRANCH', ''), env.get('CIRCLE_TAG', '')
    pull_request = (env.get('CIRCLE_PULL_REQUEST') or env.get('CIRCLE_PULL_REQUESTS')
                    or branch.startswith('pull/'))
    level = 'PROD' if branch.lower() == 'master' and tag else (
        'BETA' if branch.lower() == 'master' or tag or pull_request else 'ALPHA')
    repository = env.get('CLOUDSMITH_' + level + '_REPO',
                         'opencpn/climatology-' + level.lower())
    revision = env.get('CIRCLE_SHA1') or subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], text=True).strip()
    version = tag or VERSION + '+' + env.get('CIRCLE_BUILD_NUM', '1') + '.' + revision[:7]
    if not re.fullmatch(r'[\w-]+/[\w-]+', repository):
        raise ValueError('Invalid Cloudsmith repository')
    return repository, version


def download_url(repository, version, filename, kind='tarball'):
    stem = filename[:-len('.tar.gz')] if filename.endswith('.tar.gz') else Path(filename).stem
    return ('https://dl.cloudsmith.io/public/' + repository + '/raw/names/' +
            urllib.parse.quote(stem + '-' + kind, safe='') + '/versions/' +
            urllib.parse.quote(version, safe='') + '/' + urllib.parse.quote(filename, safe=''))


def publication_plan(directory, repository, version):
    archives = sorted(directory.glob('*.tar.gz'))
    if len(archives) != 1:
        raise ValueError('Expected exactly one complete Windows x64 archive')
    archive = archives[0]
    xml = archive.with_name(archive.name[:-len('.tar.gz')] + '.xml')
    plugin = ET.parse(xml).getroot()
    expected_name = 'climatology_pi-' + VERSION + '-msvc-64-10-x86_64-wx334.tar.gz'
    if archive.name != expected_name:
        raise ValueError('Only the primary wxWidgets 3.3.4 package can be published')
    expected = {'name': 'Climatology', 'version': VERSION, 'target': 'msvc-64',
                'target-version': '10', 'target-arch': 'x86_64',
                'tarball-url': download_url(repository, version, archive.name)}
    content = archive.read_bytes()
    if plugin.findtext('tarball-checksum') != digest(content):
        raise ValueError('Archive checksum differs from its companion XML')
    with tarfile.open(archive) as tar:
        metadata = [p for p in tar.getmembers() if p.name.endswith('/metadata.xml')]
        libraries = [p for p in tar.getmembers() if p.name.endswith('/plugins/climatology_pi.dll')]
        if len(metadata) != 1 or len(libraries) != 1:
            raise ValueError('Expected one embedded metadata file and one plugin DLL')
        embedded = ET.fromstring(tar.extractfile(metadata[0]).read())
        validate_windows_abi(tar.extractfile(libraries[0]).read(), '3.3.4')
    for field, value in expected.items():
        if plugin.findtext(field) != value or embedded.findtext(field) != value:
            raise ValueError('External/embedded publication metadata mismatch: ' + field)
    objects = []
    for path, kind in ((archive, 'tarball'), (xml, 'metadata')):
        objects.append(dict(file=str(path), name=archive.name[:-len('.tar.gz')] + '-' + kind,
                            version=version, sha256=digest(path.read_bytes()),
                            url=download_url(repository, version, path.name, kind)))
    return dict(repository=repository, plugin_version=VERSION, objects=objects)


def existing_objects(repository, version):
    url = ('https://api.cloudsmith.io/v1/packages/' + repository +
           '/?page_size=100&query=' + urllib.parse.quote(version, safe=''))
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def publish(plan):
    existing = existing_objects(plan['repository'], plan['objects'][0]['version'])
    for item in plan['objects']:
        if digest(Path(item['file']).read_bytes()) != item['sha256']:
            raise ValueError('Publication input changed after qualification')
        matches = [p for p in existing if p['name'] == item['name'] and p['version'] == item['version']]
        if matches:
            if len(matches) != 1 or matches[0]['checksum_sha256'] != item['sha256']:
                raise ValueError('Refusing to replace different published bytes: ' + item['name'])
        else:
            # Wait for synchronization so an XML is never announced ahead of its tarball.
            result = subprocess.run([
                'cloudsmith', 'push', 'raw', '--no-republish', '--name', item['name'],
                '--version', item['version'], '--summary', 'Climatology Windows x64 package',
                plan['repository'], item['file']], capture_output=True, text=True, timeout=600)
            if result.returncode:
                # CLI diagnostics can contain credential/configuration details.
                raise RuntimeError('Cloudsmith upload failed for ' + Path(item['file']).name)
        with urllib.request.urlopen(item['url'], timeout=120) as response:
            if digest(response.read()) != item['sha256']:
                raise ValueError('Public download differs from qualified publication input')
        print('Published and verified:', item['url'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=Path('public-packages'))
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    repository, version = context()
    plan = publication_plan(args.directory, repository, version)
    (args.directory / 'publication-plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    if args.publish:
        publish(plan)
    else:
        print(json.dumps(plan, indent=2))
