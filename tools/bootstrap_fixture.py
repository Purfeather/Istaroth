import hashlib
from pathlib import Path
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
fixtures = {
    'bootstrap-game.exe': b'owned game',
    'bootstrap-os.exe': b'owned overseas game',
    'bootstrap-payload.dll': b'owned payload',
}
for name, content in fixtures.items():
    (out / name).write_bytes(content)

mismatch = out / 'mismatched'
mismatch.mkdir(exist_ok=True)
(mismatch / 'bootstrap-os.exe').write_bytes(fixtures['bootstrap-game.exe'])

def entry(name):
    return '{L"%s","%s"}' % (name, hashlib.sha256(fixtures[name]).hexdigest())

(out / 'resident_payloads.h').write_text('''#pragma once
struct Payload { const wchar_t* name; const char* sha256; };
static const Payload GameTargets[]={%s,%s};
#ifdef BASELINE
static const wchar_t* TargetName=GameTargets[0].name;
static const char* TargetSha256=GameTargets[0].sha256;
#endif
static const wchar_t* ResidentPayload=L"bootstrap-payload.dll";
static const Payload Payloads[]={%s};
''' % tuple(entry(name) for name in fixtures), encoding='ascii')
