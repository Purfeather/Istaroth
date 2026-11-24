#pragma once
#include <windows.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    const wchar_t *executable;
    DWORD timestamp;
    DWORD image_size;
    BOOL overseas;
} IstarothGameBuild;

typedef struct {
    DWORD rva;
    SIZE_T size;
    BYTE bytes[64];
} IstarothCodeCheck;

static inline const IstarothGameBuild *IstarothFindBuild(const BYTE *image, const wchar_t *name) {
    static const IstarothGameBuild builds[] = {
        {L"YuanShen.exe", 0x6ad886fb, 0x1b65f000, FALSE},
        {L"GenshinImpact.exe", 0x6ad8992c, 0x1b690000, TRUE},
    };
    if (!image || !name) return NULL;

    __try {
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew < (LONG)sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 4096) {
            return NULL;
        }
        const IMAGE_NT_HEADERS64 *pe = (const IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
        if (pe->Signature != IMAGE_NT_SIGNATURE ||
            pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            pe->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            return NULL;
        }
        for (SIZE_T index = 0; index < ARRAYSIZE(builds); ++index) {
            const IstarothGameBuild *build = &builds[index];
            if (pe->FileHeader.TimeDateStamp == build->timestamp &&
                pe->OptionalHeader.SizeOfImage == build->image_size &&
                lstrcmpiW(name, build->executable) == 0) {
                return build;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    return NULL;
}

static inline const IstarothGameBuild *IstarothCurrentBuild(void) {
    // Only called on process attach. Keep the path buffer off the loader stack.
    static wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return NULL;

    const wchar_t *name = wcsrchr(path, L'\\');
    return IstarothFindBuild((const BYTE *)GetModuleHandleW(NULL), name ? name + 1 : path);
}

static inline BOOL IstarothValidateChecks(const BYTE *image, const IstarothGameBuild *build,
                                          const IstarothCodeCheck *checks, SIZE_T count) {
    if (!image || !build || !checks || !count) return FALSE;

    __try {
        for (SIZE_T index = 0; index < count; ++index) {
            const IstarothCodeCheck *check = &checks[index];
            if (!check->size || check->size > sizeof(check->bytes) ||
                check->rva >= build->image_size || check->size > build->image_size - check->rva ||
                memcmp(image + check->rva, check->bytes, check->size) != 0) {
                return FALSE;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
    return TRUE;
}
