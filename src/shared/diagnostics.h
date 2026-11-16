#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include "build_identity.h"

namespace istaroth::diagnostics {
inline constexpr DWORD LogLimit=512*1024;
inline SRWLOCK log_lock=SRWLOCK_INIT;
struct PreserveError {
    DWORD value=GetLastError();
    ~PreserveError(){SetLastError(value);}
};
struct LogLock {
    LogLock(){AcquireSRWLockExclusive(&log_lock);}
    ~LogLock(){ReleaseSRWLockExclusive(&log_lock);}
};

// Logging is best effort; it must not change the caller's Win32 error or throw.
inline bool Append(const std::wstring& directory,const wchar_t* name,const char* event) noexcept {
    PreserveError preserve;
    try {
        LogLock lock;
        std::wstring path=directory+name;
        HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};
        if(GetFileSizeEx(file,&size)&&size.QuadPart>=LogLimit) {
            CloseHandle(file);
            std::wstring previous=path+L".1";
            if(!MoveFileExW(path.c_str(),previous.c_str(),MOVEFILE_REPLACE_EXISTING))return false;
            file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)return false;
        }
        SYSTEMTIME time{};GetSystemTime(&time);
        char line[2048];_snprintf_s(line,sizeof(line),_TRUNCATE,
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ tick=%llu pid=%lu tid=%lu version=%s source=%s dirty=%u %s\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            (unsigned long long)GetTickCount64(),GetCurrentProcessId(),GetCurrentThreadId(),
            ISTAROTH_BUILD_VERSION,ISTAROTH_SOURCE_REVISION,ISTAROTH_SOURCE_DIRTY,event?event:"");
        DWORD bytes=(DWORD)strlen(line),written=0;
        bool ok=WriteFile(file,line,bytes,&written,nullptr)&&written==bytes;CloseHandle(file);return ok;
    }catch(...){return false;}
}
}
