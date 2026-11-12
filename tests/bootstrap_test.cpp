// Compile the real loader against owned handles and dummy, hash-pinned files.
// No game process is created, opened, terminated, or loaded by this test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <cstdio>
#include <atomic>
static DWORD stub_pid=0;
static HANDLE stub_event=nullptr,runtime_map=nullptr;
static unsigned generation=0,enumeration=0,loads=0,foreign_access=0;
static std::wstring fixture_path,mode;
static std::atomic<bool> live_exited=false;
static bool close_sent=false;
static LPWSTR FakeCommandLine();
static HANDLE WINAPI FakeSnapshot(DWORD,DWORD);
static BOOL WINAPI FakeFirst(HANDLE,PROCESSENTRY32W*);
static BOOL WINAPI FakeNext(HANDLE,PROCESSENTRY32W*);
static HANDLE WINAPI FakeOpen(DWORD,BOOL,DWORD);
static DWORD WINAPI FakeWait(HANDLE,DWORD);
static BOOL WINAPI FakeExit(HANDLE,LPDWORD);
static BOOL WINAPI FakeTimes(HANDLE,LPFILETIME,LPFILETIME,LPFILETIME,LPFILETIME);
static BOOL WINAPI FakePath(HANDLE,DWORD,LPWSTR,PDWORD);
static BOOL WINAPI FakeRead(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static BOOL WINAPI FakeModules(HANDLE,HMODULE*,DWORD,LPDWORD,DWORD);
static HANDLE WINAPI FakeRemote(HANDLE,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD);
static BOOL WINAPI DenyCreate(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
static BOOL WINAPI DenyTerminate(HANDLE,UINT);
static int WINAPI Dismiss(HWND,LPCWSTR,LPCWSTR,UINT);
#define GetCommandLineW FakeCommandLine
#define CreateToolhelp32Snapshot FakeSnapshot
#define Process32FirstW FakeFirst
#define Process32NextW FakeNext
#define OpenProcess FakeOpen
#define WaitForSingleObject FakeWait
#define GetExitCodeProcess FakeExit
#define GetProcessTimes FakeTimes
#define QueryFullProcessImageNameW FakePath
#define ReadFile FakeRead
#undef EnumProcessModulesEx
#define EnumProcessModulesEx FakeModules
#define CreateRemoteThread FakeRemote
#define CreateProcessW DenyCreate
#define TerminateProcess DenyTerminate
#define MessageBoxW Dismiss
#define wWinMain LoaderEntryUnderTest
#ifdef BASELINE
#include "istaroth_injector.before.cpp"
#else
#include "istaroth_injector.cpp"
#endif
#undef GetCommandLineW
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef OpenProcess
#undef WaitForSingleObject
#undef GetExitCodeProcess
#undef GetProcessTimes
#undef QueryFullProcessImageNameW
#undef ReadFile
#undef EnumProcessModulesEx
#undef CreateRemoteThread
#undef CreateProcessW
#undef TerminateProcess
#undef MessageBoxW
static LPWSTR FakeCommandLine(){static wchar_t value[]=L"OwnedBootstrap.exe";return value;}
static HANDLE WINAPI FakeSnapshot(DWORD,DWORD){return CreateEventW(nullptr,TRUE,FALSE,nullptr);}
static BOOL WINAPI FakeFirst(HANDLE,PROCESSENTRY32W* entry){enumeration=0;wcscpy_s(entry->szExeFile,L"other.exe");return TRUE;}
static DWORD WINAPI CancelLater(void*){Sleep(70);PostMessageW(progress,WM_CLOSE,0,0);return 0;}
static BOOL WINAPI FakeNext(HANDLE,PROCESSENTRY32W* entry) {
    if(mode==L"dead-listed"&&enumeration<2) {
        wcscpy_s(entry->szExeFile,TargetName);entry->th32ProcessID=enumeration++?GetCurrentProcessId():stub_pid;return TRUE;
    }
    if(enumeration++||mode==L"cancel"&&generation) {
        if(mode==L"cancel"&&generation&&!close_sent){close_sent=true;HANDLE thread=CreateThread(nullptr,0,CancelLater,nullptr,0,nullptr);CloseHandle(thread);}
        SetLastError(ERROR_NO_MORE_FILES);return FALSE;
    }
    wcscpy_s(entry->szExeFile,TargetName);
    entry->th32ProcessID=generation||mode==L"loaded"||mode==L"module-absent"?GetCurrentProcessId():stub_pid;return TRUE;
}
static HANDLE WINAPI FakeOpen(DWORD access,BOOL inherit,DWORD pid) {
    if(pid==stub_pid){HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),stub_event,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS);return copy;}
    if(pid==GetCurrentProcessId())return OpenProcess(access,inherit,pid);
    foreign_access++;SetLastError(ERROR_ACCESS_DENIED);return nullptr;
}
static bool IsSelf(HANDLE process){return GetProcessId(process)==GetCurrentProcessId();}
static DWORD WINAPI FakeWait(HANDLE handle,DWORD ms){if(IsSelf(handle)&&live_exited)return WAIT_OBJECT_0;return WaitForSingleObject(handle,ms);}
static BOOL WINAPI FakeExit(HANDLE process,LPDWORD code) {
    if(!IsSelf(process)){*code=mode==L"crash"?0xc0000005:0xc000042c;generation=1;return TRUE;}
    if(live_exited){*code=0xc000042c;return TRUE;}return GetExitCodeProcess(process,code);
}
static BOOL WINAPI FakeTimes(HANDLE,LPFILETIME a,LPFILETIME b,LPFILETIME c,LPFILETIME d){return GetProcessTimes(GetCurrentProcess(),a,b,c,d);}
static BOOL WINAPI FakePath(HANDLE,DWORD,LPWSTR path,PDWORD size){wcscpy_s(path,*size,fixture_path.c_str());*size=(DWORD)fixture_path.size();return TRUE;}
static BOOL WINAPI FakeRead(HANDLE file,LPVOID data,DWORD size,LPDWORD count,LPOVERLAPPED overlapped) {
    BOOL result=ReadFile(file,data,size,count,overlapped);
    wchar_t path[32768];DWORD length=GetFinalPathNameByHandleW(file,path,32768,FILE_NAME_NORMALIZED);
    if(length&&wcsstr(path,TargetName)) {
        if(!generation&&mode!=L"loaded"&&mode!=L"module-absent")SetEvent(stub_event);
        if(generation&&mode==L"repeat")live_exited=true;
    }
    return result;
}
static BOOL WINAPI FakeModules(HANDLE process,HMODULE* modules,DWORD size,LPDWORD needed,DWORD flags) {
    if(!IsSelf(process)){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    if(mode==L"module-absent"){SetLastError(ERROR_PARTIAL_COPY);return FALSE;}
    return K32EnumProcessModulesEx(process,modules,size,needed,flags);
}
static DWORD WINAPI OwnedLoad(void*) {
    if(mode==L"loaded"){live_exited=true;return 0;}
    wchar_t name[100];swprintf_s(name,L"Local\\Istaroth.Runtime.%lu",game_pid);
    runtime_map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RuntimeView),name);
    auto view=(RuntimeView*)MapViewOfFile(runtime_map,FILE_MAP_ALL_ACCESS,0,0,sizeof(RuntimeView));
    RuntimeView sample{};sample.magic=RuntimeMagic;sample.version=RuntimeVersion;sample.size=sizeof(sample);sample.pid=game_pid;sample.birth=game_birth;
    sample.sequence=2;sample.status=2;sample.flags=Initialized|StartedPaused;sample.heartbeat=sample.present_tick=GetTickCount64();
    memcpy(view,&sample,sizeof(sample));UnmapViewOfFile(view);return 0;
}
static HANDLE WINAPI FakeRemote(HANDLE process,LPSECURITY_ATTRIBUTES a,SIZE_T stack,LPTHREAD_START_ROUTINE,LPVOID memory,DWORD flags,LPDWORD tid) {
    if(!IsSelf(process)){foreign_access++;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    loads++;return CreateThread(a,stack,OwnedLoad,memory,flags,tid);
}
static BOOL WINAPI DenyCreate(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){foreign_access++;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI DenyTerminate(HANDLE,UINT){foreign_access++;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static int WINAPI Dismiss(HWND,LPCWSTR,LPCWSTR,UINT){return IDOK;}
int wmain(int argc,wchar_t** argv) {
    mode=argc>1?argv[1]:L"handover";stub_pid=GetCurrentProcessId()|0x40000000u;
    stub_event=CreateEventW(nullptr,TRUE,mode==L"dead-listed",nullptr);wchar_t own[32768];GetModuleFileNameW(nullptr,own,32768);
    std::wstring folder=own;folder.resize(folder.find_last_of(L'\\')+1);fixture_path=folder+TargetName;
    DeleteFileW((folder+L"Istaroth.ini").c_str());
    DWORD result=(DWORD)LoaderEntryUnderTest(GetModuleHandleW(nullptr),nullptr,nullptr,SW_HIDE);
    bool pass=foreign_access==0;
#ifdef BASELINE
    pass=pass&&result==ERROR_MOD_NOT_FOUND&&!load_started&&loads==0&&child_exit==0xc000042c;
#else
    if(mode==L"handover")pass=pass&&result==0&&loads==1&&runtime_ready&&handover_count==1&&handover_pid==stub_pid&&handover_exit==0xc000042c&&ConfiguredGamePath()==fixture_path;
    else if(mode==L"dead-listed")pass=pass&&result==0&&loads==1&&runtime_ready&&!handover_count&&ConfiguredGamePath()==fixture_path;
    else if(mode==L"loaded")pass=pass&&result==ERROR_PROCESS_ABORTED&&loads==1&&load_started&&!handover_count;
    else if(mode==L"crash")pass=pass&&result==ERROR_PROCESS_ABORTED&&loads==0&&!handover_count;
    else if(mode==L"repeat")pass=pass&&result==ERROR_ELEVATION_REQUIRED&&loads==0&&handover_count==1;
    else if(mode==L"cancel")pass=pass&&result==ERROR_CANCELLED&&loads==0&&handover_count==1;
    else if(mode==L"module-absent")pass=pass&&result==ERROR_MOD_NOT_FOUND&&loads==0&&!handover_count;
#endif
    if(runtime_map)CloseHandle(runtime_map);CloseHandle(stub_event);
    wprintf(L"{\"passed\":%s,\"mode\":\"%s\",\"error\":%lu,\"loads\":%u,\"foreign_access\":%u,\"runtime_ack\":%s}\n",pass?L"true":L"false",mode.c_str(),result,loads,foreign_access,runtime_ready?L"true":L"false");
    return pass?0:1;
}
