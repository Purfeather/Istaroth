#include "curtain_advance.h"
#include <cassert>
#include <cstdio>
using namespace istaroth;
int main() {
    ContinueKey page{0x10000,0x20000,42},other{0x30000,0x40000,44};
    ContinueLatch latch;
    assert(!latch.Observe(ContinueObservation::Unknown,{},0,true));
    assert(!latch.Observe(ContinueObservation::Waiting,{},1000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,{1,2,42},1000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,page,1000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,page,1250,true));
    assert(!latch.Observe(ContinueObservation::Waiting,page,1499,true));
    assert(latch.Observe(ContinueObservation::Waiting,page,1500,true));
    for(unsigned i=0;i<100;i++)assert(!latch.Observe(ContinueObservation::Waiting,page,1501+i,true));
    // An ineffective click, stale sample, focus/menu/F10 change, or another
    // instance appearing cannot release the outstanding interaction.
    assert(!latch.Observe(ContinueObservation::Unknown,{},2000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,page,3000,false));
    assert(!latch.Observe(ContinueObservation::Waiting,page,4000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,other,5000,true));
    assert(!latch.Observe(ContinueObservation::Waiting,other,6000,true));
    assert(!latch.Observe(ContinueObservation::Inactive,other,6100,true));
    assert(!latch.Observe(ContinueObservation::Waiting,page,7000,true));
    assert(!latch.Observe(ContinueObservation::Inactive,page,7100,false));
    assert(!latch.Observe(ContinueObservation::Waiting,other,7200,false));
    assert(!latch.Observe(ContinueObservation::Waiting,other,7450,false));
    assert(!latch.Observe(ContinueObservation::Waiting,other,7700,false));
    assert(!latch.Observe(ContinueObservation::Waiting,other,7950,false));
    assert(!latch.Observe(ContinueObservation::Waiting,other,8000,false));
    assert(latch.Observe(ContinueObservation::Waiting,other,8001,true));
    assert(!latch.Observe(ContinueObservation::Waiting,other,9000,true));
    ContinueLatch interrupted;
    assert(!interrupted.Observe(ContinueObservation::Waiting,page,1000,true));
    assert(!interrupted.Observe(ContinueObservation::Unknown,{},1400,true));
    assert(!interrupted.Observe(ContinueObservation::Waiting,page,1500,true));
    assert(!interrupted.Observe(ContinueObservation::Waiting,page,1750,true));
    assert(!interrupted.Observe(ContinueObservation::Waiting,page,1999,true));
    assert(interrupted.Observe(ContinueObservation::Waiting,page,2000,true));
    ContinueLatch gap;
    assert(!gap.Observe(ContinueObservation::Waiting,page,1000,true));
    assert(!gap.Observe(ContinueObservation::Waiting,page,11000,true));
    assert(!gap.Observe(ContinueObservation::Waiting,page,11250,true));
    assert(gap.Observe(ContinueObservation::Waiting,page,11500,true));
    std::puts("Continue latch: stable wait, one request, no retry, unknown and permission guards passed");
}
