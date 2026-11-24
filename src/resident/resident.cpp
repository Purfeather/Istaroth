#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstdio>
#include "resident_internal.h"
#include "diagnostics.h"
#include "file_integrity.h"
#include "verified_peers.h"
#ifndef ISTAROTH_FIXTURE
#include "game_build.h"
#endif

namespace istaroth {
HMODULE ResidentModule=nullptr;
HWND GameWindow=nullptr;
volatile LONG MenuVisible=0,RendererReady=0,BackdropReady=0;
alignas(8) volatile LONG64 LastPresent=0;
std::wstring DataDirectory;
enum FeatureBit : LONG { AdvanceBit=1, ChooseFirstBit=2, SkipCooldownBit=4 };
static volatile LONG enabled=0,target=4,features=AdvanceBit|ChooseFirstBit|SkipCooldownBit;
static volatile LONG persist_revision=0,external_error=0,ui_ready=0;
static SRWLOCK ui_lock=SRWLOCK_INIT;
static UiState ui_state;
alignas(8) static volatile LONG64 last_control=0;
static RuntimeView *runtime=nullptr;
static ClockShared *clock_map=nullptr;
static ActionShared *action_map=nullptr;
static ResidentControl control;

static void Log(const char* message) {
    diagnostics::Append(DataDirectory,L"Istaroth-runtime.log",message);
}
void TraceRuntime(const char* message) {Log(message);}
void SetRuntimeError(DWORD error,const char* message) {
    InterlockedCompareExchange(&external_error,(LONG)(error?error:ERROR_GEN_FAILURE),0);
    InterlockedExchange(&enabled,0);
    char line[512];sprintf_s(line,"event=runtime_error error=%lu hex=0x%08lx reason=%s",error,error,message);Log(line);
}
void EmitCommand(UiCommand command,int value) {
    char line[160];sprintf_s(line,"event=ui_command command=%u value=%d",(unsigned)command,value);Log(line);
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
    LONG bit;
    switch (command) {
    case UiCommand::Advance: bit=AdvanceBit; break;
    case UiCommand::ChooseFirst: bit=ChooseFirstBit; break;
    case UiCommand::SkipCooldown: bit=SkipCooldownBit; break;
    default: return;
    }
    LONG before=features,after;
    do {after=value?before|bit:before&~bit;LONG found=InterlockedCompareExchange(&features,after,before);if(found==before)break;before=found;}while(true);
    InterlockedIncrement(&persist_revision);
}
UiState GetUiState() {
    AcquireSRWLockShared(&ui_lock);UiState copy=ui_state;ReleaseSRWLockShared(&ui_lock);
    uint64_t heartbeat=(uint64_t)InterlockedCompareExchange64(&last_control,0,0);
    if(heartbeat&&!Fresh(GetTickCount64(),heartbeat,400)) {
        copy.actual_known=copy.speed_active=copy.effects_active=copy.dialogue_ready=false;
        copy.advance_active=copy.choose_active=copy.skip_active=false;copy.phase=UiPhase::Fault;
        strcpy_s(copy.reason,u8"控制器响应超时，等待恢复确认");
    }
    else if(MenuVisible&&GameWindow&&GetForegroundWindow()==GameWindow&&copy.enabled&&copy.phase!=UiPhase::Fault) {
        SetOpenMenuReason(copy);
    }
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
// A late peer can be requested after the launcher has closed. Check it here
// as well and keep the verified file open until the local library load returns.
static DWORD LoadPeer(const PinnedPeer& peer) {
    try {
        const std::atomic_bool stop{false};
        FileHasher verifier;
        auto file = verifier.Verify(DataDirectory + peer.name, peer.sha256, stop);
        const auto path = FinalFilePath(file.get());
        if (!LoadLibraryW(path.c_str())) return GetLastError();
        return ERROR_SUCCESS;
    } catch (const DigestMismatch&) { return ERROR_CRC; }
    catch (DWORD error) { return error; }
    catch (const std::bad_alloc&) { return ERROR_OUTOFMEMORY; }
}
static void LoadSettings() {
    const std::wstring ini = DataDirectory + L"Istaroth.ini";
    const unsigned configured = GetPrivateProfileIntW(L"Settings", L"Multiplier", 4, ini.c_str());
    target = configured >= 1 && configured <= 4 ? static_cast<LONG>(configured) : 4;
    auto selected = [&](const wchar_t* key) { return GetPrivateProfileIntW(L"Settings", key, 1, ini.c_str()) == 1; };
    features = (selected(L"Advance") ? AdvanceBit : 0)
             | (selected(L"ChooseFirst") ? ChooseFirstBit : 0)
             | (selected(L"SkipCooldown") ? SkipCooldownBit : 0);
}

static Desired GetDesired() {
    Desired d;
    d.enabled = enabled != 0;
    d.target = static_cast<unsigned>(target);
    const LONG bits = features;
    d.advance = (bits & AdvanceBit) != 0;
    d.choose = (bits & ChooseFirstBit) != 0;
    d.skip = (bits & SkipCooldownBit) != 0;
    return d;
}
static void SaveDesired(const Desired& d) {
    std::wstring path=DataDirectory+L"Istaroth.ini";wchar_t number[8];swprintf_s(number,L"%u",d.target);
    DWORD error=0;
    auto write = [&](const wchar_t* key, const wchar_t* value) {
        if (!WritePrivateProfileStringW(L"Settings", key, value, path.c_str()) && !error) error = GetLastError();
    };
    write(L"Multiplier", number);
    write(L"Advance", d.advance ? L"1" : L"0");
    write(L"ChooseFirst", d.choose ? L"1" : L"0");
    write(L"SkipCooldown", d.skip ? L"1" : L"0");
    char line[160];sprintf_s(line,"event=config_saved error=%lu target=%u advance=%u choose=%u skip=%u",error,d.target,d.advance,d.choose,d.skip);Log(line);
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
    LoadSettings();
    swprintf_s(name,L"Local\\Istaroth.DialogueActions.Writer.%lu",GetCurrentProcessId());
    HANDLE action_writer=CreateMutexW(nullptr,FALSE,name);
    if(!action_writer||GetLastError()==ERROR_ALREADY_EXISTS)SetRuntimeError(ERROR_BUSY,"another action writer exists");
    swprintf_s(name,L"Local\\GenshinSkip.Time.Writer.%lu",GetCurrentProcessId());
    HANDLE clock_writer=CreateMutexW(nullptr,FALSE,name);
    // Retain ownership for this worker's lifetime; legacy writers cannot interleave.
    if(!clock_writer||WaitForSingleObject(clock_writer,0)!=WAIT_OBJECT_0)SetRuntimeError(ERROR_BUSY,"another time writer owns the mailbox");
    if(!external_error) {
#ifdef ISTAROTH_FIXTURE
        if(DWORD error=LoadPeer(TestClockPeer))SetRuntimeError(error,"owned time component load failed");
#else
        if(DWORD error=LoadPeer(ClockPeer))SetRuntimeError(error,"time component load failed");
#endif
    }
    HANDLE clock_handle=nullptr,action_handle=nullptr,story_handle=nullptr;
    BYTE* story_map=nullptr;
    bool story_loaded=false,actions_loaded=false,renderer_attempted=false,initialized=false;
    ClockShared last_clock{};ActionShared last_action{};bool have_clock_snapshot=false,have_action_snapshot=false;
    uint64_t started=GetTickCount64(),window_check=0,last_diagnostic=0;LONG saved_revision=0;bool f10_down=false,insert_down=false;
    UiPhase previous=UiPhase::Starting;
    for(;;) {
        uint64_t now=GetTickCount64();bool down=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
        if(down&&!f10_down)EmitCommand(UiCommand::Emergency,0);f10_down=down;
        bool insert=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;
        if(insert&&!insert_down&&((GameWindow&&GetForegroundWindow()==GameWindow)||RendererOwnsForeground()))SetMenuVisible(!MenuVisible);
        insert_down=insert;
        if(now-window_check>=250) {unsigned found=0;GameWindow=nullptr;EnumWindows(FindWindow,(LPARAM)&found);if(found!=1)GameWindow=nullptr;window_check=now;}
        if(!clock_map&&!external_error)clock_map=(ClockShared*)OpenMap(L"Local\\GenshinSkip.Time.",104,FILE_MAP_ALL_ACCESS,clock_handle);
        if(actions_loaded&&!action_map&&!external_error)action_map=(ActionShared*)OpenMap(L"Local\\Istaroth.DialogueActions.",336,FILE_MAP_ALL_ACCESS,action_handle);
        if(story_loaded&&!story_map&&!external_error)story_map=(BYTE*)OpenMap(L"Local\\GenshinSkip.Story.",19904,FILE_MAP_READ,story_handle);
        ClockShared c{};ActionShared a{};
        bool clock_read=ReadSnapshot(clock_map,c,offsetof(ClockShared,sequence));
        bool action_read=ReadSnapshot(action_map,a,offsetof(ActionShared,sequence));
        // A contended seqlock is not evidence that the producer stopped. Reuse
        // only the last complete sample; its original heartbeat still expires.
        if(clock_read){last_clock=c;have_clock_snapshot=true;}
        else if(have_clock_snapshot){c=last_clock;clock_read=true;}
        if(action_read){last_action=a;have_action_snapshot=true;}
        else if(have_action_snapshot){a=last_action;action_read=true;}
        const DWORD pid = GetCurrentProcessId();
        const bool clock_ready = clock_read && c.magic == 0x47535431 && c.version == 1 && c.pid == pid;
        if (!story_loaded && !external_error && clock_ready && c.heartbeat && c.status <= 2) {
            SendClock(false, 1);
#ifdef ISTAROTH_FIXTURE
            story_loaded = actions_loaded = true;
#else
            if (const DWORD error = LoadPeer(StoryPeer)) SetRuntimeError(error, "story component load failed");
            else story_loaded = true;
#endif
        }
        const auto story_header = reinterpret_cast<const volatile DWORD*>(story_map);
        const bool story_ready = story_header && story_header[0] == 0x47535331 &&
                                 story_header[1] == 7 && story_header[2] == pid;
#ifndef ISTAROTH_FIXTURE
        if (story_ready && !external_error) {
            const DWORD status = story_header[3];
            if (status == 3 || status == 7) {
                SetRuntimeError(status == 3 ? ERROR_REVISION_MISMATCH : ERROR_INVALID_STATE, "story component stopped");
            } else if (!actions_loaded && status == 2 && GameWindow) {
                if (const DWORD error = LoadPeer(ActionPeer)) SetRuntimeError(error, "dialogue component load failed");
                else actions_loaded = true;
            }
        }
#endif
        bool components=clock_ready&&action_read&&story_ready&&
            a.magic==0x49534131&&a.version==4&&a.pid==pid&&a.status!=0;
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
        if(now-last_diagnostic>=5000||result.ui.phase!=previous) {
            char line[768];sprintf_s(line,"event=state phase=%u initialized=%u enabled=%u foreground=%u menu=%lu dialogue=%u target=%u actual=%.3f known=%u clock_status=%lu clock_error=%ld action_status=%lu action_error=%lu gate=%lu renderer=%u advance=%u choose=%u skip=%u error=%lu",
                (unsigned)result.ui.phase,initialized,desired.enabled,foreground,(DWORD)MenuVisible,result.ui.dialogue,desired.target,result.ui.actual,result.ui.actual_known,
                clock_read?c.status:0,clock_read?c.error:0,action_read?a.status:0,action_read?a.error:0,action_read?a.gate_reason:0,rendered,desired.advance,desired.choose,desired.skip,result.error);
            Log(line);last_diagnostic=now;
        }
        if(result.ui.phase!=previous) {previous=result.ui.phase;Log(result.ui.reason);}
        const LONG revision = persist_revision;
        if (revision != saved_revision) {
            SaveDesired(GetDesired());
            saved_revision = revision;
        }
        Sleep(25);
    }
}
}
static __declspec(noinline) BOOL AttachResident(HINSTANCE module) {
#ifdef ISTAROTH_FIXTURE
        wchar_t path[512];DWORD length=GetModuleFileNameW(nullptr,path,512);if(!length||length>=512)return FALSE;
        const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
        if(lstrcmpiW(name,L"gst-time-test.exe"))return FALSE;
#else
        if (!IstarothCurrentBuild()) return FALSE;
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
