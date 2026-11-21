#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <thread>
#include <exception>
#include "resident_protocol.h"
#include "resident_payloads.h"
#include "injector_ui.h"
#include "diagnostics.h"
#include "diagnostic_report.h"
#include "file_integrity.h"
using namespace istaroth;

static std::wstring directory;
static InjectorUiState visual;
static InjectorUiRenderer renderer;
static HWND progress=nullptr;
static bool quiet=false;
static bool runtime_ready=false;
static bool cancel_requested=false,load_started=false,path_discovered=false;
static DWORD game_pid=0;
static uint64_t game_birth=0;
static DWORD child_exit=STILL_ACTIVE;
static constexpr DWORD ElevationExit=0xc000042c;
static unsigned handover_count=0;
static DWORD handover_pid=0,handover_exit=STILL_ACTIVE;
static const char* operation="starting";
static void Trace(const char* event,DWORD error=0) {
    char line[512];sprintf_s(line,"phase=%s event=%s game_pid=%lu birth=%llu error=%lu child_exit=0x%08lx load_started=%u handovers=%u",
        operation,event,game_pid,(unsigned long long)game_birth,error,child_exit,load_started?1u:0u,handover_count);
    diagnostics::Append(directory,L"Istaroth-injector.log",line);
}
static void RequireAlive(HANDLE process) {
    DWORD state=WaitForSingleObject(process,0);
    if(state==WAIT_FAILED)throw GetLastError();
    if(state==WAIT_OBJECT_0) {
        GetExitCodeProcess(process,&child_exit);
        throw (DWORD)(child_exit==ElevationExit?ERROR_ELEVATION_REQUIRED:ERROR_PROCESS_ABORTED);
    }
}
static void Pump() {MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static void CheckCancelled(){Pump();if(cancel_requested)throw (DWORD)ERROR_CANCELLED;}
static void RefreshProgress() {
    if (!progress) return;
    InvalidateRect(progress, nullptr, FALSE);
    UpdateWindow(progress);
}

static void Progress(const wchar_t* text, InjectorStage stage) {
    CheckCancelled();
    visual.stage = stage;
    visual.main_status = text;
    RefreshProgress();
    CheckCancelled();
}

static LRESULT CALLBACK Window(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CLOSE:
        cancel_requested = true;
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DPICHANGED: {
        auto rect = reinterpret_cast<const RECT*>(l);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        renderer.Reset();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        visual.load_started = load_started;
        renderer.Paint(dc, client, visual);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        renderer.Reset();
        return 0;
    }
    return DefWindowProcW(window, msg, w, l);
}
// Only the main thread dispatches window messages or changes the launch state.
template<class Work>
static void VerifyInBackground(Work work) {
    CheckCancelled();
    std::atomic_bool stop{false};
    std::exception_ptr failure;
    std::thread worker([&] {
        try { work(stop); }
        catch (...) { failure = std::current_exception(); }
    });
    HANDLE thread = worker.native_handle();
    DWORD wait_error = ERROR_SUCCESS;
    for (;;) {
        DWORD result = MsgWaitForMultipleObjectsEx(1, &thread, stop ? 50 : INFINITE,
                                                   QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_FAILED) {
            wait_error = GetLastError();
            stop = true;
            CancelSynchronousIo(thread);
            break;
        }
        Pump();
        if (cancel_requested) {
            stop = true;
            // Retry if cancellation raced with the worker about to enter a blocking read.
            CancelSynchronousIo(thread);
        }
    }
    worker.join();
    CheckCancelled();
    if (wait_error) throw wait_error;
    if (failure) std::rethrow_exception(failure);
}

static bool GamePath(const std::wstring& input,std::wstring& full) {
    if(input.empty())return false;
    std::wstring path=input;std::replace(path.begin(),path.end(),L'/',L'\\');
    wchar_t buffer[32768];DWORD length=GetFullPathNameW(path.c_str(),32768,buffer,nullptr);
    if(!length||length>=32768)return false;
    const wchar_t* base=wcsrchr(buffer,L'\\');base=base?base+1:buffer;
    DWORD attributes=GetFileAttributesW(buffer);
    if(_wcsicmp(base,TargetName)||attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    full=buffer;return true;
}
[[maybe_unused]] static std::wstring ConfiguredGamePath() {
    wchar_t configured[32768];
    GetPrivateProfileStringW(L"Istaroth",L"GamePath",L"",configured,32768,(directory+L"Istaroth.ini").c_str());
    std::wstring game;GamePath(configured,game);return game;
}
static void SaveGamePath(const std::wstring& game) {
    CheckCancelled();
    if(!path_discovered)return;
    auto ini=directory+L"Istaroth.ini";
    // A missing INI must also preserve paths outside the system ANSI code page.
    HANDLE file=CreateFileW(ini.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        WORD bom=0xfeff;DWORD written=0;bool ok=WriteFile(file,&bom,sizeof(bom),&written,nullptr)&&written==sizeof(bom);
        CloseHandle(file);if(!ok)throw (DWORD)ERROR_WRITE_FAULT;
    } else if(GetLastError()!=ERROR_FILE_EXISTS)throw GetLastError();
    if(!WritePrivateProfileStringW(L"Istaroth",L"GamePath",game.c_str(),ini.c_str()))throw (DWORD)ERROR_WRITE_FAULT;
    wchar_t saved[32768];GetPrivateProfileStringW(L"Istaroth",L"GamePath",L"",saved,32768,ini.c_str());
    if(game!=saved)throw (DWORD)ERROR_NO_UNICODE_TRANSLATION;
}
static uint64_t Birth(HANDLE process) {
    FILETIME created,exit,kernel,user;uint64_t value=0;
    if(!GetProcessTimes(process,&created,&exit,&kernel,&user))throw GetLastError();memcpy(&value,&created,8);return value;
}
static std::wstring ProcessPath(HANDLE process) {
    wchar_t path[32768];DWORD size=32768;
    if(!QueryFullProcessImageNameW(process,0,path,&size)){DWORD error=GetLastError();RequireAlive(process);throw error;}return path;
}
static DWORD FindGamePid() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw GetLastError();
    PROCESSENTRY32W entry={sizeof(entry)};std::vector<DWORD> matches;
    BOOL found=Process32FirstW(snapshot,&entry);
    if(!found){DWORD error=GetLastError();CloseHandle(snapshot);throw error;}
    do {if(!_wcsicmp(entry.szExeFile,TargetName)) {
        HANDLE probe=OpenProcess(SYNCHRONIZE,FALSE,entry.th32ProcessID);bool exited=false;
        if(probe){exited=WaitForSingleObject(probe,0)==WAIT_OBJECT_0;CloseHandle(probe);}
        else exited=GetLastError()==ERROR_INVALID_PARAMETER;
        if(!exited)matches.push_back(entry.th32ProcessID);
    }}while(Process32NextW(snapshot,&entry));
    DWORD error=GetLastError();CloseHandle(snapshot);if(error!=ERROR_NO_MORE_FILES)throw error;
    if(matches.empty())return 0;if(matches.size()!=1)throw (DWORD)ERROR_DUP_NAME;return matches[0];
}
static HANDLE WaitForGame(std::wstring& game) {
    operation="waiting_game";Trace("waiting");
    Progress(handover_count ? L"正在等待《原神》启动" : L"请启动《原神》", InjectorStage::WaitingGame);
    for(;;) {
        CheckCancelled();DWORD pid=FindGamePid();
        if(pid) {
            DWORD access=PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE;
            HANDLE process=OpenProcess(access,FALSE,pid);
            if(!process){DWORD error=GetLastError();if(error!=ERROR_INVALID_PARAMETER)throw error;}
            else {
                try {
                    if(WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);Sleep(50);continue;}
                    if(!GamePath(ProcessPath(process),game))throw (DWORD)ERROR_INVALID_NAME;
                    game_birth=Birth(process);game_pid=pid;path_discovered=true;CheckCancelled();Trace("candidate_detected");return process;
                }catch(...){CloseHandle(process);throw;}
            }
        }
        Sleep(50);
    }
}
static HANDLE ClaimGame() {
    wchar_t name[100];swprintf_s(name,L"Local\\Istaroth.Injecting.%lu",game_pid);
    HANDLE guard=CreateMutexW(nullptr,FALSE,name);if(!guard)throw GetLastError();
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(guard);throw (DWORD)ERROR_ALREADY_EXISTS;}
    swprintf_s(name,L"Local\\Istaroth.Runtime.%lu",game_pid);
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
    if(mapping){CloseHandle(mapping);CloseHandle(guard);throw (DWORD)ERROR_ALREADY_EXISTS;}
    DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND){CloseHandle(guard);throw error;}
    return guard;
}
static void Existing(const std::wstring& expected,bool restart) {
    CheckCancelled();DWORD pid=FindGamePid();if(!pid)return;
    HANDLE process=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)throw GetLastError();
    try {
        if(_wcsicmp(ProcessPath(process).c_str(),expected.c_str()))throw (DWORD)ERROR_INVALID_NAME;
        if(!restart) {
            if(quiet||MessageBoxW(progress,L"《原神》正在运行。现在关闭游戏并重新启动，以加载伊斯塔露吗？\n\n选择“否”或“取消”可继续当前游戏。",L"伊斯塔露加载器",MB_YESNOCANCEL|MB_DEFBUTTON2|MB_ICONINFORMATION)!=IDYES)
                throw (DWORD)ERROR_CANCELLED;
        }
        Progress(L"正在关闭当前游戏",InjectorStage::Terminating);
        CheckCancelled();
        if(!TerminateProcess(process,0))throw GetLastError();
        uint64_t end=GetTickCount64()+5000;DWORD stopped=WAIT_TIMEOUT;
        while(GetTickCount64()<end){stopped=WaitForSingleObject(process,20);if(stopped!=WAIT_TIMEOUT)break;CheckCancelled();}
        if(stopped!=WAIT_OBJECT_0)throw (DWORD)(stopped==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_GEN_FAILURE);
    } catch(...) {CloseHandle(process);throw;}
    CloseHandle(process);
}
static LPTHREAD_START_ROUTINE Loader(HANDLE process) {
    operation="locating_loader";CheckCancelled();RequireAlive(process);Trace("locating_system_loader");
    FARPROC function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE owner=nullptr;
    if(!function||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)function,&owner))throw GetLastError();
    wchar_t expected[32768];if(!GetModuleFileNameW(owner,expected,32768))throw GetLastError();
    uintptr_t rva=(uintptr_t)function-(uintptr_t)owner;
    auto dos=(IMAGE_DOS_HEADER*)owner;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||dos->e_lfanew>4096)throw (DWORD)ERROR_BAD_EXE_FORMAT;
    auto pe=(IMAGE_NT_HEADERS64*)((BYTE*)owner+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||rva>=pe->OptionalHeader.SizeOfImage)throw (DWORD)ERROR_BAD_EXE_FORMAT;
    uint64_t end=GetTickCount64()+3000;
    while(GetTickCount64()<end) {
        RequireAlive(process);
        HMODULE modules[1024];DWORD needed=0;
        if(!EnumProcessModulesEx(process,modules,sizeof(modules),&needed,LIST_MODULES_ALL)) {
            DWORD error=GetLastError();RequireAlive(process);if(error==ERROR_ACCESS_DENIED)throw error;
        } else if(needed<=sizeof(modules)) {
            LPTHREAD_START_ROUTINE entry=nullptr;
            for(DWORD i=0;i<needed/sizeof(HMODULE);i++) {
                wchar_t path[32768];if(!GetModuleFileNameExW(process,modules[i],path,32768))continue;
                const wchar_t* base=wcsrchr(path,L'\\');base=base?base+1:path;
                if(!_wcsicmp(base,ResidentPayload))throw (DWORD)ERROR_ALREADY_EXISTS;
                if(!_wcsicmp(path,expected))entry=(LPTHREAD_START_ROUTINE)((BYTE*)modules[i]+rva);
            }
            if(entry){RequireAlive(process);return entry;}
        }
        CheckCancelled();Sleep(20);
    }
    RequireAlive(process);throw (DWORD)ERROR_MOD_NOT_FOUND;
}
static void Load(HANDLE process, const std::wstring& path, ScopedHandle& pending_use) {
    CheckCancelled();
    auto entry=Loader(process);SIZE_T bytes=(path.size()+1)*sizeof(wchar_t),written=0;
    operation="preparing_load";RequireAlive(process);
    void* memory=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!memory){DWORD error=GetLastError();RequireAlive(process);throw error;}
    HANDLE thread=nullptr;bool completed=false;
    try {
        if(!WriteProcessMemory(process,memory,path.c_str(),bytes,&written)||written!=bytes){DWORD error=GetLastError();RequireAlive(process);throw error;}
        CheckCancelled();RequireAlive(process);
        thread=CreateRemoteThread(process,nullptr,0,entry,memory,0,nullptr);
        if(!thread){DWORD error=GetLastError();RequireAlive(process);throw error;}
        load_started=true;operation="loading_payload";Trace("load_started");
        uint64_t end=GetTickCount64()+10000;
        while(GetTickCount64()<end) {if(WaitForSingleObject(thread,20)==WAIT_OBJECT_0){completed=true;break;}Pump();}
        if(!completed)throw (DWORD)ERROR_TIMEOUT;
    } catch(...) {
        if(thread && !completed) pending_use.reset(thread);
        else {
            if(thread) CloseHandle(thread);
            VirtualFreeEx(process,memory,0,MEM_RELEASE);
        }
        throw;
    }
    CloseHandle(thread);VirtualFreeEx(process,memory,0,MEM_RELEASE);CheckCancelled();
}
static void AwaitReady(HANDLE process) {
    operation="awaiting_runtime";CheckCancelled();
    wchar_t name[100];swprintf_s(name,L"Local\\Istaroth.Runtime.%lu",game_pid);
    HANDLE mapping=nullptr;RuntimeView* view=nullptr;bool ready=false;DWORD error=ERROR_TIMEOUT;
    uint64_t end=GetTickCount64()+95000;
    while(GetTickCount64()<end) {
        Pump();if(cancel_requested){error=ERROR_CANCELLED;break;}
        if(WaitForSingleObject(process,0)==WAIT_OBJECT_0){error=ERROR_PROCESS_ABORTED;break;}
        if(!mapping)mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        if(mapping&&!view)view=(RuntimeView*)MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(RuntimeView));
        RuntimeView snapshot{};uint64_t now=GetTickCount64();
        if(ReadSnapshot(view,snapshot,offsetof(RuntimeView,sequence))&&snapshot.heartbeat) {
            if(snapshot.magic!=RuntimeMagic||snapshot.version!=RuntimeVersion||snapshot.size!=sizeof(RuntimeView)||snapshot.pid!=game_pid||snapshot.birth!=game_birth) {error=ERROR_REVISION_MISMATCH;break;}
            if(snapshot.status==3||snapshot.error){error=snapshot.error?snapshot.error:ERROR_DLL_INIT_FAILED;break;}
            if(snapshot.status==2&&(snapshot.flags&Initialized)&&(snapshot.flags&StartedPaused)&&Fresh(now,snapshot.heartbeat,1000)&&Fresh(now,snapshot.present_tick,1000)) {ready=true;break;}
        }
        Pump();Sleep(20);
    }
    if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);if(!ready)throw error;CheckCancelled();
}
static void Receipt(DWORD error) {
    auto path=directory+L"Istaroth-injector-result.json";HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;char text[900];int count=sprintf_s(text,"{\"version\":1,\"build_version\":\"%s\",\"source_revision\":\"%s\",\"source_dirty\":%s,\"pid\":%lu,\"birth\":%llu,\"error\":%lu,\"child_exit\":%lu,\"runtime_ack\":%s,\"injector_exits\":true,\"cancelled\":%s,\"load_started\":%s,\"phase\":\"%s\",\"handover_count\":%u,\"handover_pid\":%lu,\"handover_exit\":%lu}\n",ISTAROTH_BUILD_VERSION,ISTAROTH_SOURCE_REVISION,ISTAROTH_SOURCE_DIRTY?"true":"false",game_pid,(unsigned long long)game_birth,error,child_exit,runtime_ready?"true":"false",error==ERROR_CANCELLED?"true":"false",load_started?"true":"false",operation,handover_count,handover_pid,handover_exit);
    DWORD written=0;if(count>0)WriteFile(file,text,(DWORD)count,&written,nullptr);CloseHandle(file);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);bool restart=false,export_report=false;DWORD error=0;
    if(!args)return (int)GetLastError();
    for(int i=1;i<argc;i++){if(!wcscmp(args[i],L"--restart"))restart=true;else if(!wcscmp(args[i],L"--quiet"))quiet=true;else if(!wcscmp(args[i],L"--diagnostics"))export_report=true;else error=ERROR_INVALID_PARAMETER;}LocalFree(args);
    wchar_t own[32768];DWORD length=GetModuleFileNameW(nullptr,own,32768);if(!length||length>=32768)return ERROR_FILENAME_EXCED_RANGE;
    directory=own;directory.resize(directory.find_last_of(L"\\/")+1);
    if(export_report) {
        if(error||restart)return ERROR_INVALID_PARAMETER;
        std::wstring report;error=diagnostics::ExportReport(directory,TargetSha256,report);
        if(!quiet) {
            std::wstring text=error?L"无法保存诊断报告。错误代码 "+std::to_wstring(error):L"诊断报告已保存到以下位置：\n"+report+L"\n\n反馈问题时，请附上此报告，并说明出错时间和当时看到的现象。";
            MessageBoxW(nullptr,text.c_str(),L"伊斯塔露 · 诊断报告",MB_OK|(error?MB_ICONERROR:MB_ICONINFORMATION));
        }
        return (int)error;
    }
    if (!quiet) {
        visual = {};
        progress = CreateInjectorWindow(instance, Window);
        if (!progress) { error = GetLastError(); Receipt(error); return (int)error; }
        ShowWindow(progress, SW_SHOW);
        UpdateWindow(progress);
    }
    bool digest_mismatch = false;
    std::vector<ScopedHandle> verified_files;
    ScopedHandle pending_use;
    HANDLE process=nullptr,injection_guard=nullptr;
    try {
        if(error)throw error;
        std::wstring game;
#ifdef ISTAROTH_FIXTURE
        game=directory+TargetName;
#else
        game=ConfiguredGamePath();if(game.empty()&&quiet)throw (DWORD)ERROR_PATH_NOT_FOUND;
#endif
        operation="validating_payloads";Trace("started");
        FileHasher verifier;
        std::wstring verified_resident_path;
        VerifyInBackground([&](const std::atomic_bool& stop) {
            for (const auto& payload : Payloads) {
                auto file = verifier.Verify(directory + payload.name, payload.sha256, stop);
                if (!wcscmp(payload.name, ResidentPayload)) verified_resident_path = FinalFilePath(file.get());
                verified_files.push_back(std::move(file));
            }
        });
        if (verified_resident_path.empty()) throw (DWORD)ERROR_INVALID_DATA;
        const bool first_run=game.empty();
        for(;;) {try {
        if(game.empty())process=WaitForGame(game);
        operation="validating_game";
        Progress(L"正在检查《原神》的版本", InjectorStage::CheckingGame);
        const wchar_t* base=wcsrchr(game.c_str(),L'\\');base=base?base+1:game.c_str();
        if (_wcsicmp(base, TargetName)) throw (DWORD)ERROR_REVISION_MISMATCH;
        VerifyInBackground([&](const std::atomic_bool& stop) {
            auto file = OpenReadOnly(game);
            if (verifier.Sha256(file.get(), stop) != TargetSha256) throw (DWORD)ERROR_REVISION_MISMATCH;
        });
        if(!process) {
            operation="starting_game";
            Existing(game,restart);Progress(L"正在启动《原神》",InjectorStage::Starting);
            STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION info={};std::wstring cwd=game.substr(0,game.find_last_of(L'\\'));
            CheckCancelled();
            if(!CreateProcessW(game.c_str(),nullptr,nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&startup,&info))throw GetLastError();
            process=info.hProcess;CloseHandle(info.hThread);game_pid=info.dwProcessId;game_birth=Birth(process);
        } else Progress(L"已找到《原神》，正在加载伊斯塔露",InjectorStage::Starting);
        RequireAlive(process);if(_wcsicmp(ProcessPath(process).c_str(),game.c_str()))throw (DWORD)ERROR_INVALID_NAME;
        RequireAlive(process);operation="claiming_process";injection_guard=ClaimGame();Load(process,verified_resident_path,pending_use);Progress(L"正在等待游戏内菜单",InjectorStage::Waiting);AwaitReady(process);runtime_ready=true;operation="saving_path";SaveGamePath(game);operation="ready";break;
        }catch(DWORD value) {
            if(value==ERROR_ELEVATION_REQUIRED&&first_run&&!load_started&&!handover_count&&process&&
               WaitForSingleObject(process,0)==WAIT_OBJECT_0&&GetExitCodeProcess(process,&child_exit)&&child_exit==ElevationExit) {
                handover_count=1;handover_pid=game_pid;handover_exit=child_exit;Trace("elevation_handoff",value);
                if(injection_guard){CloseHandle(injection_guard);injection_guard=nullptr;}
                CloseHandle(process);process=nullptr;game.clear();game_pid=0;game_birth=0;child_exit=STILL_ACTIVE;path_discovered=false;
                continue;
            }
            throw;
        }}
    } catch (const DigestMismatch&) {digest_mismatch=true;error=ERROR_CRC;}
    catch(DWORD value) {error=value;}catch(...) {error=ERROR_UNHANDLED_EXCEPTION;}
    if(!pending_use) verified_files.clear();
    if(cancel_requested)error=ERROR_CANCELLED;
    if(!error&&!runtime_ready)error=ERROR_DLL_INIT_FAILED;
    Trace(error?"failed":"ready",error);
    if(injection_guard)CloseHandle(injection_guard);
    if(process){GetExitCodeProcess(process,&child_exit);CloseHandle(process);}Receipt(error);
    if(error&&!quiet&&error!=ERROR_CANCELLED) {
        const wchar_t* hint = L"请打开加载器所在文件夹，查看 Istaroth-injector.log 中的错误记录。";
        if (pending_use)
            hint = L"等待组件加载超时。关闭提示后，加载器会在后台保护已校验的文件，直到本次加载结束。退出游戏后即可重试。";
        else if (runtime_ready && !strcmp(operation, "saving_path"))
            hint = L"游戏内功能已加载，但游戏路径未能保存到 Istaroth.ini。请检查加载器所在文件夹是否可写。";
        else if (!strcmp(operation, "validating_payloads") && (error == ERROR_FILE_NOT_FOUND || digest_mismatch))
            hint = L"伊斯塔露的文件缺失或未通过校验。请将同一版本的压缩包完整解压到一个文件夹，再打开加载器。";
        else if (error == ERROR_REVISION_MISMATCH)
            hint = !strcmp(operation, "validating_game")
                ? L"当前版本的伊斯塔露不支持这个游戏版本。请使用与游戏版本匹配的伊斯塔露。"
                : L"加载器未能确认游戏内组件的版本或运行状态。请退出游戏，确认加载器和组件来自同一版本后重试。";
        else if (error == ERROR_ELEVATION_REQUIRED)
            hint = L"游戏启动时需要 Windows 权限确认。请完成系统提示的操作后重试。";
        else if (error == ERROR_PROCESS_ABORTED)
            hint = L"《原神》在加载完成前已退出。请查看游戏是否能正常启动。";
        else if (error == ERROR_ALREADY_EXISTS)
            hint = L"伊斯塔露已在运行或正在加载。请回到游戏，或等待先打开的加载器完成。";
        else if (error == ERROR_ACCESS_DENIED)
            hint = L"Windows 拒绝了本次操作。请查看加载日志，确认哪一步未能完成。";
        visual.stage = InjectorStage::Failed;
        visual.main_status = runtime_ready ? L"未能保存游戏路径" : L"本次加载未完成";
        visual.detail_hint = hint;
        visual.error_code = error;
        RefreshProgress();
        Pump();
        wchar_t text[600];
        swprintf_s(text, L"%s（错误代码 %lu）\n\n%s", visual.main_status.c_str(), error, hint);
        if(!cancel_requested)MessageBoxW(progress,text,L"伊斯塔露加载器",MB_OK|MB_ICONERROR);
    }
    if (progress) {
        DestroyWindow(progress);
        progress = nullptr;
    }
    // A timeout ends the UI wait. Keep the verified files pinned while the
    // outstanding request can still open the primary DLL by name.
    if (pending_use) WaitForSingleObject(pending_use.get(), INFINITE);
    return (int)error;
}
