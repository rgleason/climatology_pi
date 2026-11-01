"""Retain one complete XML/archive pair per package, without changing upload inputs."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
from urllib.parse import quote, unquote, urlsplit
from uuid import UUID
import xml.etree.ElementTree as ET


def checksum(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def retain(source, destination, artifact_base=None):
    archives = sorted(source.glob('*.tar.gz'))
    metadata = {path: ET.parse(path).getroot() for path in sorted(source.glob('*.xml'))}
    if not archives:
        raise ValueError(f'No built plugin archive found in {source}/')
    if len(metadata) != len(archives):
        raise ValueError(f'Expected one companion XML per archive in {source}/; '
                         f'found {len(metadata)} XMLs and {len(archives)} archives')
    if artifact_base is None and os.environ.get('CIRCLE_WORKFLOW_JOB_ID'):
        job = UUID(os.environ['CIRCLE_WORKFLOW_JOB_ID'])
        node = int(os.environ.get('CIRCLE_NODE_INDEX', '0'))
        if node < 0:
            raise ValueError('Invalid CircleCI node index')
        artifact_base = f'https://output.circle-artifacts.com/output/job/{job}/artifacts/{node}/packages'

    pairs = []
    for archive in archives:
        stem = archive.name.removesuffix('.tar.gz')
        matches = [path for path, root in metadata.items()
                   if path.stem == stem or
                   unquote(urlsplit(root.findtext('tarball-url', '').strip()).path).rsplit('/', 1)[-1] == archive.name]
        # The toolkit's Flatpak archive and XML use different basenames.
        if not matches and len(archives) == 1 and len(metadata) == 1:
            matches = list(metadata)
        if len(matches) != 1:
            raise ValueError(f'Cannot identify the companion XML for {archive.name}')
        root = metadata.pop(matches[0])
        if root.tag not in ('plugin', 'opencpn-plugin'):
            raise ValueError(f'Invalid plugin XML for {archive.name}')
        for element in root.iter():
            if element.text:
                element.text = element.text.strip()
        for field in ('name', 'version', 'api-version', 'target', 'target-version', 'target-arch'):
            if not root.findtext(field):
                raise ValueError(f'Missing {field} in the XML for {archive.name}')
        url = root.find('tarball-url')
        if url is None:
            raise ValueError(f'Missing tarball-url in the XML for {archive.name}')
        if artifact_base:
            url.text = artifact_base.rstrip('/') + '/' + quote(archive.name, safe='')
        placeholders = ('--pkg_repo--', '--name--', '--version--', '--filename--')
        parsed = urlsplit(url.text or '')
        if (not parsed.netloc or parsed.scheme not in ('https', 'http') or
                any(token in (url.text or '') for token in placeholders)):
            raise ValueError('An artifact base URL is required to finalize the XML download URL')
        digest = checksum(archive)
        value = root.find('tarball-checksum')
        if value is None:
            value = ET.Element('tarball-checksum')
            root.insert(list(root).index(url) + 1, value)
        value.text = digest
        ET.indent(root)
        pairs.append((archive, stem + '.xml', ET.tostring(root, encoding='utf-8') + b'\n', digest))

    destination.mkdir(parents=True, exist_ok=True)
    sums = []
    for archive, xml_name, xml, digest in pairs:
        shutil.copyfile(archive, destination / archive.name)
        (destination / xml_name).write_bytes(xml)
        sums.append(f'{digest}  {archive.name}\n')
    (destination / 'SHA256SUMS').write_text(''.join(sums), encoding='utf-8')
    print(f'Retained {len(pairs)} XML/archive pairs with complete URLs and SHA-256 checksums')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--artifact-base-url')
    args = parser.parse_args()
    retain(args.source, args.destination, args.artifact_base_url)
