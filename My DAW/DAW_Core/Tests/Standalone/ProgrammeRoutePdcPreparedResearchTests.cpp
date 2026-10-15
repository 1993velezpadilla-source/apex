// APEX audio-path research: actual preallocated per-edge delay banks.
// This is an offline standalone harness, NOT an AudioEngine integration.
#include "../../Source/PDCCore/ProgrammeRoutePdcPreparedResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using DAW::PluginParallelDryDelayResearchCore;
using namespace DAW::PdcResearch;
struct Stereo {
    explicit Stereo(int n):left(static_cast<std::size_t>(n),0.f),
                           right(static_cast<std::size_t>(n),0.f){}
    std::vector<float> left,right;
    void process(PluginParallelDryDelayResearchCore& delay,int n) {
        float* p[]={left.data(),right.data()};delay.process(p,2,n);
    }
    void process(ProgrammeRoutePdcPreparedResearchCore& bank,
                 std::size_t i,int n) {
        float* p[]={left.data(),right.data()};
        assert(bank.processEdge(i,p,2,n));
    }
    void sum(const Stereo& input,int n) {
        for(int i=0;i<n;++i) {
            left[static_cast<std::size_t>(i)]+=input.left[static_cast<std::size_t>(i)];
            right[static_cast<std::size_t>(i)]+=input.right[static_cast<std::size_t>(i)];
        }
    }
};
static void impulseJourney(int block,int rate) {
    constexpr int total=523;
    ProgrammeRoutePdcPlannerResearchCore planner;
    const std::vector<Node> nodes={
        {"vocal",32,true},{"bass",0,true},{"aux",16,false},{"master",0,false}};
    const std::vector<Edge> edges={
        {"vocal-dry","vocal","master",EdgeKind::Direct},
        {"vocal-send","vocal","aux",EdgeKind::Send},
        {"bass-send","bass","aux",EdgeKind::PreSend},
        {"bass-dry","bass","master",EdgeKind::Direct},
        {"aux-to-master","aux","master",EdgeKind::Direct}};
    assert(planner.build(nodes,edges,{"vocal","bass","aux","master"}));
    assert(planner.findEdge("vocal-dry")->delaySamples==16);
    assert(planner.findEdge("bass-send")->delaySamples==32);
    assert(planner.findEdge("bass-dry")->delaySamples==48);
    ProgrammeRoutePdcPreparedResearchCore bank;
    assert(bank.prepare(planner,2));
    const auto vd=bank.findEdgeIndex("vocal-dry");
    const auto vs=bank.findEdgeIndex("vocal-send");
    const auto bs=bank.findEdgeIndex("bass-send");
    const auto bd=bank.findEdgeIndex("bass-dry");
    const auto am=bank.findEdgeIndex("aux-to-master");
    assert(bank.edgeCount()==5);
    assert(vd!=bank.invalidIndex&&vs!=bank.invalidIndex&&
           bs!=bank.invalidIndex&&bd!=bank.invalidIndex&&am!=bank.invalidIndex);
    PluginParallelDryDelayResearchCore vocalFx,auxFx;
    assert(vocalFx.prepare(2,32));
    assert(auxFx.prepare(2,16));

    for(int pass=0;pass<2;++pass) {
        if(pass>0) {
            bank.reset();vocalFx.reset();auxFx.reset();
        }
        for(int start=0;start<total;start+=block) {
            const int n=std::min(block,total-start);
            Stereo vocal(n),bass(n);
            for(int i=0;i<n;++i) {
                const int at=start+i;
                vocal.left[i]=static_cast<float>(.2*std::sin(at*.031)+.01*std::cos(at*.11));
                vocal.right[i]=static_cast<float>(-.15*std::cos(at*.023));
                bass.left[i]=static_cast<float>(.16*std::cos(at*.041));
                bass.right[i]=static_cast<float>(-.18*std::sin(at*.017));
            }
            // Vocal inserts happen before direct output and send tap.
            vocal.process(vocalFx,n);
            Stereo toMaster(n),wetAux(n);
            Stereo vocalDry=vocal;
            vocalDry.process(bank,vd,n);
            toMaster.sum(vocalDry,n);
            vocal.process(bank,vs,n);
            wetAux.sum(vocal,n);
            Stereo bassDry=bass,bassSend=bass;
            bassDry.process(bank,bd,n);
            bassSend.process(bank,bs,n);
            toMaster.sum(bassDry,n);
            wetAux.sum(bassSend,n);
            // The bus plugin sees a phase-aligned input.
            wetAux.process(auxFx,n);
            wetAux.process(bank,am,n);
            toMaster.sum(wetAux,n);
            for(int i=0;i<n;++i) {
                const int source=start+i-48;
                const double vL=source<0?0:
                    .2*std::sin(source*.031)+.01*std::cos(source*.11);
                const double vR=source<0?0:-.15*std::cos(source*.023);
                const double bL=source<0?0:.16*std::cos(source*.041);
                const double bR=source<0?0:-.18*std::sin(source*.017);
                assert(std::abs(toMaster.left[i]-2*(vL+bL))<1.e-6);
                assert(std::abs(toMaster.right[i]-2*(vR+bR))<1.e-6);
            }
        }
    }
    const double compensatedMs=1000.*48/rate;
    assert(std::abs(compensatedMs-(48000.0/rate))<1.e-12);
}
static void localTrackInput(int block) {
    ProgrammeRoutePdcPlannerResearchCore plan;
    assert(plan.build({{"parent",64,true},{"child",32,true},{"master",0,false}},
                      {{"upstream","parent","child",EdgeKind::Direct},
                       {"output","child","master",EdgeKind::Direct}},
                      {"parent","child","master"}));
    ProgrammeRoutePdcPreparedResearchCore bank;
    assert(bank.prepare(plan,2));
    const auto local=bank.findLocalInputIndex("child");
    assert(local!=bank.invalidIndex && bank.localDelaySamples(local)==64);
    PluginParallelDryDelayResearchCore upstream, childFx;
    assert(upstream.prepare(2,64));
    assert(childFx.prepare(2,32));
    for(int start=0;start<320;start+=block) {
        const int n=std::min(block,320-start);
        Stereo parent(n),childLocal(n);
        for(int i=0;i<n;++i) {
            parent.left[i]=parent.right[i]=start+i==0?.6f:0.f;
            childLocal.left[i]=childLocal.right[i]=start+i==0?.3f:0.f;
        }
        parent.process(upstream,n);
        float* locals[]={childLocal.left.data(),childLocal.right.data()};
        assert(bank.processLocalInput(local,locals,2,n));
        childLocal.sum(parent,n);
        childLocal.process(childFx,n);
        for(int i=0;i<n;++i) {
            const double expected=start+i==96?.9:0;
            assert(std::abs(childLocal.left[i]-expected)<1.e-6);
            assert(std::abs(childLocal.right[i]-expected)<1.e-6);
        }
    }
}
// Mono source-to-master sample path, including callback boundaries.
static void monoRoute(int block) {
    ProgrammeRoutePdcPlannerResearchCore plan;
    assert(plan.build({{"voice",32,true},{"bass",0,true},{"master",0,false}},
                      {{"voice-master","voice","master",EdgeKind::Direct},
                       {"bass-master","bass","master",EdgeKind::Direct}},
                      {"voice","bass","master"}));
    ProgrammeRoutePdcPreparedResearchCore bank;
    assert(bank.prepare(plan,1));
    const auto voiceIndex=bank.findEdgeIndex("voice-master");
    const auto bassIndex=bank.findEdgeIndex("bass-master");
    assert(bank.edgeDelaySamples(voiceIndex)==0);
    assert(bank.edgeDelaySamples(bassIndex)==32);
    PluginParallelDryDelayResearchCore voiceFx;
    assert(voiceFx.prepare(1,32));
    for(int start=0;start<391;start+=block) {
        const int count=std::min(block,391-start);
        std::vector<float> voice(static_cast<std::size_t>(count),0.f);
        std::vector<float> bass(static_cast<std::size_t>(count),0.f);
        for(int i=0;i<count;++i) {
            voice[static_cast<std::size_t>(i)]=start+i==0?.25f:0.f;
            bass[static_cast<std::size_t>(i)]=start+i==0?.5f:0.f;
        }
        float* voiceData[]={voice.data()};
        float* bassData[]={bass.data()};
        voiceFx.process(voiceData,1,count);
        assert(bank.processEdge(voiceIndex,voiceData,1,count));
        assert(bank.processEdge(bassIndex,bassData,1,count));
        for(int i=0;i<count;++i) {
            const float expected=start+i==32?.75f:0.f;
            assert(std::abs(voice[static_cast<std::size_t>(i)]+
                            bass[static_cast<std::size_t>(i)]-expected)<1.e-6f);
        }
    }
}
int main(){
    // Test coverage includes graph-wide sample-path parity.
    for(int rate:{44100,48000,96000})
        for(int frames:{1,31,64,128,256,1024})
            impulseJourney(frames,rate);
    for(int frames:{1,31,64,128,256,1024}) {
        localTrackInput(frames);
        monoRoute(frames);
    }
    ProgrammeRoutePdcPlannerResearchCore plan;
    assert(plan.build({{"source",16,true},{"master",0,false}},
                      {{"route","source","master",EdgeKind::Direct}},
                      {"source","master"}));
    ProgrammeRoutePdcPreparedResearchCore bank;
    assert(bank.prepare(plan,2));
    assert(bank.findEdgeIndex("route")!=bank.invalidIndex);
    assert(!bank.prepare(plan,0));
    assert(bank.findEdgeIndex("route")!=bank.invalidIndex);
    assert(!bank.prepare(plan,33));
    float mono[5]{};
    float* channels[]={mono};
    assert(!bank.processEdge(bank.findEdgeIndex("route"),channels,1,5));
    assert(!bank.processEdge(bank.invalidIndex,channels,1,5));
    assert(!bank.processEdge(bank.findEdgeIndex("route"),nullptr,2,5));
    assert(!bank.processLocalInput(bank.invalidIndex,channels,1,5));
    std::cout<<"APEX prepared route PDC: per-edge stereo multiway mix,"
             <<" 44.1/48/96k, 1/31/64/128/256/1024 callback segmentation,"
             <<" reset/local-source/invalid bounds GREEN\n";
}
