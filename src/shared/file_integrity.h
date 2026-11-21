#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <memory>
#include <string>

namespace istaroth {

struct CloseKernelHandle {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using ScopedHandle = std::unique_ptr<void, CloseKernelHandle>;

inline ScopedHandle OpenReadOnly(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw GetLastError();
    return ScopedHandle(file);
}

// Use the resolved path of the opened object so a later change to a directory
// junction does not redirect a path-based consumer to a different installation.
inline std::wstring FinalFilePath(HANDLE file) {
    std::wstring path(32768, L'\0');
    DWORD count = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED);
    if (!count) throw GetLastError();
    if (count >= path.size()) throw (DWORD)ERROR_FILENAME_EXCED_RANGE;
    path.resize(count);
    return path;
}

struct DigestMismatch {};

class FileHasher {
public:
    ScopedHandle Verify(const std::wstring& path, const char* expected, const std::atomic_bool& stop) {
        if (stop) throw (DWORD)ERROR_CANCELLED;
        auto file = OpenReadOnly(path);
        if (Sha256(file.get(), stop) != expected) throw DigestMismatch{};
        return file;
    }

    std::string Sha256(HANDLE file, const std::atomic_bool& stop) {
        BCRYPT_HASH_HANDLE hash = nullptr;
        auto status = BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0);
        if (status < 0) throw static_cast<DWORD>(status);
        const std::unique_ptr<void, decltype(&BCryptDestroyHash)> owner(hash, BCryptDestroyHash);
        BYTE digest[32];
        for (;;) {
            if (stop) throw (DWORD)ERROR_CANCELLED;
            DWORD count = 0;
            if (!ReadFile(file, buffer.get(), chunk_size, &count, nullptr)) throw GetLastError();
            if (!count) break;
            status = BCryptHashData(hash, buffer.get(), count, 0);
            if (status < 0) throw static_cast<DWORD>(status);
        }
        status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
        if (status < 0) throw static_cast<DWORD>(status);
        constexpr char digits[] = "0123456789abcdef";
        std::string result(sizeof(digest) * 2, '0');
        for (size_t i = 0; i < sizeof(digest); ++i) {
            result[2 * i] = digits[digest[i] >> 4];
            result[2 * i + 1] = digits[digest[i] & 15];
        }
        return result;
    }

private:
    static constexpr DWORD chunk_size = 1024 * 1024;
    std::unique_ptr<BYTE[]> buffer{new BYTE[chunk_size]};
};

} // namespace istaroth
