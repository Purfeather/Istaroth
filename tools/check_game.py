import hashlib
import json
from pathlib import Path
import sys

profile = json.loads((Path(__file__).resolve().parents[1]/'profiles/compatibility.json').read_text())
target = Path(sys.argv[1])
with target.open('rb') as source:
    actual = hashlib.file_digest(source, 'sha256').hexdigest()
matched = target.name.casefold() == profile['executable'].casefold() and actual == profile['sha256']
print(json.dumps({'matched': matched, 'sha256': actual}))
raise SystemExit(0 if matched else 1)
