#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstdio>
#include "resident_internal.h"

namespace istaroth {
HMODULE ResidentModule=nullptr;
HWND GameWindow=nullptr;
volatile LONG MenuVisible=0,RendererReady=0,BackdropReady=0;
alignas(8) volatile LONG64 LastPresent=0;
std::wstring DataDirectory;
static volatile LONG enabled=0,target=4,features=7,persist_revision=0,external_error=0,ui_ready=0;
static SRWLOCK ui_lock=SRWLOCK_INIT;
static UiState ui_state;
alignas(8) static volatile LONG64 last_control=0;
static RuntimeView *runtime=nullptr;
static ClockShared *clock_map=nullptr;
static ActionShared *action_map=nullptr;
static ResidentControl control;

static void Log(const char* message) {
    std::wstring path=DataDirectory+L"Istaroth-runtime.log";
    HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME t;GetSystemTime(&t);char line[512];
    int size=sprintf_s(line,"%04u-%02u-%02uT%02u:%02u:%02uZ pid=%lu %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),message);
    DWORD written=0;if(size>0)WriteFile(file,line,(DWORD)size,&written,nullptr);CloseHandle(file);
}
void TraceRuntime(const char* message) {Log(message);}
void SetRuntimeError(DWORD error,const char* message) {
    InterlockedCompareExchange(&external_error,(LONG)(error?error:ERROR_GEN_FAILURE),0);
    InterlockedExchange(&enabled,0);Log(message);
}
void EmitCommand(UiCommand command,int value) {
    if(command==UiCommand::Emergency){InterlockedExchange(&enabled,0);return;}
    if(command==UiCommand::Multiplier) {
        if(value<1||value>4)return;
        InterlockedExchange(&target,value);InterlockedIncrement(&persist_revision);return;
    }
    if(value!=0&&value!=1)return;
    if(command==UiCommand::Enabled) {
        if(value&&!InterlockedCompareExchange(&ui_ready,0,0))return;
        InterlockedExchange(&enabled,value);return;
    }
    LONG bit=command==UiCommand::Advance?1:command==UiCommand::ChooseFirst?2:command==UiCommand::SkipCooldown?4:0;
    if(!bit)return;
    LONG before=features,after;
    do {after=value?before|bit:before&~bit;LONG found=InterlockedCompareExchange(&features,after,before);if(found==before)break;before=found;}while(true);
    InterlockedIncrement(&persist_revision);
}
UiState GetUiState() {
    AcquireSRWLockShared(&ui_lock);UiState copy=ui_state;ReleaseSRWLockShared(&ui_lock);
    uint64_t heartbeat=(uint64_t)InterlockedCompareExchange64(&last_control,0,0);
    if(heartbeat&&!Fresh(GetTickCount64(),heartbeat,400)) {
        copy.actual_known=copy.speed_active=copy.effects_active=false;
        copy.advance_active=copy.choose_active=copy.skip_active=false;copy.phase=UiPhase::Fault;
        strcpy_s(copy.reason,u8"控制器响应超时，等待恢复确认");
    }
    else if(MenuVisible&&RendererOwnsForeground()&&copy.enabled&&copy.phase!=UiPhase::Fault)
        strcpy_s(copy.reason,u8"菜单已打开，功能暂挂；关闭后继续");
    return copy;
}
void RendererTick() {InterlockedExchange64(&LastPresent,(LONG64)GetTickCount64());InterlockedExchange(&RendererReady,1);}

static void* OpenMap(const wchar_t* prefix,SIZE_T size,DWORD access,HANDLE& handle) {
    wchar_t name[100];swprintf_s(name,L"%s%lu",prefix,GetCurrentProcessId());
    handle=OpenFileMappingW(access,FALSE,name);
    if(!handle)return nullptr;
    void* view=MapViewOfFile(handle,access,0,0,size);
    if(!view) {
        DWORD error=GetLastError();CloseHandle(handle);handle=nullptr;SetLastError(error);
    }
    return view;
}
static DWORD SendClock(bool active,unsigned multiplier) {
    if(!clock_map||clock_map->magic!=0x47535431||clock_map->version!=1||clock_map->pid!=GetCurrentProcessId())return 0;
    LONG sequence=clock_map->command_seq;
    if(!(sequence&1))InterlockedIncrement(&clock_map->command_seq);
    clock_map->enable=active?1:0;clock_map->requested_milli=(LONG)multiplier*1000;clock_map->stop=0;
    MemoryBarrier();return (DWORD)InterlockedIncrement(&clock_map->command_seq);
}
static DWORD SendActions(bool permit,const Desired& d,DWORD epoch) {
    if(!action_map||action_map->magic!=0x49534131||action_map->version!=4||action_map->pid!=GetCurrentProcessId())return 0;
    DWORD sequence=((DWORD)action_map->command+2)&0x7ffffffeu;if(!sequence)sequence=2;
    InterlockedExchange(&action_map->command,(LONG)sequence-1);
    action_map->enabled=permit;action_map->advance=d.advance;action_map->choose=d.choose;action_map->skip=d.skip;
    action_map->stop=0;action_map->limit=0xffffffffu;action_map->epoch=epoch;action_map->command_tick=GetTickCount64();
    MemoryBarrier();InterlockedExchange(&action_map->command,(LONG)sequence);return sequence;
}
static BOOL CALLBACK FindWindow(HWND window,LPARAM value) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t name[64]={};
    if(pid==GetCurrentProcessId()&&IsWindowVisible(window)&&GetClassNameW(window,name,64)&&!lstrcmpW(name,L"UnityWndClass")) {
        auto count=(unsigned*)value;(*count)++;GameWindow=window;
    }
    return TRUE;
}
static bool LoadPeer(const wchar_t* name) {return LoadLibraryW((DataDirectory+name).c_str())!=nullptr;}
static Desired GetDesired() {
    Desired d;d.enabled=enabled!=0;d.target=(unsigned)target;LONG bits=features;
    d.advance=(bits&1)!=0;d.choose=(bits&2)!=0;d.skip=(bits&4)!=0;return d;
}
static void SaveDesired(const Desired& d) {
    std::wstring path=DataDirectory+L"Istaroth.ini";wchar_t number[8];swprintf_s(number,L"%u",d.target);
    WritePrivateProfileStringW(L"Settings",L"Multiplier",number,path.c_str());
    WritePrivateProfileStringW(L"Settings",L"Advance",d.advance?L"1":L"0",path.c_str());
    WritePrivateProfileStringW(L"Settings",L"ChooseFirst",d.choose?L"1":L"0",path.c_str());
    WritePrivateProfileStringW(L"Settings",L"SkipCooldown",d.skip?L"1":L"0",path.c_str());
}
static void Publish(const ControlResult& result,const ClockShared* c,const ActionShared* a,bool initialized) {
    auto& u=result.ui;
    InterlockedExchange64(&last_control,(LONG64)GetTickCount64());
    AcquireSRWLockExclusive(&ui_lock);ui_state=u;ReleaseSRWLockExclusive(&ui_lock);
    InterlockedIncrement(&runtime->sequence);MemoryBarrier();
    runtime->status=result.error?3u:initialized?2u:1u;runtime->error=result.error;
    runtime->heartbeat=runtime->controller_tick=GetTickCount64();runtime->present_tick=(uint64_t)LastPresent;
    runtime->flags=(initialized?(Initialized|StartedPaused):0u)|(MenuVisible?MenuOpen:0u)|(u.enabled?Enabled:0u)|
        (u.foreground?Foreground:0u)|(u.dialogue?Dialogue:0u)|(result.permit?GateReady:0u)|
        (u.actual_known?ActualKnown:0u)|(u.speed_active?SpeedActive:0u)|(u.effects_active?EffectsActive:0u)|
        (u.advance?Advance:0u)|(u.choose_first?Choose:0u)|(u.skip_cooldown?Skip:0u)|
        (u.advance_active?AdvanceActive:0u)|(u.choose_active?ChooseActive:0u)|(u.skip_active?SkipActive:0u)|(BackdropReady?BlurReady:0u);
#ifdef ISTAROTH_FIXTURE
    runtime->flags|=Fixture;
#endif
    runtime->target=u.target;runtime->actual=u.actual;runtime->baseline=u.baseline;
    runtime->field_scale=c?c->scale:0;runtime->clock_status=c?c->status:0;runtime->action_status=a?a->status:0;
    runtime->gate_reason=a?a->gate_reason:2;runtime->gate_epoch=a?a->gate_epoch:0;
    runtime->attempts=a?a->attempts:0;runtime->completed=a?a->completed:0;runtime->choices=a?a->choices:0;
    runtime->outer_bypassed=a?a->outer_bypassed:0;runtime->auto_bypassed=a?a->auto_bypassed:0;
    runtime->phase=(DWORD)u.phase;strcpy_s(runtime->reason,u.reason);
    MemoryBarrier();InterlockedIncrement(&runtime->sequence);
}
static DWORD WINAPI Run(void*) {
#ifdef ISTAROTH_FIXTURE
    ULONG guaranteed=65536;SetThreadStackGuarantee(&guaranteed);
#endif
    wchar_t own[32768];DWORD length=GetModuleFileNameW(ResidentModule,own,32768);
    if(!length||length>=32768)return 1;
    DataDirectory=own;DataDirectory.resize(DataDirectory.find_last_of(L"\\/")+1);
    wchar_t name[100];swprintf_s(name,L"Local\\Istaroth.Runtime.%lu",GetCurrentProcessId());
    HANDLE status_handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RuntimeView),name);
    if(!status_handle||GetLastError()==ERROR_ALREADY_EXISTS)return 2;
    runtime=(RuntimeView*)MapViewOfFile(status_handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(RuntimeView));if(!runtime)return 3;
    runtime->magic=RuntimeMagic;runtime->version=RuntimeVersion;runtime->size=sizeof(RuntimeView);runtime->pid=GetCurrentProcessId();
    FILETIME birth,exit,kernel,user;GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);memcpy(&runtime->birth,&birth,8);
    runtime->status=1;runtime->heartbeat=GetTickCount64();Log("resident loaded; initial state paused");
    std::wstring ini=DataDirectory+L"Istaroth.ini";
    unsigned configured=GetPrivateProfileIntW(L"Settings",L"Multiplier",4,ini.c_str());target=configured>=1&&configured<=4?(LONG)configured:4;
    features=(GetPrivateProfileIntW(L"Settings",L"Advance",1,ini.c_str())==1?1:0)|
        (GetPrivateProfileIntW(L"Settings",L"ChooseFirst",1,ini.c_str())==1?2:0)|
        (GetPrivateProfileIntW(L"Settings",L"SkipCooldown",1,ini.c_str())==1?4:0);
    swprintf_s(name,L"Local\\Istaroth.DialogueActions.Writer.%lu",GetCurrentProcessId());
    HANDLE action_writer=CreateMutexW(nullptr,FALSE,name);
    if(!action_writer||GetLastError()==ERROR_ALREADY_EXISTS)SetRuntimeError(ERROR_BUSY,"another action writer exists");
    swprintf_s(name,L"Local\\GenshinSkip.Time.Writer.%lu",GetCurrentProcessId());
    HANDLE clock_writer=CreateMutexW(nullptr,FALSE,name);
    // Retain ownership for this worker's lifetime; legacy writers cannot interleave.
    if(!clock_writer||WaitForSingleObject(clock_writer,0)!=WAIT_OBJECT_0)SetRuntimeError(ERROR_BUSY,"another time writer owns the mailbox");
    if(!external_error) {
#ifdef ISTAROTH_FIXTURE
        if(!LoadPeer(L"time_core_test.dll"))SetRuntimeError(GetLastError(),"owned time component load failed");
#else
        if(!LoadPeer(L"time_core.dll"))SetRuntimeError(GetLastError(),"time component load failed");
#endif
    }
    HANDLE clock_handle=nullptr,action_handle=nullptr,story_handle=nullptr;
    BYTE* story_map=nullptr;bool peers_loaded=false,renderer_attempted=false,initialized=false;
    ClockShared last_clock{};ActionShared last_action{};bool have_clock_snapshot=false,have_action_snapshot=false;
    uint64_t started=GetTickCount64(),window_check=0;LONG saved_revision=0;bool f10_down=false,insert_down=false;
    UiPhase previous=UiPhase::Starting;
    for(;;) {
        uint64_t now=GetTickCount64();bool down=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
        if(down&&!f10_down)EmitCommand(UiCommand::Emergency,0);f10_down=down;
        bool insert=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;
        if(insert&&!insert_down&&((GameWindow&&GetForegroundWindow()==GameWindow)||RendererOwnsForeground()))SetMenuVisible(!MenuVisible);
        insert_down=insert;
        if(now-window_check>=250) {unsigned found=0;GameWindow=nullptr;EnumWindows(FindWindow,(LPARAM)&found);if(found!=1)GameWindow=nullptr;window_check=now;}
        if(!clock_map&&!external_error)clock_map=(ClockShared*)OpenMap(L"Local\\GenshinSkip.Time.",104,FILE_MAP_ALL_ACCESS,clock_handle);
        if(clock_map&&!peers_loaded) {
            SendClock(false,1);peers_loaded=true;
#ifndef ISTAROTH_FIXTURE
            if(!LoadPeer(L"story_probe.dll")||!LoadPeer(L"dialogue_actions.dll"))SetRuntimeError(GetLastError(),"dialogue component load failed");
#endif
        }
        if(peers_loaded&&!action_map&&!external_error)action_map=(ActionShared*)OpenMap(L"Local\\Istaroth.DialogueActions.",336,FILE_MAP_ALL_ACCESS,action_handle);
        if(peers_loaded&&!story_map&&!external_error)story_map=(BYTE*)OpenMap(L"Local\\GenshinSkip.Story.",19904,FILE_MAP_READ,story_handle);
        ClockShared c{};ActionShared a{};
        bool clock_read=ReadSnapshot(clock_map,c,offsetof(ClockShared,sequence));
        bool action_read=ReadSnapshot(action_map,a,offsetof(ActionShared,sequence));
        // A contended seqlock is not evidence that the producer stopped. Reuse
        // only the last complete sample; its original heartbeat still expires.
        if(clock_read){last_clock=c;have_clock_snapshot=true;}
        else if(have_clock_snapshot){c=last_clock;clock_read=true;}
        if(action_read){last_action=a;have_action_snapshot=true;}
        else if(have_action_snapshot){a=last_action;action_read=true;}
        bool components=clock_read&&action_read&&story_map&&c.magic==0x47535431&&c.version==1&&c.pid==GetCurrentProcessId()&&
            a.magic==0x49534131&&a.version==4&&a.pid==GetCurrentProcessId()&&a.status!=0&&
            *(DWORD*)story_map==0x47535331&&*(DWORD*)(story_map+4)==7&&*(DWORD*)(story_map+8)==GetCurrentProcessId();
        if(components&&GameWindow&&!renderer_attempted&&!external_error) {
            TraceRuntime("native components responded; installing renderer");
            renderer_attempted=true;if(!InstallRenderer())SetRuntimeError(GetLastError(),"DX11 renderer setup failed");
            else TraceRuntime("resident UI thread started");
        }
        // The native producers can publish during the reads or renderer setup.
        // Compare their timestamps against a sample taken after those operations.
        uint64_t present=(uint64_t)InterlockedCompareExchange64(&LastPresent,0,0);
        now=GetTickCount64();
        bool rendered=RendererReady&&Fresh(now,present,1000);
        if(initialized&&GameWindow&&GetForegroundWindow()==GameWindow&&!rendered&&now-present>1500) {
            InterlockedExchange(&enabled,0);
        }
        bool game_foreground=GameWindow&&GetForegroundWindow()==GameWindow&&!IsIconic(GameWindow);
        if(!game_foreground&&!RendererOwnsForeground()&&MenuVisible)SetMenuVisible(false);
        bool foreground=game_foreground&&!MenuVisible;
        Desired desired=GetDesired();
        if(external_error)control.fault=(DWORD)external_error;
        ControlResult result=control.Evaluate(desired,clock_read?&c:nullptr,action_read?&a:nullptr,GetCurrentProcessId(),now,foreground,rendered,initialized);
        if(result.error) {InterlockedExchange(&enabled,0);InterlockedExchange(&ui_ready,0);desired.enabled=false;}
        DWORD sequence=SendClock(result.clock_enabled,result.multiplier);control.ClockSent(result.clock_enabled,result.multiplier,sequence);
        sequence=SendActions(result.permit,desired,result.epoch);control.ActionsSent(result.permit,desired,result.epoch,sequence);
        if(!initialized&&components&&rendered&&!result.error&&c.status==1&&!c.lease_active&&a.status==1&&!a.cooldown_active&&
           Fresh(now,c.heartbeat,1000)&&Fresh(now,a.heartbeat,250)&&Acknowledged((DWORD)c.ack,2)&&a.ack) {
            initialized=true;InterlockedExchange(&ui_ready,1);Log("runtime ready; DX11 frame and native component ACKs confirmed; injector may exit");
        }
        if(!initialized&&now-started>90000&&!control.fault){SetRuntimeError(ERROR_TIMEOUT,"initialization timed out");control.fault=ERROR_TIMEOUT;}
        Publish(result,clock_read?&c:nullptr,action_read?&a:nullptr,initialized&&!control.fault);
        if(result.ui.phase!=previous) {previous=result.ui.phase;Log(result.ui.reason);}
        LONG revision=persist_revision;if(revision!=saved_revision){SaveDesired(GetDesired());saved_revision=revision;}
        Sleep(25);
    }
}
}
static __declspec(noinline) BOOL AttachResident(HINSTANCE module) {
        wchar_t path[512];DWORD length=GetModuleFileNameW(nullptr,path,512);if(!length||length>=512)return FALSE;
        const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
#ifdef ISTAROTH_FIXTURE
        if(lstrcmpiW(name,L"gst-time-test.exe"))return FALSE;
#else
        if(lstrcmpiW(name,L"YuanShen.exe"))return FALSE;
#endif
        istaroth::ResidentModule=module;
        HANDLE worker=CreateThread(nullptr,0,istaroth::Run,nullptr,0,nullptr);if(!worker)return FALSE;CloseHandle(worker);
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*) {
    // /MT CRT needs its thread notifications. Keep this entry tiny even on
    // driver-created threads with small stacks; all work runs after attach.
    return reason==DLL_PROCESS_ATTACH?AttachResident(module):TRUE;
}
