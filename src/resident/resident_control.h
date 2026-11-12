#pragma once
#include "resident_protocol.h"
#include "resident_ui.h"
#include <cmath>

namespace istaroth {
struct Desired {
    bool enabled=false,advance=true,choose=true,skip=true;
    unsigned target=4;
};
struct ControlResult {
    UiState ui;
    bool clock_enabled=false,permit=false;
    unsigned multiplier=1;
    DWORD epoch=0,error=0;
};
// The resident and the owned integration host use the same transition/ACK logic.
class ResidentControl {
    bool clock_requested=false,have_clock=false;
    unsigned clock_target=1;
    DWORD clock_transition=0,action_transition=0;
    uint64_t action_signature=~uint64_t(0);
public:
    DWORD fault=0;
    void ClockSent(bool enabled,unsigned target,DWORD sequence) {
        if(!sequence)return;
        if(!have_clock||clock_requested!=enabled||clock_target!=target) {
            clock_requested=enabled;clock_target=target;clock_transition=sequence;have_clock=true;
        }
    }
    void ActionsSent(bool permit,const Desired& d,DWORD epoch,DWORD sequence) {
        if(!sequence)return;
        uint64_t signature=(uint64_t(permit?epoch:0)<<8)|(permit?1u:0u)|(d.advance?2u:0u)|(d.choose?4u:0u)|(d.skip?8u:0u);
        if(signature!=action_signature){action_signature=signature;action_transition=sequence;}
    }
    ControlResult Evaluate(const Desired& d,const ClockShared* c,const ActionShared* a,
                           DWORD pid,uint64_t now,bool foreground,bool rendered,bool initialized) {
        ControlResult result;auto& u=result.ui;
        u.target=d.target;u.enabled=d.enabled;u.advance=d.advance;u.choose_first=d.choose;u.skip_cooldown=d.skip;
        u.initialized=initialized;u.foreground=foreground;
        bool valid_target=d.target>=1&&d.target<=4;
        bool clock_identity=c&&c->magic==0x47535431&&c->version==1&&c->pid==pid;
        bool clock_live=clock_identity&&Fresh(now,c->heartbeat,1000);
        bool clock_usable=clock_live&&(c->status==1||c->status==2)&&std::isfinite(c->scale)&&c->scale>=0;
        bool time=clock_usable&&c->scale>0;
        bool action_identity=a&&a->magic==0x49534131&&a->version==4&&a->pid==pid;
        bool actions_live=action_identity&&Fresh(now,a->heartbeat,250);
        bool actions=actions_live&&(a->status==1||a->status==2);
        if(c&&Fresh(now,c->heartbeat,1000)&&(c->magic!=0x47535431||c->version!=1||c->pid!=pid))fault=ERROR_REVISION_MISMATCH;
        if(a&&Fresh(now,a->heartbeat,250)&&(a->magic!=0x49534131||a->version!=4||a->pid!=pid))fault=ERROR_REVISION_MISMATCH;
        if(!valid_target)fault=ERROR_INVALID_PARAMETER;
        // Exit is irreversible for these loaded workers. Its last publication
        // remains authoritative after the heartbeat expires, unlike live state.
        if(clock_identity&&c->status==7&&c->heartbeat&&c->heartbeat<=now)fault=c->error?c->error:ERROR_INVALID_STATE;
        if(action_identity&&a->status==7&&a->heartbeat&&a->heartbeat<=now)fault=a->error?a->error:ERROR_INVALID_STATE;
        if(clock_live&&(c->error||c->status==3||c->status==4||c->status==6))fault=c->error?c->error:ERROR_INVALID_STATE;
        if(clock_live&&(c->status==1||c->status==2)&&!clock_usable)fault=ERROR_INVALID_DATA;
        if(clock_live&&c->lease_active&&(!std::isfinite(c->baseline)||c->baseline<=0))fault=ERROR_INVALID_DATA;
        if(actions_live&&(a->error||a->status==3))fault=a->error?a->error:ERROR_INVALID_STATE;
        if(actions_live&&a->status>3&&a->status!=7)fault=ERROR_INVALID_DATA;
        bool gate=time&&actions&&a->gate_valid==1&&a->foreground==1&&foreground&&rendered&&initialized&&!fault;
        result.epoch=actions?a->gate_epoch:0;
        u.dialogue=actions&&a->root_native;u.actual_known=time&&!fault;
        if(u.actual_known) {
            if(c->lease_active&&std::isfinite(c->baseline)&&c->baseline>0) {
                u.actual=c->scale/c->baseline;u.baseline=c->baseline;
            } else if(!c->lease_active&&c->status==1) {u.actual=1;u.baseline=c->scale;}
            else u.actual_known=false;
        }
        result.clock_enabled=d.enabled&&gate&&valid_target&&d.target>1;
        result.multiplier=result.clock_enabled?d.target:1;
        bool clock_ack=clock_usable&&have_clock&&clock_requested==result.clock_enabled&&clock_target==result.multiplier&&
            Acknowledged((DWORD)c->ack,clock_transition);
        bool confirmed=clock_ack&&(result.clock_enabled?
            c->lease_active&&u.actual_known&&std::fabs(u.actual-(float)d.target)<.02f:!c->lease_active);
        result.permit=d.enabled&&gate&&confirmed;
        u.speed_active=time&&c->lease_active&&u.actual_known&&u.actual>1.01f;
        u.advance_active=result.permit&&d.advance&&a->option_count==0;
        u.choose_active=result.permit&&d.choose&&a->option_count>0;
        u.skip_active=result.permit&&a->cooldown_active==1;
        u.effects_active=u.speed_active||(result.permit&&(d.advance||d.choose||d.skip));
        bool stopped=confirmed&&actions&&!a->cooldown_active&&a->status==1&&Acknowledged(a->ack,action_transition);
        if(fault){u.phase=UiPhase::Fault;strcpy_s(u.reason,u8"组件异常，已停止请求；请正常重启并加载");}
        else if(!initialized){u.phase=UiPhase::Starting;strcpy_s(u.reason,u8"正在初始化游戏内组件");}
        else if(!d.enabled){u.phase=stopped?UiPhase::Paused:UiPhase::Restoring;strcpy_s(u.reason,stopped?(time?u8"已暂停，已确认恢复原速":u8"已暂停，游戏时钟处于暂停状态"):u8"正在停止操作并确认恢复原速");}
        else if(!gate) {
            u.phase=actions&&a->gate_reason==17?UiPhase::Confirming:UiPhase::Waiting;
            strcpy_s(u.reason,!foreground?u8"游戏处于后台，等待返回后重验":!rendered?u8"等待界面恢复":
                !time?u8"等待游戏时钟恢复":!actions?u8"游戏更新暂停，等待重新确认":a->gate_reason==17?u8"正在确认对话":u8"等待可操作的常规对话");
        } else if(!result.permit){u.phase=UiPhase::Confirming;strcpy_s(u.reason,u8"正在确认实际倍率");}
        else {u.phase=UiPhase::Active;strcpy_s(u.reason,u8"常规对话控制中");}
        result.error=fault;return result;
    }
};
}
