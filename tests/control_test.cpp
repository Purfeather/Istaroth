#define WIN32_LEAN_AND_MEAN
#include "resident_control.h"
#include <cstdio>
#include <limits>
using namespace istaroth;
static unsigned checks=0;
#define CHECK(value) do{checks++;if(!(value)){printf("failed line %u: %s\n",__LINE__,#value);return 1;}}while(0)
int main() {
    ClockShared c{};c.magic=0x47535431;c.version=1;c.pid=42;c.status=1;c.scale=1;c.heartbeat=1000;
    ActionShared a{};a.magic=0x49534131;a.version=4;a.pid=42;a.status=1;a.heartbeat=1000;a.gate_valid=1;a.foreground=1;a.gate_epoch=7;a.root_native=123;
    ResidentControl controller;Desired d;
    auto run=[&](uint64_t now=1000,bool front=true,bool rendered=true,bool initialized=true){return controller.Evaluate(d,&c,&a,42,now,front,rendered,initialized);};
    CHECK(!run().permit&&!run().clock_enabled);
    controller.ClockSent(false,1,0);controller.ActionsSent(false,d,0,0);
    CHECK(run().ui.phase==UiPhase::Restoring); // Components have not opened their maps yet.
    controller.ClockSent(false,1,2);controller.ActionsSent(false,d,7,2);c.ack=2;a.ack=2;
    CHECK(run().ui.phase==UiPhase::Paused&&run().ui.actual_known&&run().ui.actual==1);
    controller.ActionsSent(false,d,8,4); // A gate epoch change cannot restart an already-paused ACK.
    CHECK(run().ui.phase==UiPhase::Paused);
    d.enabled=true;CHECK(run().clock_enabled&&!run().permit);
    controller.ClockSent(true,4,4);CHECK(!run().permit);
    c.ack=4;c.status=2;c.lease_active=1;c.baseline=1;c.scale=4;
    CHECK(run().permit&&run().ui.speed_active&&run().ui.actual==4);
    a.option_count=2;a.cooldown_active=1;a.status=2;
    CHECK(run().ui.choose_active&&!run().ui.advance_active&&run().ui.skip_active);
    a.option_count=0;CHECK(run().ui.advance_active&&!run().ui.choose_active);
    CHECK(!run(1000,false).permit&&!run(1000,false).clock_enabled&&run(1000,false).ui.enabled);
    CHECK(!run(1000,true,false).permit&&!run(1000,true,true,false).permit);
    CHECK(!run(1300).permit); // Expired main-thread heartbeat, even while the clock is alive.
    controller.ClockSent(false,1,6);controller.ActionsSent(false,d,7,6);c.ack=6;c.status=1;c.lease_active=0;c.scale=1;a.ack=6;a.status=1;a.cooldown_active=0;
    d.enabled=false;CHECK(run().ui.phase==UiPhase::Paused&&run().ui.target==4&&!run().ui.effects_active);
    d.enabled=true;d.target=1;CHECK(run().permit&&!run().clock_enabled&&run().ui.effects_active);
    a.heartbeat=1300;CHECK(!run(1000).permit);a.heartbeat=1000;
    a.version=3;CHECK(run().error==ERROR_REVISION_MISMATCH&&!run().permit);a.version=4;
    controller.fault=0;d.target=5;CHECK(run().error==ERROR_INVALID_PARAMETER);d.target=4;
    controller.fault=0;c.scale=0;CHECK(!run().error&&!run().permit&&!run().clock_enabled&&!run().ui.actual_known);
    d.enabled=false;CHECK(run().ui.phase==UiPhase::Paused&&!run().ui.actual_known);d.enabled=true;
    c.scale=-1;CHECK(run().error==ERROR_INVALID_DATA);c.scale=1;
    controller.fault=0;c.scale=std::numeric_limits<float>::quiet_NaN();CHECK(run().error==ERROR_INVALID_DATA&&!run().ui.actual_known);c.scale=1;
    controller.fault=0;c.status=6;CHECK(run().error&&!run().permit&&!run().ui.actual_known);c.status=1;
    CHECK(run().error); // Ownership failure is latched; disabling cannot invent a restored rate.
    controller.fault=0;c.status=2;c.lease_active=1;c.baseline=0;CHECK(run().error==ERROR_INVALID_DATA);c.baseline=1;
    controller.fault=0;c.status=4;c.error=ERROR_WRITE_FAULT;CHECK(run().error==ERROR_WRITE_FAULT&&run().ui.phase==UiPhase::Fault);
    CHECK(Acknowledged(2,0xfffffffeu)&&!Acknowledged(0xfffffffeu,2)&&!Acknowledged(0,0));
    RuntimeView snapshot{};snapshot.sequence=2;snapshot.target=3;RuntimeView copy{};
    CHECK(ReadSnapshot(&snapshot,copy,offsetof(RuntimeView,sequence))&&copy.target==3);
    snapshot.sequence=3;CHECK(!ReadSnapshot(&snapshot,copy,offsetof(RuntimeView,sequence)));
    {
        ClockShared exited{};exited.magic=0x47535431;exited.version=1;exited.pid=42;
        exited.status=7;exited.scale=1;exited.heartbeat=1000;exited.ack=2;
        ActionShared live{};live.magic=0x49534131;live.version=4;live.pid=42;live.status=1;
        live.heartbeat=10000;live.gate_valid=live.foreground=1;live.gate_epoch=7;live.ack=2;
        Desired request;request.enabled=true;ResidentControl terminal;
        terminal.ClockSent(false,1,2);terminal.ActionsSent(false,request,7,2);
        auto evaluate=[&](){return terminal.Evaluate(request,&exited,&live,42,10000,true,true,true);};
        auto result=evaluate();
        CHECK(result.error==ERROR_INVALID_STATE&&result.ui.phase==UiPhase::Fault&&!result.permit&&!result.clock_enabled&&!result.ui.actual_known);
        request.enabled=false;CHECK(evaluate().ui.phase==UiPhase::Fault); // Never pretend a dead worker is restoring.
        request.enabled=true;exited.status=1;exited.heartbeat=10000;
        CHECK(evaluate().error==ERROR_INVALID_STATE&&!evaluate().permit); // A fault requires normal restart.
        terminal=ResidentControl();terminal.ClockSent(false,1,2);terminal.ActionsSent(false,request,7,2);
        exited.heartbeat=8999;CHECK(!evaluate().error&&!evaluate().clock_enabled); // Nonterminal staleness can recover.
        exited.heartbeat=10000;CHECK(!evaluate().error&&evaluate().clock_enabled);
        terminal=ResidentControl();exited.status=7;exited.heartbeat=10000;
        CHECK(evaluate().error==ERROR_INVALID_STATE); // Fresh and stale exit snapshots agree.
        for(unsigned invalid=0;invalid<3;invalid++) {
            terminal=ResidentControl();ClockShared untrusted=exited;untrusted.heartbeat=1000;
            if(invalid==0)untrusted.magic=0;else if(invalid==1)untrusted.version=2;else untrusted.pid=43;
            auto rejected=terminal.Evaluate(request,&untrusted,&live,42,10000,true,true,true);
            CHECK(!rejected.error&&!rejected.permit&&!rejected.clock_enabled);
        }
        terminal=ResidentControl();exited.heartbeat=0;CHECK(!evaluate().error&&!evaluate().permit);
        exited.heartbeat=10001;CHECK(!evaluate().error&&!evaluate().permit); // Future time is not an exit receipt.
        exited.status=1;exited.heartbeat=10000;live.status=7;live.heartbeat=1000;
        CHECK(evaluate().error==ERROR_INVALID_STATE); // The action worker has the same terminal boundary.
        terminal=ResidentControl();live.pid=43;
        CHECK(!evaluate().error&&!evaluate().permit);
        live.pid=42;live.heartbeat=10001;
        CHECK(!evaluate().error&&!evaluate().permit);
    }
    printf("{\"passed\":true,\"checks\":%u,\"clock_size\":%zu,\"actions_size\":%zu,\"runtime_size\":%zu}\n",checks,sizeof(c),sizeof(a),sizeof(snapshot));
    return 0;
}
