import argparse
import hashlib
import json
from pathlib import Path

profile = json.loads((Path(__file__).resolve().parents[1]/'profiles/compatibility.json').read_text())
parser = argparse.ArgumentParser(description='Hash a local game file without starting it.')
parser.add_argument('executable', type=Path)
target = parser.parse_args().executable
try:
    with target.open('rb') as source:
        actual = hashlib.file_digest(source, 'sha256').hexdigest()
except OSError as error:
    print(json.dumps({'matched': False, 'error': error.__class__.__name__}))
    raise SystemExit(2)
matched = target.name.casefold() == profile['executable'].casefold() and actual == profile['sha256']
print(json.dumps({'matched': matched, 'sha256': actual}))
raise SystemExit(0 if matched else 1)
