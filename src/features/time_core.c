#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#ifndef GST_SELF_TEST
#include "time_profile.h"
#endif

typedef struct {
    DWORD magic, version, pid, status;
    volatile LONG command_seq, enable, requested_milli, stop;
    LONG error, lease_active;
    float scale, baseline;
    ULONGLONG heartbeat_tick;
    double clocks[4];
    volatile LONG snapshot_seq;
    LONG restore_count;
    LONG ack_seq;
} Shared;
_Static_assert(sizeof(Shared) == 104, "Shared ABI");

static HMODULE module;
static BYTE *image;
static Shared *state;
static volatile float *owned;
static float original, applied;
static BOOL blocked;
static BOOL profile_valid;
static BOOL recovering;
static LONG restores;
static LONG acknowledged;
#ifdef GST_SELF_TEST
static volatile float *test_field;
static volatile double *test_clocks;
#endif

static void copy_bytes(void *target, const void *source, SIZE_T size) {
    volatile BYTE *out = (volatile BYTE *)target;
    const volatile BYTE *in = (const volatile BYTE *)source;
    for (SIZE_T i = 0; i < size; i++) out[i] = in[i];
}

static BOOL copy_checked(void *target, const void *source, SIZE_T size) {
    __try {
        copy_bytes(target, source, size);
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static BOOL range_ok(const void *address, SIZE_T size, BOOL write) {
    MEMORY_BASIC_INFORMATION info;
    uintptr_t first = (uintptr_t)address;
    if (first < 0x10000 || size > 4096 || first + size < first) return FALSE;
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return FALSE;
    if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        first + size > (uintptr_t)info.BaseAddress + info.RegionSize) return FALSE;
    DWORD allowed = write ? (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
                          : (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return (info.Protect & allowed) != 0;
}

static volatile float *field(void) {
#ifdef GST_SELF_TEST
    return range_ok((const void *)test_field, sizeof(float), TRUE) ? test_field : NULL;
#else
    uintptr_t *slot = (uintptr_t *)(image + GST_SLOT_RVA);
    if (!range_ok(slot, sizeof(*slot), FALSE)) return NULL;
    uintptr_t manager;
    if (!copy_checked(&manager, slot, sizeof(manager))) return NULL;
    if (manager < 0x10000 || manager > 0x7ffffffff000ULL || (manager & 7)) return NULL;
    volatile float *value = (volatile float *)(manager + GST_FIELD_OFFSET);
    return range_ok((const void *)value, sizeof(float), TRUE) ? value : NULL;
#endif
}

static BOOL replace_scale(volatile float *target, float expected, float desired) {
    LONG before, after;
    copy_bytes(&before, &expected, sizeof(before));
    copy_bytes(&after, &desired, sizeof(after));
    __try {
        return InterlockedCompareExchange((volatile LONG *)target, after, before) == before;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static BOOL restore(void) {
    if (!owned) return TRUE;
    volatile float *current = field();
    if (!current) return FALSE;
    if (current != owned) {
        owned = NULL;
        return TRUE;
    }
    if (replace_scale(owned, applied, original)) {
        restores++;
        owned = NULL;
        return TRUE;
    }
    float observed;
    if (copy_checked(&observed, (const void *)owned, sizeof(observed)) && observed != applied) {
        owned = NULL;
        return TRUE;
    }
    return FALSE;
}

static void publish(DWORD status, LONG error) {
    InterlockedIncrement(&state->snapshot_seq);
    state->status = status;
    state->error = error;
    state->lease_active = owned != NULL;
    state->baseline = owned ? original : 0;
    volatile float *current = profile_valid ? field() : NULL;
    state->scale = 0;
    if (current) copy_checked(&state->scale, (const void *)current, sizeof(float));
    state->heartbeat_tick = GetTickCount64();
    state->restore_count = restores;
    state->ack_seq = acknowledged;
    for (int i = 0; i < 4; i++) {
#ifdef GST_SELF_TEST
        const volatile double *clock = test_clocks ? test_clocks + i : NULL;
#else
        const SIZE_T offsets[4] = {0x40, 0x50, 0xa0, 0xb0};
        const volatile double *clock = current ? (const volatile double *)((BYTE *)current - GST_FIELD_OFFSET + offsets[i]) : NULL;
#endif
        state->clocks[i] = 0;
        if (range_ok((const void *)clock, sizeof(double), FALSE)) copy_checked(&state->clocks[i], (const void *)clock, sizeof(double));
    }
    InterlockedIncrement(&state->snapshot_seq);
}

static BOOL check_profile(void) {
    image = (BYTE *)GetModuleHandleW(NULL);
#ifdef GST_SELF_TEST
    test_field = (volatile float *)GetProcAddress((HMODULE)image, "GSTTestTimeScale");
    test_clocks = (volatile double *)GetProcAddress((HMODULE)image, "GSTTestClocks");
    return test_field && test_clocks;
#else
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return FALSE;
    IMAGE_NT_HEADERS64 *pe = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        pe->OptionalHeader.SizeOfImage != GST_IMAGE_SIZE) return FALSE;
    for (SIZE_T i = 0; i < sizeof(gst_checks) / sizeof(gst_checks[0]); i++) {
        const GSTCheck *check = &gst_checks[i];
        BYTE actual[64];
        if (!range_ok(image + check->rva, check->size, FALSE) ||
            !copy_checked(actual, image + check->rva, check->size)) return FALSE;
        for (SIZE_T j = 0; j < check->size; j++) if (actual[j] != check->bytes[j]) return FALSE;
    }
    return TRUE;
#endif
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    wchar_t name[96];
    const wchar_t prefix[] = L"Local\\GenshinSkip.Time.";
    SIZE_T length = 0;
    while (prefix[length]) { name[length] = prefix[length]; length++; }
    wchar_t digits[10];
    SIZE_T count = 0;
    DWORD pid = GetCurrentProcessId();
    do { digits[count++] = L'0' + pid % 10; pid /= 10; } while (pid);
    while (count) name[length++] = digits[--count];
    name[length] = 0;
    SetLastError(ERROR_SUCCESS);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(Shared), name);
    if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mapping) CloseHandle(mapping);
        FreeLibraryAndExitThread(module, ERROR_ALREADY_EXISTS);
    }
    state = (Shared *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!state) {
        CloseHandle(mapping);
        FreeLibraryAndExitThread(module, ERROR_NOT_ENOUGH_MEMORY);
    }
    state->magic = 0x47535431;
    state->version = 1;
    state->pid = GetCurrentProcessId();
    BOOL valid = check_profile();
    profile_valid = valid;
    ULONGLONG started = GetTickCount64(), last_command = started;
    LONG last_seq = 0, enabled = 0, desired = 1000;
    BOOL had_command = FALSE;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        LONG sequence = InterlockedCompareExchange(&state->command_seq, 0, 0);
        if (!(sequence & 1) && sequence != last_seq) {
            LONG request = state->enable, milli = state->requested_milli, stop = state->stop;
            MemoryBarrier();
            if (sequence == InterlockedCompareExchange(&state->command_seq, 0, 0) &&
                (request == 0 || request == 1) && milli >= 1000 && milli <= 4000) {
                last_seq = sequence;
                acknowledged = sequence;
                last_command = now;
                had_command = TRUE;
                enabled = request;
                desired = milli;
                if (stop) break;
            }
        }
        BOOL active = valid && enabled && now - last_command <= 400 && !recovering;
        DWORD status = valid ? 0 : 3;
        LONG error = valid ? 0 : ERROR_REVISION_MISMATCH;
        volatile float *current = valid ? field() : NULL;
        float value = 0;
        if (current && !copy_checked(&value, (const void *)current, sizeof(value))) current = NULL;
        if (!active) {
            recovering = !restore();
            blocked = FALSE;
        } else if (owned && (!current || current != owned || value != applied)) {
            if (!current) recovering = TRUE;
            else {
                owned = NULL;
                blocked = TRUE;
            }
        }
        if (current) {
            status = 1;
            if (active && !blocked && value == value && value > 0 && value <= (owned ? 8 : 2)) {
                float baseline = owned ? original : value;
                float target = baseline * ((float)desired / 1000.0f);
                if (replace_scale(current, value, target)) {
                    original = baseline;
                    applied = target;
                    owned = current;
                    status = 2;
                } else {
                    owned = NULL;
                    blocked = TRUE;
                }
            }
            if (blocked) status = 6;
        }
        if (recovering) {
            status = 4;
            error = ERROR_WRITE_FAULT;
        }
        publish(status, error);
        if ((had_command && now - last_command > 5000) || (!had_command && now - started > 60000)) break;
        Sleep(10);
    }
    while (!restore()) {
        publish(4, ERROR_WRITE_FAULT);
        Sleep(10);
    }
    publish(7, 0);
    UnmapViewOfFile(state);
    CloseHandle(mapping);
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH];
        DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
        if (!length || length >= MAX_PATH) return FALSE;
        const wchar_t *name = path;
        for (DWORD i = 0; i < length; i++) if (path[i] == L'\\') name = path + i + 1;
#ifdef GST_SELF_TEST
        if (lstrcmpiW(name, L"gst-time-test.exe")) return FALSE;
#else
        if (lstrcmpiW(name, L"YuanShen.exe")) return FALSE;
#endif
        module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
