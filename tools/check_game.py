import argparse
import hashlib
import json
from pathlib import Path

builds = json.loads((Path(__file__).resolve().parents[1]/'profiles/compatibility.json').read_text())['builds']
parser = argparse.ArgumentParser(description='Hash a local game file without starting it.')
parser.add_argument('executable', type=Path)
target = parser.parse_args().executable
try:
    with target.open('rb') as source:
        actual = hashlib.file_digest(source, 'sha256').hexdigest()
except OSError as error:
    print(json.dumps({'matched': False, 'error': error.__class__.__name__}))
    raise SystemExit(2)
matched = any(target.name.casefold() == build['executable'].casefold() and actual == build['sha256']
              for build in builds)
print(json.dumps({'matched': matched, 'sha256': actual}))
raise SystemExit(0 if matched else 1)
