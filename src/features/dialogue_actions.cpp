// Dialogue actions execute game UI events on the verified Unity update thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <intrin.h>
#include "detours.h"
#include "dialogue_actions_profile.h"
#include "curtain_advance.h"

struct Shared {
    DWORD magic,version,pid,status;
    volatile LONG command;
    DWORD enabled,advance,choose,skip,stop,limit,epoch;
    uint64_t command_tick;
    volatile LONG sequence;
    DWORD reason;
    uint64_t heartbeat;
    DWORD engine_tid,window_tid,attempts,completed,last_kind,last_index,option_count,ack_command;
    DWORD gate_valid,foreground,gate_epoch,cooldown_active,error,gate_reason;
    uint64_t last_tick,last_button,last_event,root_native,root_managed,stable_since;
    DWORD cooldown_seen,cooldown_bypassed;
    char text[128];
    DWORD choice_total,advance_total;
    DWORD outer_seen,outer_bypassed,auto_cooldown_seen,auto_cooldown_bypassed;
};
static_assert(sizeof(Shared)==336&&offsetof(Shared,sequence)==56&&offsetof(Shared,text)==184,"Dialogue actions ABI");
struct Root {uintptr_t native,managed,go;DWORD id;};
#ifndef ISTAROTH_ACTIONS_TEST
static HMODULE own_module;
static const IstarothGameBuild *game_build;
#endif
static const DialogueActionsProfile *action_profile = &cn_action_profile;
static BYTE *image,*story;
static Shared state,*shared;
static volatile LONG running,window_thread;
static uintptr_t arena_slot;
using Update=void(__fastcall*)(void*,void*);
static Update original_update;
static uint64_t last_poll;
static uintptr_t cached_context;
static Root gate_root;
static uintptr_t gate_context;
static uint64_t pending_choice;
static volatile LONG64 last_gate_check;
static volatile LONG gate_invalidated;
static HWND volatile game_window;
static istaroth::ContinueLatch continue_latch;
extern "C" {
    volatile LONG istaroth_cooldown_enabled=0,istaroth_cooldown_seen=0,istaroth_cooldown_bypassed=0;
    uintptr_t istaroth_cooldown_model=0;
    void *istaroth_cooldown_original=nullptr,*istaroth_cooldown_next=nullptr;
    uintptr_t istaroth_advance_context=0;
    volatile LONG istaroth_advance_tid=0,istaroth_outer_seen=0,istaroth_outer_bypassed=0;
    volatile LONG istaroth_auto_cooldown_seen=0,istaroth_auto_cooldown_bypassed=0;
    void *istaroth_outer_a_original=nullptr,*istaroth_outer_a_next=nullptr;
    void *istaroth_outer_b_original=nullptr,*istaroth_outer_b_next=nullptr;
    void IstarothOuterA();
    void IstarothOuterB();
    void IstarothOsOuterA();
    void IstarothOsOuterB();
    DWORD IstarothOuterFixtureA(float,float,uintptr_t);
    DWORD IstarothOuterFixtureB(float,float,uintptr_t);
    void IstarothOuterCompareA();void IstarothOuterNextA();
    void IstarothOuterCompareB();void IstarothOuterNextB();
    DWORD IstarothOsOuterFixtureA(float,float,uintptr_t);
    DWORD IstarothOsOuterFixtureB(float,float,uintptr_t);
    void IstarothOsOuterCompareA();void IstarothOsOuterNextA();
    void IstarothOsOuterCompareB();void IstarothOsOuterNextB();
    void IstarothCooldownGate();
    DWORD IstarothCooldownFixture(float,float,uintptr_t);
    void IstarothFixtureJump();
    void IstarothFixtureNext();
}
static void clear_cooldown() {
    InterlockedExchange(&istaroth_cooldown_enabled,0);
    InterlockedExchangePointer((PVOID volatile*)&istaroth_advance_context,nullptr);
    InterlockedExchange(&istaroth_advance_tid,0);
    InterlockedExchangePointer((PVOID volatile*)&istaroth_cooldown_model,nullptr);
}
static bool foreground() {return game_window&&GetForegroundWindow()==game_window&&!IsIconic(game_window);}

static bool bytes(void *to,uintptr_t at,SIZE_T size) {
    SIZE_T count=0;
    return at>=0x10000&&size&&size<=19904&&at+size>at&&
        ReadProcessMemory(GetCurrentProcess(),(void*)at,to,size,&count)&&count==size;
}
static uintptr_t pointer(uintptr_t at) {uintptr_t value=0;return bytes(&value,at,8)?value:0;}
static DWORD u32(uintptr_t at) {DWORD value=0xffffffff;bytes(&value,at,4);return value;}
static bool one(uintptr_t at) {BYTE value=0;return bytes(&value,at,1)&&value==1;}
static bool overseas() { return action_profile->overseas; }

