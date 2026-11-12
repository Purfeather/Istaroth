"""Allowlisted local review packages. This script never uploads a release."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BINARIES = ['Istaroth.exe','Istaroth.dll','time_core.dll','story_probe.dll','dialogue_actions.dll']
BASELINE_SHA = 'bc8bef8a68e2aaa8534b46f73a65b940e9d1a59657afe51a06d156c7624b2a16'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def license_bundle():
    names = ['NOTICE','LICENSE','third_party/imgui/LICENSE.txt','third_party/detours/LICENSE.md',
             'licenses/Akebi-Apache-2.0.txt','licenses/Hutao-MIT.txt','licenses/Hutao-UnlockerIsland-MIT.txt','assets/RIGHTS.md']
    return '\n\n'.join(f'===== {name} =====\n' + (ROOT/name).read_text(encoding='utf8') for name in names).encode('utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--baseline', type=Path, help='Immutable accepted original ZIP; exact SHA256 required')
    source.add_argument('--slim', type=Path, help='Clean-source build directory for the unaccepted candidate')
    parser.add_argument('--destination', type=Path, default=ROOT/'dist')
    args = parser.parse_args()
    if args.baseline:
        data = args.baseline.read_bytes()
        if sha(data) != BASELINE_SHA:
            parser.error('Baseline archive SHA256 mismatch')
        with zipfile.ZipFile(args.baseline) as archive:
            entries = {Path(x.filename).name: x for x in archive.infolist() if not x.is_dir()}
            payload = {name: archive.read(entries[name]) for name in BINARIES}
            payload['assets/istaroth-mark.png'] = archive.read(entries['istaroth-mark.png'])
            payload['Istaroth.ini'] = archive.read(entries['Istaroth.ini'])
        assert 'GamePath=\r\n' in payload['Istaroth.ini'].decode('utf16')
        accepted = json.loads((ROOT/'docs/accepted-v0.1.0.json').read_text(encoding='utf8'))
        assert {x: sha(payload[x]) for x in BINARIES} == accepted['binaries']
        version, name, live = '0.1.0', 'Istaroth-v0.1.0-win64', True
        payload['使用说明.txt'] = (ROOT/'README.md').read_bytes()
    else:
        manifest = json.loads((args.slim/'build.json').read_text(encoding='utf8'))
        if manifest['version'] != '0.1.1-candidate':
            parser.error('Slim packaging requires a -Slim build manifest')
        tests = json.loads((args.slim/'test-results.json').read_text(encoding='utf8'))
        assert tests['passed'], 'Candidate offline tests did not pass'
        payload = {x: (args.slim/x).read_bytes() for x in BINARIES}
        assert {x: sha(payload[x]) for x in BINARIES} == manifest['binaries']
        version, name, live = '0.1.1-candidate', 'Istaroth-v0.1.1-candidate-win64', False
    payload['LICENSES.txt'] = license_bundle()
    metadata = {'version': version, 'kind': 'accepted-binary-repackage' if live else 'clean-source-candidate',
        'live_acceptance': live, 'publication_ready': False,
        'publication_blockers': ['Bot v5 third-party character/reference redistribution basis is not documented'],
        'files': {x: {'bytes': len(data), 'sha256': sha(data)} for x,data in sorted(payload.items())}}
    if live:
        payload['manifest.json'] = (json.dumps(metadata, ensure_ascii=False, indent=2)+'\n').encode('utf8')
    else:
        assert len(payload) == 6
    args.destination.mkdir(parents=True, exist_ok=True)
    target = args.destination/(name+'.zip')
    if target.exists():
        parser.error('Destination already exists; do not overwrite an archived package')
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path,data in sorted(payload.items()):
            archive.writestr(name+'/'+path, data)
    # Re-open and check every entry, not just the archive hash.
    with zipfile.ZipFile(target) as archive:
        assert len(archive.namelist()) == len(payload)
        for path,data in payload.items():
            assert archive.read(name+'/'+path) == data
        assert not archive.testzip()
    checksum = sha(target.read_bytes())
    (args.destination/(name+'.sha256')).write_text(f'{checksum}  {target.name}\n', encoding='ascii')
    metadata['archive'] = {'file': target.name, 'bytes': target.stat().st_size, 'sha256': checksum}
    metadata['initial_file_count'] = len(payload)
    (args.destination/(name+'.manifest.json')).write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps({'archive': str(target), 'file_count': len(payload), 'sha256': checksum, 'publication_ready': False}))


if __name__ == '__main__':
    main()
