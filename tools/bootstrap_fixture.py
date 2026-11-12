import hashlib
from pathlib import Path
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for name, content in [('bootstrap-game.exe', b'owned game'), ('bootstrap-payload.dll', b'owned payload')]:
    (out / name).write_bytes(content)
(out / 'resident_payloads.h').write_text('''#pragma once
struct Payload { const wchar_t* name; const char* sha256; };
static const wchar_t* TargetName=L"bootstrap-game.exe";
static const char* TargetSha256="%s";
static const wchar_t* ResidentPayload=L"bootstrap-payload.dll";
static const Payload Payloads[]={{L"bootstrap-payload.dll","%s"}};
''' % tuple(hashlib.sha256(x).hexdigest() for x in [b'owned game', b'owned payload']), encoding='ascii')