static bool named_class(uintptr_t klass,const char *wanted) {
    char name[64]={};
    return bytes(name,pointer(klass+action_profile->class_name_offset),sizeof(name))&&memchr(name,0,sizeof(name))&&!strcmp(name,wanted);
}
static bool named(uintptr_t object,const char *wanted) {return named_class(pointer(object),wanted);}
static bool active_go(uintptr_t go) {return go&&one(go+0x1f8);}
static bool component(uintptr_t object,uintptr_t *go=nullptr) {
    uintptr_t native=pointer(object+16),owner=pointer(native+0x40);
    if(!native||pointer(native)!=(uintptr_t)image+action_profile->mono_vtable||u32(native+0x20)!=2||
        pointer(native+0x28)!=object||!active_go(owner))return false;
    if(go)*go=owner;
    return true;
}
static bool live_root(const Root &root) {
    uintptr_t go=0;
    return named(root.managed,"MonoTalkDialog")&&pointer(root.managed+16)==root.native&&
        u32(root.native+8)==root.id&&component(root.managed,&go)&&go==root.go;
}
static bool story_snapshot(BYTE *snapshot,DWORD *count) {
    LONG before=0,after=0;
    if(!story||!bytes(&before,(uintptr_t)story+16,4)||before&1||
        !bytes(snapshot,(uintptr_t)story,19904)||!bytes(&after,(uintptr_t)story+16,4)||before!=after)return false;
    DWORD magic=0,version=0,pid=0,status=0;uint64_t heartbeat=0;
    memcpy(&magic,snapshot,4);memcpy(&version,snapshot+4,4);memcpy(&pid,snapshot+8,4);
    memcpy(&status,snapshot+12,4);memcpy(count,snapshot+20,4);memcpy(&heartbeat,snapshot+40,8);
    return magic==0x47535331&&version==7&&pid==GetCurrentProcessId()&&status==2&&*count<=16&&GetTickCount64()-heartbeat<=250;
}
static DWORD find_root(Root *root) {
    BYTE snapshot[19904];DWORD count=0;
    if(!story_snapshot(snapshot,&count))return 2;
    DWORD found=0;
    for(DWORD i=0;i<count;i++) {
        BYTE *record=snapshot+56+i*896;char name[64]={};memcpy(name,record+28,63);
        if(strcmp(name,"MonoTalkDialog"))continue;
        Root candidate={};DWORD origin=0;
        memcpy(&candidate.native,record,8);memcpy(&candidate.managed,record+8,8);memcpy(&candidate.id,record+16,4);memcpy(&origin,record+24,4);
        candidate.go=pointer(candidate.native+0x40);
        if(origin||!live_root(candidate))continue;
        if(++found>1)return 4;
        *root=candidate;
    }
    return found?0:3;
}
static istaroth::ContinueObservation find_continue(istaroth::ContinueKey *key,uint64_t now) {
    BYTE snapshot[19904];DWORD count=0,found=0;bool invalid=false;
    if(!story_snapshot(snapshot,&count))return istaroth::ContinueObservation::Unknown;
    for(DWORD i=0;i<count;i++) {
        BYTE *record=snapshot+56+i*896;char name[64]={};memcpy(name,record+28,63);
        if(strcmp(name,"MonoClickContinue"))continue;
        istaroth::ContinueKey candidate{};DWORD origin=0;
        memcpy(&candidate.native,record,8);memcpy(&candidate.managed,record+8,8);
        memcpy(&candidate.instance,record+16,4);memcpy(&origin,record+24,4);
        BYTE prefix[80]={},flags[3]={};uintptr_t owner=0;
        if(origin||!candidate.Valid()||!named(candidate.managed,"MonoClickContinue")||
           pointer(candidate.managed+16)!=candidate.native||!bytes(prefix,candidate.native,sizeof(prefix))||
           pointer(candidate.native)!=(uintptr_t)image+action_profile->mono_vtable||u32(candidate.native+8)!=candidate.instance||
           u32(candidate.native+0x20)!=2||pointer(candidate.native+0x28)!=candidate.managed) {invalid=true;continue;}
        memcpy(&owner,prefix+0x40,8);
        if(!bytes(flags,owner+0x1f6,3)||flags[2]>1||prefix[0x48]>1) {invalid=true;continue;}
        if(!flags[2])continue_latch.Observe(istaroth::ContinueObservation::Inactive,candidate,now,false);
        else if(!prefix[0x48])invalid=true;
        else {*key=candidate;found++;}
    }
    return !invalid&&found==1?istaroth::ContinueObservation::Waiting:istaroth::ContinueObservation::Unknown;
}
static uintptr_t current_action(uintptr_t context) {
    return pointer(context + (overseas() ? 0x240 : 0x248));
}

static uintptr_t verified_method(uintptr_t klass, uintptr_t rva, DWORD cn_index, WORD cn_count, bool os) {
    struct MethodLayout { DWORD table, count, owner, entry; };
    const MethodLayout layout = os ? MethodLayout{0x98, 0xc0, 8, 0} :
                                            MethodLayout{0x88, 0xc2, 0, 8};
    WORD count = 0;
    const uintptr_t table = pointer(klass + layout.table);
    if (!table || !bytes(&count, klass + layout.count, sizeof(count))) return 0;

    auto matches = [&](uintptr_t info) {
        BYTE parameters = 255;
        return info && pointer(info + layout.owner) == klass &&
               pointer(info + layout.entry) == (uintptr_t)image + rva &&
               bytes(&parameters, info + 0x2e, 1) && parameters == 0;
    };
    if (!os) {
        if (count != cn_count) return 0;
        const uintptr_t info = pointer(table + cn_index * sizeof(uintptr_t));
        return matches(info) ? info : 0;
    }

    // The OS build reorders managed methods. Locate the exact native entry
    // once per class/table and revalidate the same live slot on future calls.
    struct Cache { uintptr_t klass, table, rva; WORD slot; };
    static Cache advance_cache{}, event_cache{};
    Cache& cache = rva == action_profile->free_click ? advance_cache : event_cache;
    if (!count || count > 512) return 0;
    if (cache.klass == klass && cache.table == table && cache.rva == rva && cache.slot < count) {
        const uintptr_t info = pointer(table + cache.slot * sizeof(uintptr_t));
        if (matches(info)) return info;
    }
    for (WORD slot = 0; slot < count; ++slot) {
        const uintptr_t info = pointer(table + slot * sizeof(uintptr_t));
        if (matches(info)) {
            cache = {klass, table, rva, slot};
            return info;
        }
    }
    return 0;
}

