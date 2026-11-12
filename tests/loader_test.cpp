// Actual loader code with process discovery redirected to this test process.
// Game processes cannot be opened, created, terminated, or loaded by this test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <cstdio>
static unsigned checks=0,process_starts=0,terminations=0,thread_starts=0;
static std::vector<DWORD> targets;
static size_t target_index=0;
static DWORD open_error=0,main_thread=0;
static std::wstring observed_path,command_line=L"OwnedLoader.exe";
static HWND close_window=nullptr;
static bool cancel_read=false,thread_saw_memory=false,entry_title_ok=false;
static unsigned reads=0;
static void* thread_memory=nullptr;
static LPWSTR FakeCommandLine(){return command_line.data();}
static HANDLE WINAPI FakeSnapshot(DWORD,DWORD){return CreateEventW(nullptr,TRUE,FALSE,nullptr);}
static BOOL WINAPI FakeFirst(HANDLE,PROCESSENTRY32W* entry){target_index=0;wcscpy_s(entry->szExeFile,L"other.exe");return TRUE;}
static BOOL WINAPI FakeNext(HANDLE,PROCESSENTRY32W* entry) {
    if(target_index==targets.size()){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
    wcscpy_s(entry->szExeFile,L"YuanShen.exe");entry->th32ProcessID=targets[target_index++];return TRUE;
}
static HANDLE WINAPI FakeOpenProcess(DWORD access,BOOL inherit,DWORD pid) {
    if(open_error){SetLastError(open_error);return nullptr;}
    if(pid!=GetCurrentProcessId()){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return OpenProcess(access,inherit,pid);
}
static BOOL WINAPI FakeProcessPath(HANDLE process,DWORD flags,LPWSTR buffer,PDWORD size) {
    if(!observed_path.empty()) {
        if(observed_path.size()>=*size){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
        wcscpy_s(buffer,*size,observed_path.c_str());*size=(DWORD)observed_path.size();return TRUE;
    }
    return QueryFullProcessImageNameW(process,flags,buffer,size);
}
static int WINAPI FakeMessageBox(HWND window,LPCWSTR,LPCWSTR,UINT) {
    wchar_t title[80]={};GetWindowTextW(window,title,80);entry_title_ok=!wcscmp(title,L"伊斯塔露加载器");return IDOK;
}
static BOOL WINAPI FakeReadFile(HANDLE file,LPVOID buffer,DWORD size,LPDWORD read,LPOVERLAPPED overlapped) {
    BOOL result=ReadFile(file,buffer,size,read,overlapped);
    if(cancel_read&&++reads==2)PostMessageW(close_window,WM_CLOSE,0,0);return result;
}
static DWORD WINAPI OwnedThread(void* memory) {
    Sleep(100);MEMORY_BASIC_INFORMATION info{};thread_saw_memory=VirtualQuery(memory,&info,sizeof(info))&&info.State==MEM_COMMIT;return 0;
}
static DWORD WINAPI CloseLater(void*){Sleep(70);PostMessageW(close_window,WM_CLOSE,0,0);return 0;}
static BOOL CALLBACK CloseEntryWindow(HWND window,LPARAM) {
    wchar_t name[80]={};GetClassNameW(window,name,80);
    if(!wcscmp(name,L"Istaroth.Injector")){PostMessageW(window,WM_CLOSE,0,0);return FALSE;}return TRUE;
}
static DWORD WINAPI CloseEntryLater(void*){Sleep(200);EnumThreadWindows(main_thread,CloseEntryWindow,0);return 0;}
static HANDLE WINAPI FakeRemoteThread(HANDLE process,LPSECURITY_ATTRIBUTES attributes,SIZE_T stack,LPTHREAD_START_ROUTINE,LPVOID memory,DWORD flags,LPDWORD tid) {
    if(GetProcessId(process)!=GetCurrentProcessId()){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    thread_starts++;thread_memory=memory;PostMessageW(close_window,WM_CLOSE,0,0);return CreateThread(attributes,stack,OwnedThread,memory,flags,tid);
}
static BOOL WINAPI FakeCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION) {
    process_starts++;SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
static BOOL WINAPI FakeTerminate(HANDLE,UINT){terminations++;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
#define GetCommandLineW FakeCommandLine
#define CreateToolhelp32Snapshot FakeSnapshot
#define Process32FirstW FakeFirst
#define Process32NextW FakeNext
#define OpenProcess FakeOpenProcess
#define QueryFullProcessImageNameW FakeProcessPath
#define MessageBoxW FakeMessageBox
#define ReadFile FakeReadFile
#define CreateRemoteThread FakeRemoteThread
#define CreateProcessW FakeCreateProcess
#define TerminateProcess FakeTerminate
#define wWinMain LoaderEntryNotRun
#include "istaroth_injector.cpp"
static void Check(bool value){checks++;if(!value){fprintf(stderr,"loader check %u failed\n",checks);exit(1);}}
template<class Action>static void Expect(DWORD code,Action action) {
    DWORD actual=0;try{action();}catch(DWORD value){actual=value;}Check(actual==code);
}
static void Write(const std::wstring& path,const std::string& body) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;Check(WriteFile(file,body.data(),(DWORD)body.size(),&written,nullptr)&&written==body.size());CloseHandle(file);
}
static void Reset(){cancel_requested=false;path_discovered=false;load_started=false;quiet=false;targets.clear();open_error=0;observed_path.clear();}
template<class Action>static void CancelSoon(Action action) {
    HANDLE closer=CreateThread(nullptr,0,CloseLater,nullptr,0,nullptr);Check(closer!=nullptr);Expect(ERROR_CANCELLED,action);
    WaitForSingleObject(closer,1000);CloseHandle(closer);
}
int wmain() {
    main_thread=GetCurrentThreadId();wchar_t own[32768];GetModuleFileNameW(nullptr,own,32768);
    directory=own;directory.resize(directory.find_last_of(L'\\')+1);std::wstring entry_dir=directory;directory+=L"cases\\";
    Check(CreateDirectoryW(directory.c_str(),nullptr)||GetLastError()==ERROR_ALREADY_EXISTS);
    std::wstring ini=directory+L"Istaroth.ini",game=directory+TargetName,wrong=directory+L"launcher.exe";
    Write(game,std::string(8*1024*1024,'x'));Write(wrong,"not a game");
    WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"Istaroth.LoaderChecks";RegisterClassW(&wc);
    progress=CreateWindowW(wc.lpszClassName,L"Owned loader check",WindowStyle,0,0,650,200,nullptr,nullptr,wc.hInstance,nullptr);close_window=progress;
    Check(progress!=nullptr);Check((GetWindowLongPtrW(progress,GWL_STYLE)&WS_SYSMENU)!=0);
    for(int size:{16,24,32,48,64,128,256}) {
        HICON icon=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,0);Check(icon!=nullptr);
        ICONINFO info{};Check(GetIconInfo(icon,&info)!=FALSE);BITMAP color{};GetObjectW(info.hbmColor,sizeof(color),&color);Check(color.bmWidth==size&&color.bmHeight==size);
        if(info.hbmColor)DeleteObject(info.hbmColor);if(info.hbmMask)DeleteObject(info.hbmMask);DestroyIcon(icon);
    }
    Check((GetMenuState(GetSystemMenu(progress,FALSE),SC_CLOSE,MF_BYCOMMAND)&MF_DISABLED)==0);
    RECT suggested={10,10,970,400},actual{};SendMessageW(progress,WM_DPICHANGED,MAKELONG(144,144),(LPARAM)&suggested);GetWindowRect(progress,&actual);Check(EqualRect(&suggested,&actual)!=FALSE);
    SendMessageW(progress,WM_SYSCOMMAND,SC_CLOSE,0);Check(cancel_requested);Check(IsWindow(progress)&&!IsWindowVisible(progress));
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth=640;bitmap.bmiHeader.biHeight=-220;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HBITMAP image=CreateDIBSection(nullptr,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0);Check(dc&&image&&pixels);
    auto old=SelectObject(dc,image);RECT client={0,0,640,220};InjectorUiState paint_state;paint_state.main_status=L"首次使用，请手动启动原神。正在等待游戏启动。";
    RenderInjectorUi(dc,client,paint_state);DWORD objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(unsigned i=0;i<40;i++)RenderInjectorUi(dc,client,paint_state);Check(objects==GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
    SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);
    Reset();std::wstring full;Check(!GamePath(L"",full));Check(!GamePath(wrong,full));Check(!GamePath(directory,full));Check(GamePath(game,full)&&full==game);
    DeleteFileW(ini.c_str());Check(ConfiguredGamePath().empty());Write(ini,"[Istaroth]\r\nGamePath=\r\n");Check(ConfiguredGamePath().empty());
    Reset();Check(FindGamePid()==0);std::wstring detected;CancelSoon([&]{WaitForGame(detected);});Check(!path_discovered&&game_pid==0);
    Reset();targets={GetCurrentProcessId(),12345};Expect(ERROR_DUP_NAME,[]{FindGamePid();});
    Reset();targets={GetCurrentProcessId()};open_error=ERROR_ACCESS_DENIED;Expect(ERROR_ACCESS_DENIED,[&]{WaitForGame(detected);});
    Reset();targets={GetCurrentProcessId()};open_error=ERROR_INVALID_PARAMETER;CancelSoon([&]{WaitForGame(detected);});
    Reset();targets={GetCurrentProcessId()};observed_path=wrong;Expect(ERROR_INVALID_NAME,[&]{WaitForGame(detected);});
    Reset();targets={GetCurrentProcessId()};observed_path=game;HANDLE found=WaitForGame(detected);
    Check(found&&GetProcessId(found)==GetCurrentProcessId()&&detected==game&&path_discovered&&game_pid==GetCurrentProcessId());CloseHandle(found);
    SaveGamePath(detected);Reset();Check(ConfiguredGamePath()==game);
    std::wstring unicode_dir=directory+L"\u8def\u5f84 \U0001f31f\\";Check(CreateDirectoryW(unicode_dir.c_str(),nullptr)||GetLastError()==ERROR_ALREADY_EXISTS);
    std::wstring unicode_game=unicode_dir+TargetName;Write(unicode_game,"Unicode path");DeleteFileW(ini.c_str());path_discovered=true;SaveGamePath(unicode_game);Check(ConfiguredGamePath()==unicode_game);
    Reset();Hash(game);DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);cancel_read=true;reads=0;Expect(ERROR_CANCELLED,[&]{Hash(game);});cancel_read=false;
    GetProcessHandleCount(GetCurrentProcess(),&after);Check(before==after);Check(!load_started&&!runtime_ready);
    Reset();cancel_requested=true;Expect(ERROR_CANCELLED,[]{Load(GetCurrentProcess());});Check(thread_starts==0);
    Reset();Expect(ERROR_CANCELLED,[]{Load(GetCurrentProcess());});Check(thread_starts==1&&load_started&&thread_saw_memory);
    MEMORY_BASIC_INFORMATION memory{};Check(VirtualQuery(thread_memory,&memory,sizeof(memory))&&memory.State==MEM_FREE);
    Reset();HANDLE guard=ClaimGame();Check(guard!=nullptr);Expect(ERROR_ALREADY_EXISTS,[]{HANDLE other=ClaimGame();CloseHandle(other);});CloseHandle(guard);
    wchar_t map_name[100];swprintf_s(map_name,L"Local\\Istaroth.Runtime.%lu",game_pid);
    HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RuntimeView),map_name);Check(map!=nullptr);
    Expect(ERROR_ALREADY_EXISTS,[]{HANDLE other=ClaimGame();CloseHandle(other);});
    auto view=(RuntimeView*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(RuntimeView));Check(view!=nullptr);
    RuntimeView sample{};sample.magic=RuntimeMagic;sample.version=RuntimeVersion;sample.size=sizeof(sample);sample.sequence=2;sample.pid=game_pid;sample.birth=game_birth;
    sample.status=2;sample.flags=Initialized|StartedPaused;sample.heartbeat=sample.present_tick=GetTickCount64();memcpy(view,&sample,sizeof(sample));AwaitReady(GetCurrentProcess());Check(!runtime_ready);
    sample.birth++;memcpy(view,&sample,sizeof(sample));Expect(ERROR_REVISION_MISMATCH,[]{AwaitReady(GetCurrentProcess());});sample.birth--;
    sample.status=3;sample.error=ERROR_DLL_INIT_FAILED;memcpy(view,&sample,sizeof(sample));Expect(ERROR_DLL_INIT_FAILED,[]{AwaitReady(GetCurrentProcess());});
    sample.status=2;sample.error=0;sample.heartbeat=sample.present_tick=GetTickCount64()-2000;memcpy(view,&sample,sizeof(sample));Reset();CancelSoon([]{AwaitReady(GetCurrentProcess());});
    Reset();sample.heartbeat=sample.present_tick=GetTickCount64();sample.flags=Initialized;memcpy(view,&sample,sizeof(sample));CancelSoon([]{AwaitReady(GetCurrentProcess());});
    UnmapViewOfFile(view);CloseHandle(map);Reset();HANDLE exited=CreateEventW(nullptr,TRUE,TRUE,nullptr);Expect(ERROR_PROCESS_ABORTED,[&]{AwaitReady(exited);});CloseHandle(exited);
    DestroyWindow(progress);progress=nullptr;
    // Verified payload data is copied only for hashing; no DLL is loaded here.
    std::wstring payload_dir=entry_dir+L"..\\";
    for(auto payload:Payloads)Check(CopyFileW((payload_dir+payload.name).c_str(),(entry_dir+payload.name).c_str(),FALSE)!=FALSE);
    std::wstring entry_ini=entry_dir+L"Istaroth.ini";
    auto entry=[&](const wchar_t* command) {
        Reset();runtime_ready=false;game_pid=0;game_birth=0;command_line=command;
        int result=LoaderEntryNotRun(GetModuleHandleW(nullptr),nullptr,nullptr,SW_HIDE);progress=nullptr;UnregisterClassW(L"Istaroth.Injector",GetModuleHandleW(nullptr));return result;
    };
    DeleteFileW(entry_ini.c_str());
    std::wstring first_payload=entry_dir+Payloads[0].name;
    Check(DeleteFileW(first_payload.c_str())!=FALSE);
    Check(entry(L"OwnedLoader.exe")==ERROR_FILE_NOT_FOUND);
    Write(first_payload,"damaged payload");
    Check(entry(L"OwnedLoader.exe")==ERROR_CRC);
    Check(process_starts==0&&terminations==0);
    Check(CopyFileW((payload_dir+Payloads[0].name).c_str(),first_payload.c_str(),FALSE)!=FALSE);
    DeleteFileW(entry_ini.c_str());Check(entry(L"OwnedLoader.exe --quiet")==ERROR_PATH_NOT_FOUND);
    Check(entry(L"OwnedLoader.exe --quiet --invalid")==ERROR_INVALID_PARAMETER);
    HANDLE closer=CreateThread(nullptr,0,CloseEntryLater,nullptr,0,nullptr);Check(closer!=nullptr);Check(entry(L"OwnedLoader.exe")==ERROR_CANCELLED);WaitForSingleObject(closer,1000);CloseHandle(closer);
    Check(GetFileAttributesW(entry_ini.c_str())==INVALID_FILE_ATTRIBUTES);
    WritePrivateProfileStringW(L"Istaroth",L"GamePath",game.c_str(),entry_ini.c_str());Check(entry(L"OwnedLoader.exe --quiet")==ERROR_REVISION_MISMATCH);
    Check(entry(L"OwnedLoader.exe")==ERROR_REVISION_MISMATCH);Check(entry_title_ok);
    Check(process_starts==0&&terminations==0&&thread_starts==1&&!load_started&&!runtime_ready);
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"actual close/config/hash/ACK and self-owned-thread tests; process discovery redirected to test process\",\"game_started\":false,\"game_process_accessed\":false}\n",checks);
}
