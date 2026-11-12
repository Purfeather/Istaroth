#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cwchar>

namespace istaroth {
struct ClockShared {
    DWORD magic,version,pid,status;
    volatile LONG command_seq,enable,requested_milli,stop;
    LONG error,lease_active;
    float scale,baseline;
    uint64_t heartbeat;
    double clocks[4];
    volatile LONG sequence;
    LONG restores,ack;
};
struct ActionShared {
    DWORD magic,version,pid,status;
    volatile LONG command;
    DWORD enabled,advance,choose,skip,stop,limit,epoch;
    uint64_t command_tick;
    volatile LONG sequence;
    DWORD reason;
    uint64_t heartbeat;
    DWORD engine_tid,window_tid,attempts,completed,last_kind,last_index,option_count,ack;
    DWORD gate_valid,foreground,gate_epoch,cooldown_active,error,gate_reason;
    uint64_t last_tick,last_button,last_event,root_native,root_managed,stable_since;
    DWORD cooldown_seen,cooldown_bypassed;
    char text[128];
    DWORD choices,advances,outer_seen,outer_bypassed,auto_seen,auto_bypassed;
};
static_assert(sizeof(ClockShared)==104&&offsetof(ClockShared,sequence)==88,"time ABI1");
static_assert(sizeof(ActionShared)==336&&offsetof(ActionShared,sequence)==56&&offsetof(ActionShared,text)==184,"actions ABI4");

constexpr DWORD RuntimeMagic=0x49535231,RuntimeVersion=1;
enum RuntimeFlag : DWORD {
    Initialized=1,MenuOpen=2,Enabled=4,Foreground=8,Dialogue=16,GateReady=32,
    ActualKnown=64,SpeedActive=128,EffectsActive=256,Advance=512,Choose=1024,Skip=2048,
    AdvanceActive=4096,ChooseActive=8192,SkipActive=16384,Fixture=32768,BlurReady=65536,StartedPaused=131072
};
struct RuntimeView {
    DWORD magic,version,size,pid,status,error;
    volatile LONG sequence;
    DWORD flags;
    uint64_t birth,heartbeat,controller_tick,present_tick;
    DWORD target,gate_reason,gate_epoch,attempts,completed,choices,outer_bypassed,auto_bypassed;
    float actual,baseline,field_scale;
    DWORD clock_status,action_status,phase;
    char reason[128];
};
static_assert(sizeof(RuntimeView)==248&&offsetof(RuntimeView,sequence)==24,"runtime ABI1");

template<class T> bool ReadSnapshot(const T* source,T& result,size_t sequence_offset) {
    if(!source)return false;
    auto sequence=(const volatile LONG*)((const BYTE*)source+sequence_offset);
    for(unsigned i=0;i<8;i++) {
        LONG before=*sequence;if(before&1){YieldProcessor();continue;}
        MemoryBarrier();memcpy(&result,source,sizeof(T));MemoryBarrier();
        if(before==*sequence)return true;
    }
    return false;
}
inline bool Fresh(uint64_t now,uint64_t value,uint64_t age) {return value&&now>=value&&now-value<=age;}
inline bool Acknowledged(DWORD observed,DWORD required) {return required&&(DWORD)(observed-required)<0x80000000u;}
}
