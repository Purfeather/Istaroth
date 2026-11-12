import hashlib
import json
from pathlib import Path
import subprocess
import sys

root, out = map(Path, sys.argv[1:3])
try:
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], stderr=subprocess.DEVNULL, text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).strip())
except subprocess.CalledProcessError:
    commit = None
    dirty = None

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

manifest = {'version': sys.argv[3], 'kind': 'clean-source-build', 'live_acceptance': False,
    'source_commit': commit, 'source_dirty': dirty, 'toolchain': json.loads((root / 'toolchain.json').read_text()),
    'compatibility': json.loads((root / 'profiles/compatibility.json').read_text()),
    'dependencies': {name: json.loads((root / 'third_party' / name / 'SOURCE.json').read_text())['commit'] for name in ['imgui', 'detours']},
    'inputs': {p.relative_to(root).as_posix(): digest(p) for folder in ['src','profiles','assets','tools','tests'] for p in sorted((root / folder).rglob('*')) if p.is_file()},
    'binaries': {name: digest(out / name) for name in ['Istaroth.exe','Istaroth.dll','time_core.dll','story_probe.dll','dialogue_actions.dll']}}
(out / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