static uintptr_t advance_info(const Root &root,uintptr_t context) {
    if (!context || pointer(context + (overseas() ? 0x220 : 0x278)) != root.managed) return 0;
    if (!overseas() && (!named(context, CONTEXT_CLASS) || pointer(context + 0x68) != root.managed)) return 0;

    const uintptr_t action = current_action(context);
    const uintptr_t model = pointer(action + 32);
    if (!named(action, "Action") || !model ||
        pointer(action + 16) != (uintptr_t)image + action_profile->advance_model) return 0;
    if (!overseas() && (!named(model, "MFCLKHPEEMK") || pointer(context + 0x218) != model)) return 0;

    return verified_method(pointer(context), action_profile->free_click, CONTEXT_METHOD_INDEX, CONTEXT_METHOD_COUNT, overseas());
}
static uintptr_t button_context(const Root &root,uintptr_t button) {
    if(!named(button,"Button")&&!named(button,"MihoyoButton"))return 0;
    uintptr_t event=pointer(button+216);
    if(!named(event,"ButtonClickedEvent"))return 0;
    uintptr_t calls=pointer(event+16),list=pointer(calls+24),array=pointer(list+16);
    if(!named(calls,"InvokableCallList")||!named(list,"List`1")||u32(list+24)!=1||pointer(array+24)<1)return 0;
    uintptr_t invokable=pointer(array+32),delegate=pointer(invokable+16),closure=pointer(delegate+32);
    if(!named(invokable,"InvokableCall")||!named(delegate,"UnityAction")||
        pointer(delegate+16)!=(uintptr_t)image+action_profile->click_closure||!closure)return 0;
    if(!overseas()&&!named(closure,CLICK_CLOSURE_CLASS))return 0;
    uintptr_t inner=pointer(closure+16),context=pointer(inner+32);
    return named(inner,"UnityAction")&&advance_info(root,context)?context:0;
}
static uintptr_t find_context(const Root &root) {
    if(advance_info(root,cached_context))return cached_context;
    cached_context=0;
    struct Node {uintptr_t transform;DWORD depth;};Node queue[128]={};DWORD count=1,index=0;
    queue[0]={pointer(pointer(root.go+0x40)+8),0};
    while(index<count) {
        Node node=queue[index++];uintptr_t transform=node.transform;
        if(pointer(transform)!=(uintptr_t)image+action_profile->rect_vtable)continue;
        uintptr_t go=pointer(transform+0x40),components=pointer(go+0x40);DWORD size=u32(go+0x48);
        if(!components||size>32)continue;
        for(DWORD i=0;i<size;i++) {
            uintptr_t native=pointer(components+i*16+8);
            if(pointer(native)!=(uintptr_t)image+action_profile->mono_vtable||u32(native+0x20)!=2||pointer(native+0x40)!=go)continue;
            uintptr_t object=pointer(native+0x28);
            if(pointer(object+16)!=native)continue;
            uintptr_t context=button_context(root,object);
            if(context){cached_context=context;return context;}
        }
        uintptr_t children=pointer(transform+0x80),length=pointer(transform+0x90),capacity=pointer(transform+0x98);
        if(node.depth>=10||!children||length>capacity||capacity>128)continue;
        for(uintptr_t i=0;i<length&&count<128;i++) {
            uintptr_t child=pointer(children+i*8);
            if(pointer(child+0xa0)!=transform)continue;
            bool visited=false;for(DWORD j=0;j<count;j++)if(queue[j].transform==child){visited=true;break;}
            if(!visited)queue[count++]={child,node.depth+1};
        }
    }
    return 0;
}
static uintptr_t first_button(const Root &root,uintptr_t *item,DWORD *count) {
    uintptr_t group=pointer(root.managed+72),reusable=pointer(group+40),list=pointer(reusable+64);
    if(!named(group,"MonoGrpSelect")||!component(group)||!named(reusable,"MonoReusableList")||!component(reusable)||!named(list,"List`1"))return 0;
    *count=u32(list+24);uintptr_t array=pointer(list+16);
    if(!*count||*count>16||!named(array,"MonoReusableListItem[]")||pointer(array+24)<*count)return 0;
    *item=pointer(array+32);
    if(!named(*item,"MonoSelectItem")||!component(*item)||u32(*item+80)!=0)return 0;
    uintptr_t button=pointer(*item+40);
    return named(button,"MihoyoButton")&&component(button)?button:0;
}
static uintptr_t event_info(uintptr_t event) {
    if(!named(event,"ButtonClickedEvent"))return 0;
    uintptr_t klass=pointer(event),arena=pointer(pointer(arena_slot));
    for(DWORD depth=0;klass&&arena&&depth<4;depth++) {
        if (named_class(klass, "UnityEvent")) {
            return verified_method(klass, action_profile->event_invoke, 6, 7, overseas());
        }
        DWORD relative=u32(klass+(overseas()?0xa4:0xb0));if(!relative||relative==0xffffffff)return 0;
        uintptr_t parent=arena+relative;if(parent==klass)return 0;klass=parent;
    }
    return 0;
}
static void choice_text(uintptr_t item) {
    memset(state.text,0,sizeof(state.text));uintptr_t text=pointer(item+32),value=pointer(text+224);
    if(!named(text,"Text")||!named(value,"String"))return;
    DWORD length=u32(value+16);wchar_t buffer[64]={};
    if(length&&length<64&&bytes(buffer,value+20,length*2))WideCharToMultiByte(CP_UTF8,0,buffer,(int)length,state.text,127,nullptr,nullptr);
}
static bool invocation_matches(uintptr_t rva) {
    BYTE actual[64];
    for (SIZE_T i = 0; i < action_profile->check_count; ++i) {
        const auto& check = action_profile->checks[i];
        if (check.rva == rva && check.size == sizeof(actual)) {
            return bytes(actual, (uintptr_t)image + rva, sizeof(actual)) &&
                   memcmp(actual, check.bytes, sizeof(actual)) == 0;
        }
    }
    return false;
}
static DWORD invoke_game(uintptr_t target,uintptr_t info,uintptr_t rva) {
    __try {((void(__fastcall*)(void*,void*))(image+rva))((void*)target,(void*)info);return 0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode();}
}
static bool read_command(Shared *command) {
    LONG before=shared->command;MemoryBarrier();memcpy(command,shared,56);MemoryBarrier();
    return before>0&&!(before&1)&&before==shared->command&&command->enabled<=1&&
        command->advance<=1&&command->choose<=1&&command->skip<=1&&command->stop<=1&&
        GetTickCount64()>=command->command_tick&&GetTickCount64()-command->command_tick<=400;
}
static void publish() {
    state.cooldown_active=(DWORD)istaroth_cooldown_enabled;
    state.cooldown_seen=(DWORD)istaroth_cooldown_seen;state.cooldown_bypassed=(DWORD)istaroth_cooldown_bypassed;
    state.outer_seen=(DWORD)istaroth_outer_seen;state.outer_bypassed=(DWORD)istaroth_outer_bypassed;
    state.auto_cooldown_seen=(DWORD)istaroth_auto_cooldown_seen;state.auto_cooldown_bypassed=(DWORD)istaroth_auto_cooldown_bypassed;
    InterlockedIncrement(&shared->sequence);MemoryBarrier();state.heartbeat=GetTickCount64();
    memcpy(shared,&state,16);
    memcpy((BYTE*)shared+60,(BYTE*)&state+60,sizeof(state)-60);
    MemoryBarrier();InterlockedIncrement(&shared->sequence);
}
static uint64_t choice_key(const Root &root,DWORD count) {
    uintptr_t group=pointer(root.managed+72),list=pointer(pointer(group+40)+64),array=pointer(list+16);
    uint64_t hash=root.native^(uint64_t(root.id)<<32)^count;
    for(DWORD i=0;i<count;i++) {
        uintptr_t item=pointer(array+32+i*8),value=pointer(pointer(item+32)+224);
        DWORD length=u32(value+16);wchar_t text[512];
        if(!named(value,"String")||length>512||!bytes(text,value+20,length*2))return 0;
        for(DWORD j=0;j<length;j++)hash=(hash^text[j])*1099511628211ULL;
        hash=(hash^i)*1099511628211ULL;
    }
    return hash?hash:1;
}
static void resolve_gate(uint64_t now,Root *root,uintptr_t *context) {
    if(InterlockedExchange(&gate_invalidated,0)) {gate_root={};gate_context=0;state.stable_since=0;state.gate_epoch++;}
    state.engine_tid=GetCurrentThreadId();state.window_tid=(DWORD)window_thread;
    state.foreground=foreground();state.gate_valid=0;state.root_native=state.root_managed=0;
    DWORD reason=state.engine_tid!=state.window_tid?1:!state.foreground?16:find_root(root);
    if(!reason) {*context=find_context(*root);if(!*context)reason=8;}
    if(reason) {
        if(gate_root.native){state.gate_epoch++;gate_root={};gate_context=0;}
        state.stable_since=0;state.gate_reason=reason;clear_cooldown();return;
    }
    state.root_native=root->native;state.root_managed=root->managed;
    if(root->native!=gate_root.native||root->managed!=gate_root.managed||root->id!=gate_root.id||*context!=gate_context) {
        state.gate_epoch++;gate_root=*root;gate_context=*context;state.stable_since=now;
    }
    state.gate_valid=now-state.stable_since>=200;state.gate_reason=state.gate_valid?0:17;
    if(!state.gate_valid)clear_cooldown();
}
static void tick() {
    uint64_t now=GetTickCount64();if(now-last_poll<50)return;last_poll=now;
    InterlockedExchange64(&last_gate_check,(LONG64)now);
    Root root={};uintptr_t context=0;resolve_gate(now,&root,&context);
    istaroth::ContinueKey continue_key{};
    auto continue_observation=find_continue(&continue_key,now);
    continue_latch.Observe(continue_observation,continue_key,now,false);
    Shared command={};state.status=1;state.reason=0;state.option_count=0;
    if(!read_command(&command)){clear_cooldown();state.reason=10;publish();return;}
    state.ack_command=(DWORD)command.command;
    if(command.stop){clear_cooldown();state.status=7;state.reason=14;InterlockedExchange(&running,0);publish();return;}
    if(!command.enabled){clear_cooldown();publish();return;}
    state.status=2;
    if(state.error){clear_cooldown();state.status=3;state.reason=13;publish();return;}
    if(!state.gate_valid){clear_cooldown();state.reason=state.gate_reason;publish();return;}
    if(command.epoch!=state.gate_epoch){clear_cooldown();state.reason=19;publish();return;}
    if(state.attempts>=command.limit){clear_cooldown();state.reason=5;publish();return;}
    uintptr_t model=pointer(current_action(context)+32);
    if(istaroth_cooldown_model!=model) {
        clear_cooldown();InterlockedExchangePointer((PVOID volatile*)&istaroth_cooldown_model,(PVOID)model);
    }
    InterlockedExchange(&istaroth_cooldown_enabled,(LONG)command.skip);
    BYTE line=255;if(!bytes(&line,root.managed+256,1)||line>1){clear_cooldown();state.reason=8;publish();return;}
    uintptr_t button=0,item=0,event=0,info=0,rva=0;DWORD kind=0,count=0;uint64_t key=0;
    button=first_button(root,&item,&count);
    bool options=button&&one(button+176)&&one(button+192);
    state.option_count=options?count:0;
    if(continue_observation==istaroth::ContinueObservation::Waiting) {
        clear_cooldown();
        if(line||options){state.reason=27;publish();return;}
        if(!command.advance){state.reason=6;publish();return;}
        // The research probe identified this reference in the real notebook.
        // The existing UnityEvent metadata/byte guards validate its invocation.
        button=pointer(continue_key.managed+72);event=pointer(button+216);
        info=event_info(event);rva=action_profile->event_invoke;kind=3;item=0;count=0;
    } else if(line==1&&!options) {
#ifdef ISTAROTH_CURTAIN_SHADOW
        clear_cooldown();state.reason=28;publish();return;
#endif
        pending_choice=0;
        if(!command.advance){state.reason=6;publish();return;}
        item=0;count=0;
        button=context;info=advance_info(root,context);event=context;rva=action_profile->free_click;kind=1;
    } else {
#ifdef ISTAROTH_CURTAIN_SHADOW
        clear_cooldown();state.reason=28;publish();return;
#endif
        if(!command.choose){state.reason=7;publish();return;}
        event=pointer(button+216);info=event_info(event);rva=action_profile->event_invoke;kind=2;
        key=choice_key(root,count);
        if(key&&key==pending_choice){state.reason=18;publish();return;}
    }
    if(now-state.last_tick<50){state.reason=9;publish();return;}
    bool valid=kind==1?advance_info(root,button)==info:component(button)&&one(button+176)&&one(button+192)&&
        (kind==3?(named(button,"Button")&&pointer(continue_key.managed+72)==button&&
                   named(continue_key.managed,"MonoClickContinue")&&component(continue_key.managed)&&
                   pointer(continue_key.managed+16)==continue_key.native&&pointer(continue_key.native+0x28)==continue_key.managed&&
                   one(continue_key.native+0x48)&&u32(continue_key.native+8)==continue_key.instance):key!=0);
    if(!button||!valid||!info||!invocation_matches(rva)) {
        state.reason=8;publish();return;
    }
    BYTE current_line=255;bytes(&current_line,root.managed+256,1);
    if(!live_root(root)||!foreground()||shared->stop||command.command!=shared->command||current_line!=line||
        (kind!=1&&pointer(button+216)!=event)||(kind==1&&!advance_info(root,button))||
        GetTickCount64()-shared->command_tick>400){clear_cooldown();state.reason=15;publish();return;}
    if(kind==3&&!continue_latch.Observe(continue_observation,continue_key,now,true)){state.reason=21;publish();return;}
    state.last_kind=kind;state.last_index=kind==2?0:0xffffffff;state.last_tick=now;
    state.last_button=button;state.last_event=event;
    if(item)choice_text(item);else memset(state.text,0,sizeof(state.text));
#ifdef ISTAROTH_CURTAIN_SHADOW
    if(kind==3){state.reason=26;publish();return;}
#endif
    // Only our validated automatic call may omit the outer click-time waits.
    // Other game callers retain their original time and readiness checks.
    if(kind==1) {
        InterlockedExchange(&istaroth_advance_tid,(LONG)GetCurrentThreadId());
        InterlockedExchangePointer((PVOID volatile*)&istaroth_advance_context,(PVOID)context);
    }
    state.attempts++;state.error=invoke_game(event,info,rva);
    InterlockedExchangePointer((PVOID volatile*)&istaroth_advance_context,nullptr);
    InterlockedExchange(&istaroth_advance_tid,0);
    if(!state.error){state.completed++;state.reason=kind==3?22:kind==1?11:12;if(kind==2){pending_choice=key;state.choice_total++;}else state.advance_total++;}
    else {clear_cooldown();state.status=3;state.reason=13;}
    publish();
}
#ifndef ISTAROTH_ACTIONS_TEST
static void __fastcall game_update(void *self,void *method) {
    original_update(self,method);
    if(running&&named((uintptr_t)self,"MainThreadDispatcher"))tick();
}
static BOOL CALLBACK find_window(HWND hwnd,LPARAM) {
    DWORD pid=0,tid=GetWindowThreadProcessId(hwnd,&pid);wchar_t name[64]={};
    if(pid==GetCurrentProcessId()&&IsWindowVisible(hwnd)&&GetClassNameW(hwnd,name,64)&&!lstrcmpW(name,L"UnityWndClass")) {game_window=hwnd;InterlockedExchange(&window_thread,(LONG)tid);}
    return TRUE;
}
static LONG attach() {
    original_update=(Update)(image+action_profile->update);
    istaroth_cooldown_original=image+action_profile->cooldown_jump;istaroth_cooldown_next=image+action_profile->cooldown_jump+6;
    istaroth_outer_a_original=image+action_profile->outer_a_compare;istaroth_outer_a_next=image+action_profile->outer_a_compare+9;
    istaroth_outer_b_original=image+action_profile->outer_b_compare;istaroth_outer_b_next=image+action_profile->outer_b_compare+9;
    LONG error=DetourTransactionBegin();if(error)return error;
    error=DetourUpdateThread(GetCurrentThread());
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0),handles[512]={};DWORD count=0;
    THREADENTRY32 entry={sizeof(entry)};
    if(snapshot==INVALID_HANDLE_VALUE){DetourTransactionAbort();return GetLastError();}
    if(!error&&!Thread32First(snapshot,&entry))error=GetLastError();
    if(!error)do {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId()||entry.th32ThreadID==GetCurrentThreadId())continue;
        if(count==512){error=ERROR_BUFFER_OVERFLOW;break;}
        HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!thread){error=GetLastError();break;}
        handles[count++]=thread;error=DetourUpdateThread(thread);if(error)break;
    }while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(!error)error=DetourAttach((PVOID*)&original_update,game_update);
    if(!error)error=DetourAttach(&istaroth_cooldown_original,IstarothCooldownGate);
    if(!error)error=DetourAttach(&istaroth_outer_a_original,overseas()?IstarothOsOuterA:IstarothOuterA);
    if(!error)error=DetourAttach(&istaroth_outer_b_original,overseas()?IstarothOsOuterB:IstarothOuterB);
    if(error)DetourTransactionAbort();else error=DetourTransactionCommit();
    for(DWORD i=0;i<count;i++)CloseHandle(handles[i]);
    return error;
}
static DWORD WINAPI worker(void*) {
    wchar_t name[96];swprintf_s(name,L"Local\\Istaroth.DialogueActions.%lu",GetCurrentProcessId());
    SetLastError(0);HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name);
    if(!mapping||GetLastError()==ERROR_ALREADY_EXISTS){if(mapping)CloseHandle(mapping);FreeLibraryAndExitThread(own_module,1);}
    shared=(Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
    if(!shared){CloseHandle(mapping);FreeLibraryAndExitThread(own_module,2);}
    state.magic=0x49534131;state.version=4;state.pid=GetCurrentProcessId();state.last_index=0xffffffff;
    publish();
    image=(BYTE*)GetModuleHandleW(nullptr);arena_slot=(uintptr_t)image+action_profile->class_arena_slot;
    if (__readgsdword(0x48) != GetCurrentThreadId() ||
        !IstarothValidateChecks(image, game_build, action_profile->checks, action_profile->check_count)) {
        state.status=3;state.error=ERROR_REVISION_MISMATCH;publish();ExitThread(3);
    }
    swprintf_s(name,L"Local\\GenshinSkip.Story.%lu",GetCurrentProcessId());
    HANDLE story_mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
    story=story_mapping?(BYTE*)MapViewOfFile(story_mapping,FILE_MAP_READ,0,0,19904):nullptr;
    if(!story){state.status=3;state.error=ERROR_FILE_NOT_FOUND;publish();ExitThread(4);}
    state.error=attach();
    if(state.error){state.status=3;publish();ExitThread(5);}
    InterlockedExchange(&running,1);
    while(running) {
        EnumWindows(find_window,0);Shared command={};
        bool valid=read_command(&command),lost=!foreground()||GetTickCount64()-(uint64_t)last_gate_check>250;
        if(!valid||lost)InterlockedExchange(&gate_invalidated,1);
        if(!valid||!command.enabled||lost)clear_cooldown();
        Sleep(25);
    }
    clear_cooldown();
    // Keep the inert hook, mapping and module valid until the game exits normally.
    ExitThread(0);
}
#endif
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*) {
#ifdef ISTAROTH_ACTIONS_TEST
    (void)module;(void)reason;return TRUE;
#else
    if(reason==DLL_PROCESS_ATTACH) {
        game_build = IstarothCurrentBuild();
        if (!game_build) return FALSE;
        action_profile = game_build->overseas ? &os_action_profile : &cn_action_profile;
        own_module=module;DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
    }
    return TRUE;
#endif
}
#ifdef ISTAROTH_ACTIONS_TEST
extern "C" __declspec(dllexport) DWORD ContinueSelfTest() {
    alignas(8) BYTE klass[208]={},object[32]={},native[80]={},go[512]={},probe[19904]={};
    char name[64]="MonoClickContinue";image=(BYTE*)0x100000;
    uintptr_t value=(uintptr_t)name;memcpy(klass+0x70,&value,8);
    value=(uintptr_t)klass;memcpy(object,&value,8);
    value=(uintptr_t)native;memcpy(object+16,&value,8);
    value=(uintptr_t)image+action_profile->mono_vtable;memcpy(native,&value,8);
    DWORD id=42,kind=2;memcpy(native+8,&id,4);memcpy(native+0x20,&kind,4);
    value=(uintptr_t)object;memcpy(native+0x28,&value,8);
    value=(uintptr_t)go;memcpy(native+0x40,&value,8);native[0x48]=go[0x1f8]=1;
    DWORD magic=0x47535331,version=7,pid=GetCurrentProcessId(),status=2,count=1;
    uint64_t now=GetTickCount64();memcpy(probe,&magic,4);memcpy(probe+4,&version,4);
    memcpy(probe+8,&pid,4);memcpy(probe+12,&status,4);memcpy(probe+20,&count,4);memcpy(probe+40,&now,8);
    BYTE *record=probe+56;value=(uintptr_t)native;memcpy(record,&value,8);
    value=(uintptr_t)object;memcpy(record+8,&value,8);memcpy(record+16,&id,4);
    strcpy_s((char*)record+28,64,name);story=probe;continue_latch={};
    istaroth::ContinueKey key{};using O=istaroth::ContinueObservation;DWORD result=0;
    if(find_continue(&key,now)==O::Waiting&&key.instance==42)result|=1;
    native[0x48]=0;if(find_continue(&key,now)==O::Unknown)result|=2;native[0x48]=1;
    continue_latch.submitted=key;go[0x1f8]=0;
    if(find_continue(&key,now)==O::Unknown&&!continue_latch.submitted.Valid())result|=4;go[0x1f8]=1;
    count=2;memcpy(probe+20,&count,4);memcpy(record+896,record,896);
    if(find_continue(&key,now)==O::Unknown)result|=8;count=1;memcpy(probe+20,&count,4);
    uint64_t stale=now-500;memcpy(probe+40,&stale,8);
    if(find_continue(&key,now)==O::Unknown)result|=16;memcpy(probe+40,&now,8);
    pid++;memcpy(probe+8,&pid,4);if(find_continue(&key,now)==O::Unknown)result|=32;pid--;memcpy(probe+8,&pid,4);
    DWORD origin=24;memcpy(record+24,&origin,4);if(find_continue(&key,now)==O::Unknown)result|=64;origin=0;memcpy(record+24,&origin,4);
    id++;memcpy(native+8,&id,4);if(find_continue(&key,now)==O::Unknown)result|=128;id--;memcpy(native+8,&id,4);
    go[0x1f8]=255;if(find_continue(&key,now)==O::Unknown)result|=256;go[0x1f8]=1;
    count=0;memcpy(probe+20,&count,4);if(find_continue(&key,now)==O::Unknown)result|=512;count=1;memcpy(probe+20,&count,4);
    version=8;memcpy(probe+4,&version,4);if(find_continue(&key,now)==O::Unknown)result|=1024;
    story=nullptr;continue_latch={};return result;
}
static bool outer_fixture(void **original,void **next,void *hook,void *compare,void *continuation,
                          DWORD(*fixture)(float,float,uintptr_t)) {
    *original=compare;*next=continuation;
    if(DetourTransactionBegin())return false;
    LONG error=DetourUpdateThread(GetCurrentThread());
    if(!error)error=DetourAttach(original,hook);
    if(error){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit())return false;
    DWORD seen=(DWORD)istaroth_outer_seen,bypassed=(DWORD)istaroth_outer_bypassed;
    alignas(8) BYTE data[900]={};uintptr_t context=(uintptr_t)data;
    istaroth_advance_context=context;istaroth_advance_tid=(LONG)GetCurrentThreadId();istaroth_cooldown_enabled=0;
    bool passed=fixture(1,2,context)==0x100;
    istaroth_cooldown_enabled=1;
    union {DWORD bits;float value;} unordered={0x7fc00000};
    passed=passed&&fixture(1,2,context)==0x101&&fixture(1,1,context)==0x4001&&
        fixture(2,1,context)==1&&fixture(unordered.value,1,context)==0x4500&&fixture(1,2,context+8)==0x100;
    istaroth_advance_tid++;
    passed=passed&&fixture(1,2,context)==0x100;
    clear_cooldown();
    passed=passed&&fixture(1,2,context)==0x100&&
        (DWORD)istaroth_outer_seen-seen==3&&(DWORD)istaroth_outer_bypassed-bypassed==2;
    bool detached=!DetourTransactionBegin()&&!DetourUpdateThread(GetCurrentThread())&&
        !DetourDetach(original,hook)&&!DetourTransactionCommit();
    return passed&&detached&&fixture(1,2,context)==0x100;
}
extern "C" __declspec(dllexport) DWORD OuterWaitSelfTest() {
    if(__readgsdword(0x48)!=GetCurrentThreadId())return 0;
    bool a=outer_fixture(&istaroth_outer_a_original,&istaroth_outer_a_next,(void*)IstarothOuterA,
        (void*)IstarothOuterCompareA,(void*)IstarothOuterNextA,IstarothOuterFixtureA);
    bool b=outer_fixture(&istaroth_outer_b_original,&istaroth_outer_b_next,(void*)IstarothOuterB,
        (void*)IstarothOuterCompareB,(void*)IstarothOuterNextB,IstarothOuterFixtureB);
    const bool os_a = outer_fixture(&istaroth_outer_a_original, &istaroth_outer_a_next,
        (void*)IstarothOsOuterA, (void*)IstarothOsOuterCompareA, (void*)IstarothOsOuterNextA, IstarothOsOuterFixtureA);
    const bool os_b = outer_fixture(&istaroth_outer_b_original, &istaroth_outer_b_next,
        (void*)IstarothOsOuterB, (void*)IstarothOsOuterCompareB, (void*)IstarothOsOuterNextB, IstarothOsOuterFixtureB);
    return (a ? 1u : 0u) | (b ? 2u : 0u) | (os_a ? 4u : 0u) | (os_b ? 8u : 0u);
}
extern "C" __declspec(dllexport) DWORD OverseasMethodSelfTest() {
    alignas(uintptr_t) BYTE klass_data[0xc8] = {}, info_data[0x38] = {};
    const uintptr_t klass = (uintptr_t)klass_data;
    const uintptr_t info = (uintptr_t)info_data;
    uintptr_t methods[3] = {0, info, 0};
    const uintptr_t table = (uintptr_t)methods;
    const WORD count = ARRAYSIZE(methods);
    image = (BYTE*)0x100000;
    const uintptr_t entry = (uintptr_t)image + action_profile->event_invoke;
    memcpy(klass_data + 0x98, &table, sizeof(table));
    memcpy(klass_data + 0xc0, &count, sizeof(count));
    memcpy(info_data, &entry, sizeof(entry));
    memcpy(info_data + 8, &klass, sizeof(klass));

    DWORD result = 0;
    auto lookup = [&] { return verified_method(klass, action_profile->event_invoke, 6, 7, true); };
    if (lookup() == info) result |= 1;
    methods[1] = 0;
    methods[2] = info;
    if (lookup() == info) result |= 2;
    info_data[0x2e] = 1;
    if (!lookup()) result |= 4;
    info_data[0x2e] = 0;
    memset(info_data + 8, 0, sizeof(klass));
    if (!lookup()) result |= 8;
    return result;
}
extern "C" __declspec(dllexport) DWORD CooldownSelfTest() {
    istaroth_cooldown_original=(void*)IstarothFixtureJump;istaroth_cooldown_next=(void*)IstarothFixtureNext;
    if(DetourTransactionBegin())return 0;
    LONG error=DetourUpdateThread(GetCurrentThread());
    if(!error)error=DetourAttach(&istaroth_cooldown_original,IstarothCooldownGate);
    if(error){DetourTransactionAbort();return 0;}
    if(DetourTransactionCommit())return 0;
    istaroth_cooldown_model=123;istaroth_cooldown_enabled=0;DWORD result=0;
    union {DWORD bits;float value;} unordered={0x7fc00000};
    if(IstarothCooldownFixture(2,1,123)==1&&IstarothCooldownFixture(1,2,123)==0x100&&
       IstarothCooldownFixture(1,1,123)==0x4000&&IstarothCooldownFixture(unordered.value,1,123)==0x4500)result|=1;
    istaroth_cooldown_enabled=1;
    if(IstarothCooldownFixture(1,2,123)==0x101&&IstarothCooldownFixture(1,1,123)==0x4001&&
       IstarothCooldownFixture(unordered.value,1,123)==0x4501)result|=2;
    if(IstarothCooldownFixture(1,2,456)==0x100)result|=4;
    DWORD auto_seen=(DWORD)istaroth_auto_cooldown_seen,auto_bypassed=(DWORD)istaroth_auto_cooldown_bypassed;
    istaroth_advance_context=123;istaroth_advance_tid=(LONG)GetCurrentThreadId();istaroth_cooldown_enabled=0;
    bool source=IstarothCooldownFixture(1,2,123)==0x100;
    istaroth_cooldown_enabled=1;source=source&&IstarothCooldownFixture(1,2,123)==0x101;
    istaroth_advance_tid++;source=source&&IstarothCooldownFixture(1,2,123)==0x101;
    istaroth_advance_context=0;source=source&&IstarothCooldownFixture(1,2,123)==0x101;
    if(source&&(DWORD)istaroth_auto_cooldown_seen-auto_seen==2&&
       (DWORD)istaroth_auto_cooldown_bypassed-auto_bypassed==1)result|=32;
    clear_cooldown();if(IstarothCooldownFixture(1,2,123)==0x100)result|=8;
    if(!DetourTransactionBegin()&&!DetourUpdateThread(GetCurrentThread())&&
       !DetourDetach(&istaroth_cooldown_original,IstarothCooldownGate)&&!DetourTransactionCommit()&&
       IstarothCooldownFixture(1,1,123)==0x4000)result|=16;
    return result;
}
extern "C" __declspec(dllexport) DWORD CombinedGuardSelfTest() {
    Shared mailbox={},command={};shared=&mailbox;mailbox.command=2;mailbox.command_tick=GetTickCount64();DWORD flags=0;
    if(read_command(&command))flags|=1;
    mailbox.command=3;if(!read_command(&command))flags|=2;mailbox.command=4;
    mailbox.skip=2;if(!read_command(&command))flags|=4;mailbox.skip=0;
    mailbox.command_tick=GetTickCount64()-500;if(!read_command(&command))flags|=8;
    mailbox.command_tick=GetTickCount64()+1000;if(!read_command(&command))flags|=16;
    alignas(8) BYTE root_data[320]={},group[64]={},reusable[80]={},list[32]={},array[48]={},item[88]={},text[232]={},value[64]={},klass[208]={};
    char name[64]="String";uintptr_t p=(uintptr_t)group;memcpy(root_data+72,&p,8);
    p=(uintptr_t)reusable;memcpy(group+40,&p,8);p=(uintptr_t)list;memcpy(reusable+64,&p,8);
    p=(uintptr_t)array;memcpy(list+16,&p,8);p=(uintptr_t)item;memcpy(array+32,&p,8);
    p=(uintptr_t)text;memcpy(item+32,&p,8);p=(uintptr_t)value;memcpy(text+224,&p,8);
    p=(uintptr_t)klass;memcpy(value,&p,8);p=(uintptr_t)name;memcpy(klass+0x70,&p,8);
    DWORD length=1;memcpy(value+16,&length,4);value[20]='A';
    Root root={0x100000,(uintptr_t)root_data,0,42};uint64_t first=choice_key(root,1);
    if(first&&first==choice_key(root,1))flags|=32;
    value[20]='B';if(first!=choice_key(root,1))flags|=64;
    value[20]='A';root.id++;if(first!=choice_key(root,1))flags|=128;
    length=513;memcpy(value+16,&length,4);if(!choice_key(root,1))flags|=256;
    shared=nullptr;return flags;
}
extern "C" __declspec(dllexport) DWORD ActionsSelfTest() {
    alignas(8) BYTE klass[208]={},object[320]={},native[128]={},go[512]={};
    char class_text[64]="MonoTalkDialog";image=(BYTE*)0x100000;
    uintptr_t value=(uintptr_t)class_text;memcpy(klass+0x70,&value,8);
    value=(uintptr_t)klass;memcpy(object,&value,8);
    value=(uintptr_t)native;memcpy(object+16,&value,8);
    value=(uintptr_t)image+action_profile->mono_vtable;memcpy(native,&value,8);
    DWORD id=42,kind=2;memcpy(native+8,&id,4);memcpy(native+0x20,&kind,4);
    value=(uintptr_t)object;memcpy(native+0x28,&value,8);
    value=(uintptr_t)go;memcpy(native+0x40,&value,8);go[0x1f8]=1;
    Root root={(uintptr_t)native,(uintptr_t)object,(uintptr_t)go,42};DWORD flags=0;
    if(live_root(root))flags|=1;
    id=43;memcpy(native+8,&id,4);if(!live_root(root))flags|=2;
    id=42;memcpy(native+8,&id,4);go[0x1f8]=0;if(!live_root(root))flags|=4;go[0x1f8]=1;
    value=0;memcpy(native+0x28,&value,8);if(!live_root(root))flags|=8;
    value=(uintptr_t)object;memcpy(native+0x28,&value,8);
    alignas(8) BYTE classes[1024]={},event[32]={},info[56]={};uint64_t table[7]={};
    char event_name[64]="ButtonClickedEvent",parent_name[64]="UnityEvent";
    value=(uintptr_t)event_name;memcpy(classes+0x70,&value,8);
    value=(uintptr_t)parent_name;memcpy(classes+512+0x70,&value,8);
    DWORD relative=512;memcpy(classes+0xb0,&relative,4);
    value=(uintptr_t)classes;memcpy(event,&value,8);
    uintptr_t arena=(uintptr_t)classes,slot=(uintptr_t)&arena;arena_slot=(uintptr_t)&slot;
    value=(uintptr_t)table;memcpy(classes+512+0x88,&value,8);WORD count=7;memcpy(classes+512+0xc2,&count,2);
    table[6]=(uintptr_t)info;value=(uintptr_t)classes+512;memcpy(info,&value,8);
    value=(uintptr_t)image+action_profile->event_invoke;memcpy(info+8,&value,8);
    if(event_info((uintptr_t)event)==(uintptr_t)info)flags|=16;
    info[0x2e]=1;if(!event_info((uintptr_t)event))flags|=32;info[0x2e]=0;
    value++;memcpy(info+8,&value,8);if(!event_info((uintptr_t)event))flags|=64;
    BYTE raw=0;if(!bytes(&raw,0,1)&&!bytes(&raw,(uintptr_t)object,19905))flags|=128;
    alignas(8) BYTE fake_story[19904]={};DWORD magic=0x47535331,version=7,pid=GetCurrentProcessId(),records=1;
    uint64_t heartbeat=GetTickCount64();memcpy(fake_story,&magic,4);memcpy(fake_story+4,&version,4);
    memcpy(fake_story+8,&pid,4);DWORD probe_status=2;memcpy(fake_story+12,&probe_status,4);memcpy(fake_story+20,&records,4);memcpy(fake_story+40,&heartbeat,8);
    BYTE *record=fake_story+56;value=(uintptr_t)native;memcpy(record,&value,8);value=(uintptr_t)object;memcpy(record+8,&value,8);
    id=42;memcpy(record+16,&id,4);strcpy_s((char*)record+28,64,"MonoTalkDialog");story=fake_story;
    Root found={};if(find_root(&found)==0&&found.managed==(uintptr_t)object)flags|=256;
    go[0x1f8]=0;if(find_root(&found)==3)flags|=512;go[0x1f8]=1;
    records=2;memcpy(fake_story+20,&records,4);memcpy(record+896,record,896);if(find_root(&found)==4)flags|=1024;
    alignas(8) BYTE context[900]={},context_class[208]={},model[32]={},model_class[208]={},action[112]={},action_class[208]={};
    char context_name[64]=CONTEXT_CLASS,model_name[64]="MFCLKHPEEMK",action_name[64]="Action";
    value=(uintptr_t)context_name;memcpy(context_class+0x70,&value,8);value=(uintptr_t)context_class;memcpy(context,&value,8);
    value=(uintptr_t)model_name;memcpy(model_class+0x70,&value,8);value=(uintptr_t)model_class;memcpy(model,&value,8);
    value=(uintptr_t)action_name;memcpy(action_class+0x70,&value,8);value=(uintptr_t)action_class;memcpy(action,&value,8);
    value=(uintptr_t)object;memcpy(context+104,&value,8);memcpy(context+632,&value,8);
    value=(uintptr_t)model;memcpy(context+536,&value,8);memcpy(action+32,&value,8);
    value=(uintptr_t)action;memcpy(context+584,&value,8);value=(uintptr_t)image+action_profile->advance_model;memcpy(action+16,&value,8);
    uint64_t context_methods[CONTEXT_METHOD_COUNT]={};context_methods[CONTEXT_METHOD_INDEX]=(uintptr_t)info;
    value=(uintptr_t)context_methods;memcpy(context_class+0x88,&value,8);count=CONTEXT_METHOD_COUNT;memcpy(context_class+0xc2,&count,2);
    value=(uintptr_t)context_class;memcpy(info,&value,8);value=(uintptr_t)image+action_profile->free_click;memcpy(info+8,&value,8);
    if(advance_info(root,(uintptr_t)context)==(uintptr_t)info)flags|=2048;
    value=0;memcpy(context+104,&value,8);if(!advance_info(root,(uintptr_t)context))flags|=4096;
    value=(uintptr_t)object;memcpy(context+104,&value,8);
    value=0;memcpy(action+32,&value,8);if(!advance_info(root,(uintptr_t)context))flags|=8192;
    value=(uintptr_t)model;memcpy(action+32,&value,8);info[0x2e]=1;
    if(!advance_info(root,(uintptr_t)context))flags|=16384;
    return flags;
}
#endif
