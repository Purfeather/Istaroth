"""Check explicit publish roots, dependency integrity and common secret/path leaks."""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
allowed_dirs = {'.github','assets','docs','licenses','profiles','src','tests','third_party','tools'}
allowed_files = {'.gitattributes','.gitignore','build.ps1','README.md','LICENSE','NOTICE','VERSION','CHANGELOG.md','toolchain.json','SOURCE-ORIGINS.json'}
excluded = {'.git','out','out-slim','dist'}
errors = []
count = 0
for path in root.iterdir():
    if path.name in excluded:
        continue
    if path.is_dir() and path.name not in allowed_dirs or path.is_file() and path.name not in allowed_files:
        errors.append('Unexpected root entry: '+path.name)
files = [p for p in root.rglob('*') if p.is_file() and p.relative_to(root).parts[0] not in excluded]
for path in files:
    relative = path.relative_to(root).as_posix()
    if path.suffix.lower() in {'.exe','.dll','.lib','.obj','.log','.pdb'} or path.name == 'Istaroth.ini':
        errors.append('Generated or private runtime file: '+relative)
    if path.suffix in {'.png','.ico'}:
        continue
    text = path.read_text(encoding='utf8')
    # Report only location/category; never print a matched credential.
    patterns = {'github_token': r'gh[pousr]_[A-Za-z0-9]{30,}', 'private_key': r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
                'developer_path': r'(?:F|H):[\\/]|C:[\\/]Users[\\/]Administrator'}
    for category,pattern in patterns.items():
        if re.search(pattern,text): errors.append(category+': '+relative)
    count += 1
for dependency in ['imgui','detours']:
    folder = root/'third_party'/dependency
    source = json.loads((folder/'SOURCE.json').read_text(encoding='utf8'))
    for item in source['files']:
        path = folder/item['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            errors.append('Dependency hash mismatch: '+dependency+'/'+item['path'])
for name in ['LICENSE','NOTICE','assets/RIGHTS.md','docs/PROVENANCE.md']:
    if not (root/name).read_text(encoding='utf8').strip(): errors.append('Missing notice: '+name)
print(json.dumps({'passed': not errors, 'public_files': len(files), 'text_files_checked': count, 'errors': errors}, ensure_ascii=False))
raise SystemExit(1 if errors else 0)
