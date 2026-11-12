"""Offline tests. Only subprocesses created here are observed or terminated."""
import ctypes as C
import json
from pathlib import Path
import subprocess
import sys
import time

out = Path(sys.argv[1]).resolve()
results = []


def run(name, args=(), directory=None):
    result = subprocess.run([str(out / 'tests' / name), *args], cwd=directory or out / 'tests',
                            capture_output=True, text=True, timeout=25, encoding='utf8', errors='replace')
    if result.returncode:
        raise RuntimeError(f'{name} failed ({result.returncode}): {result.stdout}\n{result.stderr}')
    results.append({'test': name, 'args': list(args), 'passed': True, 'output': result.stdout.strip()})


def wait(predicate, seconds=5):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError('Owned fixture readiness/state timed out')


kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.OpenFileMappingW.argtypes = [C.c_uint32, C.c_int, C.c_wchar_p]
kernel.OpenFileMappingW.restype = C.c_void_p
kernel.MapViewOfFile.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_size_t]
kernel.MapViewOfFile.restype = C.c_void_p
kernel.UnmapViewOfFile.argtypes = [C.c_void_p]
kernel.CloseHandle.argtypes = [C.c_void_p]


def owned_mapping(process, prefix):
    handle = None
    end = time.monotonic() + 5
    while time.monotonic() < end:
        assert process.poll() is None, 'Owned fixture exited before mapping'
        handle = kernel.OpenFileMappingW(0xf001f, False, prefix + str(process.pid))
        if handle:
            break
        time.sleep(.02)
    assert handle, 'Owned fixture did not publish mapping'
    address = kernel.MapViewOfFile(handle, 0xf001f, 0, 0, 0)
    if not address:
        kernel.CloseHandle(handle)
        raise OSError(C.get_last_error())
    return handle, address


class Clock(C.Structure):
    _fields_ = [(x, C.c_uint32) for x in ['magic','version','pid','status']] + [
        (x, C.c_int32) for x in ['command','enable','milli','stop','error','lease']] + [
        ('scale', C.c_float), ('baseline', C.c_float), ('heartbeat', C.c_uint64),
        ('clocks', C.c_double * 4), ('sequence', C.c_int32), ('restores', C.c_int32), ('ack', C.c_int32)]


def clock_test():
    assert C.sizeof(Clock) == 104
    child = subprocess.Popen([str(out/'tests/gst-time-test.exe'), str(out/'tests/time_core_test.dll')], cwd=out/'tests')
    handle = address = None
    try:
        handle, address = owned_mapping(child, 'Local\\GenshinSkip.Time.')
        state = Clock.from_address(address)
        wait(lambda: state.magic == 0x47535431 and state.heartbeat != 0)
        assert state.pid == child.pid and abs(state.scale-1) < .01
        state.command = 1; state.enable = 1; state.milli = 2000; state.command = 2
        wait(lambda: state.ack == 2 and abs(state.scale-2) < .01)
        # An unrefreshed request must restore the original scale after its lease.
        wait(lambda: abs(state.scale-1) < .01 and state.restores > 0)
        state.command = 3; state.enable = 0; state.milli = 1000; state.stop = 1; state.command = 4
        wait(lambda: state.status == 7)
        assert abs(state.scale-1) < .01
        results.append({'test': 'owned-clock', 'passed': True, 'checks': ['initial baseline','2x apply','expired lease restores','stop restores']})
    finally:
        if address: kernel.UnmapViewOfFile(address)
        if handle: kernel.CloseHandle(handle)
        if child.poll() is None: child.terminate()
        child.wait(timeout=5)


def story_test():
    child = subprocess.Popen([str(out/'tests/gst-story-test.exe'), str(out/'tests/story_probe_test.dll')], cwd=out/'tests')
    handle = address = None
    try:
        handle, address = owned_mapping(child, 'Local\\GenshinSkip.Story.')
        fields = (C.c_uint32 * 10).from_address(address)
        wait(lambda: fields[0] == 0x47535331 and fields[3] == 2)
        assert fields[1] == 7 and fields[2] == child.pid
        wait(lambda: fields[5] > 0, 8)
        results.append({'test': 'owned-story', 'passed': True, 'records': fields[5]})
    finally:
        if address: kernel.UnmapViewOfFile(address)
        if handle: kernel.CloseHandle(handle)
        if child.poll() is None: child.terminate()
        child.wait(timeout=5)


try:
    run('control-test.exe')
    run('cursor-test.exe')
    run('loader-test.exe')
    for mode in ['handover','dead-listed','loaded','crash','repeat','cancel','module-absent']:
        run('bootstrap/bootstrap-test.exe', [mode], out/'tests/bootstrap')
    run('actions-test.exe')
    clock_test()
    story_test()
except Exception as error:
    (out/'test-results.json').write_text(json.dumps({'passed': False, 'error': str(error), 'results': results}, indent=2), encoding='utf8')
    raise
(out/'test-results.json').write_text(json.dumps({'passed': True, 'game_accessed': False, 'results': results}, indent=2)+'\n', encoding='utf8')
print(f'{len(results)} offline groups passed; no game process accessed.')
