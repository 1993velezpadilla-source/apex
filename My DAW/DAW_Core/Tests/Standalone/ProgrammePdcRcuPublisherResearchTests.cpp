// Pure C++17 lifecycle gate: audio callback MUST NEVER free a retired bank.
// The caller is single-writer on the control thread. Concurrent Reader
// acquisition is permitted; DSP mutation is single-audio-writer only.
#include "../../Source/PDCCore/ProgrammePdcRcuPublisherResearchCore.h"
#include "../../Source/PDCCore/ProgrammeRoutePdcPreparedResearchCore.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace DAW::PdcResearch;
struct State {
    int generation=0;
    std::atomic<int>* destroyed=nullptr;
    explicit State(int gen,std::atomic<int>* count):generation(gen),destroyed(count){}
    ~State(){if(destroyed)destroyed->fetch_add(1,std::memory_order_relaxed);}
};
static void lifetime() {
    std::atomic<int> destroyed{0};
    ProgrammePdcRcuPublisherResearchCore<State> pub;
    {
        auto empty=pub.readRealtime();
        assert(!empty);
    }
    assert(pub.publishPrepared(std::make_unique<State>(1,&destroyed)));
    {
        auto old=pub.readRealtime();
        assert(old && old->generation==1);
        assert(pub.activeReaders()==1);
        assert(pub.publishPrepared(std::make_unique<State>(2,&destroyed)));
        // The old Reader still sees its OWN valid original state.
        assert(old->generation==1);
        assert(pub.collectRetired()==0);
        assert(destroyed.load(std::memory_order_relaxed)==0);
        {
            auto fresh=pub.readRealtime();
            assert(fresh && fresh->generation==2);
            assert(pub.activeReaders()==2);
        }
        assert(destroyed.load(std::memory_order_relaxed)==0);
    }
    assert(pub.activeReaders()==0);
    assert(destroyed.load(std::memory_order_relaxed)==0);
    assert(pub.collectRetired()==1);
    assert(destroyed.load(std::memory_order_relaxed)==1);
    {
        auto fresh=pub.readRealtime();
        assert(fresh->generation==2);
    }
}
static void boundedConcurrentRetirement() {
    std::atomic<int> destroyed{0};
    ProgrammePdcRcuPublisherResearchCore<State> pub;
    assert(pub.publishPrepared(std::make_unique<State>(0,&destroyed)));
    std::atomic<bool> entered{false},leave{false},exited{false};
    std::thread audio([&]{
        auto read=pub.readRealtime();
        assert(read && read->generation==0);
        entered.store(true,std::memory_order_release);
        while(!leave.load(std::memory_order_acquire))
            std::this_thread::yield();
        // Control published many generations while this callback held its
        // snapshot. It MUST still own and read the original generation.
        assert(read->generation==0);
        exited.store(true,std::memory_order_release);
    });
    while(!entered.load(std::memory_order_acquire))
        std::this_thread::yield();
    for(int i=1;i<=64;++i)
        assert(pub.publishPrepared(std::make_unique<State>(i,&destroyed)));
    assert(pub.retiredCountControlOnly()==64);
    assert(pub.activeReaders()==1);
    assert(!pub.publishPrepared(std::make_unique<State>(65,&destroyed)));
    assert(pub.retiredCountControlOnly()==64);
    // Rejected candidate is destroyed on CONTROL thread (the source of one
    // destruction below); none of the 64 retired banks may be destroyed yet.
    assert(destroyed.load(std::memory_order_relaxed)==1);
    assert(pub.collectRetired()==0);
    leave.store(true,std::memory_order_release);
    audio.join();
    assert(exited.load(std::memory_order_acquire));
    assert(pub.collectRetired()==64);
    assert(destroyed.load(std::memory_order_relaxed)==65);
    assert(pub.publishPrepared(std::make_unique<State>(66,&destroyed)));
    {
        auto read=pub.readRealtime();
        assert(read && read->generation==66);
    }
    assert(pub.collectRetired()==1); // generation 64
    assert(destroyed.load(std::memory_order_relaxed)==66);
}
struct BankState {
    std::uint64_t version=0;
    ProgrammeRoutePdcPreparedResearchCore bank;
};
static void snapshotAudioGating() {
    ProgrammeRoutePdcPlannerResearchCore plan;
    assert(plan.build({{"vocal",32,true},{"aux",16,false},{"master",0,false}},
        {{"direct","vocal","master",EdgeKind::Direct},
         {"send","vocal","aux",EdgeKind::Send},
         {"wet","aux","master",EdgeKind::Direct}},
        {"vocal","aux","master"}));
    auto original=std::make_unique<BankState>();
    original->version=12;
    assert(original->bank.prepare(plan,2));
    const auto index=original->bank.findEdgeIndex("direct");
    assert(original->bank.edgeDelaySamples(index)==16);
    ProgrammePdcRcuPublisherResearchCore<BankState> pub;
    assert(pub.publishPrepared(std::move(original)));
    {
        auto scope=pub.readRealtime();
        assert(scope && scope->version==12);
        float left[31]{},right[31]{};
        left[0]=.8f;right[0]=-.4f;
        float* stereo[]={left,right};
        assert(scope->bank.processEdge(index,stereo,2,31));
        assert(left[0]==0.f && right[0]==0.f);
        assert(left[16]==.8f && right[16]==-.4f);
        // New graph removes wet send; its direct branch no longer needs PDC.
        assert(plan.build({{"vocal",32,true},{"aux",16,false},{"master",0,false}},
            {{"direct","vocal","master",EdgeKind::Direct},
             {"send","vocal","aux",EdgeKind::Send,false},
             {"wet","aux","master",EdgeKind::Direct}},
            {"vocal","aux","master"}));
        auto updated=std::make_unique<BankState>();
        updated->version=13;
        assert(updated->bank.prepare(plan,2));
        assert(pub.publishPrepared(std::move(updated)));
        assert(scope->version==12);
        assert(scope->bank.edgeDelaySamples(index)==16);
        assert(pub.collectRetired()==0);
        auto newScope=pub.readRealtime();
        assert(newScope->version==13);
        assert(newScope->bank.edgeDelaySamples(
            newScope->bank.findEdgeIndex("direct"))==0);
    }
    assert(pub.collectRetired()==1);
}
int main() {
    lifetime();
    boundedConcurrentRetirement();
    snapshotAudioGating();
    std::cout<<"APEX PDC RCU: audio-scope preserves old graph, "
             <<"bounded control-plane retirement, 64 queued versions, "
             <<"zero destruction in active readers, exact route bank "
             <<"generation and fail-closed publication GREEN\n";
}
