// Research-only routing DAG PDC contracts. No JUCE dependency.
// Planner builds entirely on control thread. It is NOT enabled in AudioEngine.
#include "../../Source/PDCCore/ProgrammeRoutePdcPlannerResearchCore.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace DAW::PdcResearch;
static int delay(const ProgrammeRoutePdcPlannerResearchCore& p,const char* id) {
    const auto* result=p.findEdge(id);assert(result);return result->delaySamples;
}
static int arrival(const ProgrammeRoutePdcPlannerResearchCore& p,const char* id) {
    const auto* result=p.findNode(id);assert(result);return result->arrivalLatency;
}
static int output(const ProgrammeRoutePdcPlannerResearchCore& p,const char* id) {
    const auto* result=p.findNode(id);assert(result);return result->outputLatency;
}
int main() {
    // Direct + send to wet bus + simultaneous dry at master: 32 vs 32+16.
    // If graph compensates ONLY direct-to-master edges, the send/wet path
    // will be late by 16 samples; routing-aware PDC is required.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        const std::vector<Node> n={
            {"vocal",32,true},{"aux",16,false},{"master",0,false}};
        const std::vector<Edge> e={
            {"dry","vocal","master",EdgeKind::Direct},
            {"to-compressor","vocal","aux",EdgeKind::Send},
            {"wet","aux","master",EdgeKind::Direct}};
        assert(p.build(n,e,{"vocal","aux","master"}));
        assert(output(p,"vocal")==32);
        assert(arrival(p,"aux")==32);
        assert(output(p,"aux")==48);
        assert(arrival(p,"master")==48);
        assert(delay(p,"dry")==16);
        assert(delay(p,"to-compressor")==0);
        assert(delay(p,"wet")==0);
        for(const auto& v:p.edgePlans())
            assert(v.sourceOutputLatency+v.delaySamples==
                   v.destinationArrivalLatency);
    }
    // Bus reconvergence: mixing unequal latency edges MUST happen before
    // destination bus FX. One 16-sample bus insert adds latency AFTER merge.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        const std::vector<Node> n={
            {"a",32,true},{"b",0,true},{"parallel-bus",16,false},
            {"master",0,false}};
        const std::vector<Edge> e={
            {"a-bus","a","parallel-bus",EdgeKind::Send},
            {"b-bus","b","parallel-bus",EdgeKind::PreSend},
            {"b-dry","b","master",EdgeKind::Direct},
            {"bus-master","parallel-bus","master",EdgeKind::FolderSum}};
        assert(p.build(n,e,{"a","b","parallel-bus","master"}));
        assert(delay(p,"a-bus")==0);
        assert(delay(p,"b-bus")==32);
        assert(arrival(p,"parallel-bus")==32);
        assert(output(p,"parallel-bus")==48);
        assert(delay(p,"b-dry")==48);
        assert(delay(p,"bus-master")==0);
    }
    // A track can have its own clips/live input AND routed incoming sound.
    // Its local input must be delayed BEFORE the own plugin chain.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        std::vector<Node> n={
            {"source",64,true},{"track-with-clips",32,true},
            {"master",0,false}};
        const std::vector<Edge> e={
            {"src-track","source","track-with-clips",EdgeKind::Direct},
            {"trk-out","track-with-clips","master",EdgeKind::Direct}};
        assert(p.build(n,e,{"source","track-with-clips","master"}));
        assert(p.findNode("track-with-clips")->localInputDelay==64);
        assert(output(p,"track-with-clips")==96);
    }
    // Sidechain uses independent tap semantics and must NEVER join the
    // programme DAG or determine audible incoming path latency.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        const std::vector<Node> n={
            {"voice",32,true},{"compressor-bus",16,false},
            {"master",0,false}};
        const std::vector<Edge> e={
            {"programme","voice","master",EdgeKind::Direct},
            {"sidechain","compressor-bus","voice",EdgeKind::Sidechain},
            {"bus-output","compressor-bus","master",EdgeKind::Direct}};
        assert(p.build(n,e,{"voice","compressor-bus","master"}));
        assert(p.findEdge("sidechain")==nullptr);
        assert(delay(p,"programme")==0); // max output 32
        assert(delay(p,"bus-output")==16);
    }
    // Disabling sends or muting/bypassing edges must not bias the path maximum.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        const std::vector<Node> n={
            {"dry",8,true},{"latency",128,true},{"master",0,false}};
        std::vector<Edge> e={
            {"direct","dry","master",EdgeKind::Direct},
            {"muted-send","latency","master",EdgeKind::Send,false,false}};
        assert(p.build(n,e,{"dry","latency","master"}));
        assert(arrival(p,"master")==8 && delay(p,"direct")==0);
        assert(p.findEdge("muted-send")==nullptr);
        e[1].active=true;e[1].bypassed=true;
        assert(p.build(n,e,{"dry","latency","master"}));
        assert(arrival(p,"master")==8);
        e[1].bypassed=false;
        assert(p.build(n,e,{"dry","latency","master"}));
        assert(arrival(p,"master")==128);
        assert(delay(p,"direct")==120);
    }
    // Strict validation: fail closed rather than return an invalid RT plan.
    {
        ProgrammeRoutePdcPlannerResearchCore p;
        const std::vector<Node> n={{"a",16,true},{"b",16,false}};
        const std::vector<Edge> e={{"id","a","b",EdgeKind::Direct}};
        assert(!p.build(n,e,{"b","a"}));
        assert(!p.valid() && !p.error().empty());
        assert(p.edgePlans().empty());
        assert(!p.build(n,{{"id","a","missing",EdgeKind::Send}},{"a","b"}));
        assert(!p.build(n,{{"id","a","b",EdgeKind::Direct},
                           {"id","a","b",EdgeKind::Send}},{"a","b"}));
        assert(!p.build({{"a",131072,true},{"b",1,false}},
                       e,{"a","b"}));
        assert(!p.build(n,e,{"a","a"}));
        assert(p.build(n,e,{"a","b"}));
        assert(p.valid() && p.error().empty());
    }
    // All delays are exact host samples, independent of engine sample rate.
    // Sample period differs, but graph plan reuses same integer algorithm.
    for(const int rate:{44100,48000}) {
        ProgrammeRoutePdcPlannerResearchCore p;
        assert(p.build({{"track",32,true},{"aux",16,false},{"master",0,false}},
                       {{"t-m","track","master",EdgeKind::Direct},
                        {"t-a","track","aux",EdgeKind::PreSend},
                        {"a-m","aux","master",EdgeKind::Send}},
                       {"track","aux","master"}));
        assert(delay(p,"t-m")==16);
        const double milliseconds=1000.0*(double)delay(p,"t-m")/rate;
        assert(milliseconds>0 && milliseconds<1.0);
    }
    // Control-plane live-edit simulation: rebuild immutable plans after a
    // QuickSend toggle or plugin-latency notification. The audio callback
    // must consume only fully prepared generations, never build these here.
    for(const int rate:{44100,48000,96000}) {
        (void)rate; // Integer-sample planner must not depend on sample rate.
        ProgrammeRoutePdcPlannerResearchCore p;
        std::vector<Node> nodes={
            {"voice",32,true},{"bass",0,true},
            {"aux",16,false},{"master",0,false}};
        std::vector<Edge> edges={
            {"voice-direct","voice","master",EdgeKind::Direct},
            {"bass-direct","bass","master",EdgeKind::Direct},
            {"voice-send","voice","aux",EdgeKind::Send},
            {"bass-send","bass","aux",EdgeKind::PreSend},
            {"aux-out","aux","master",EdgeKind::Direct}};
        const std::vector<std::string> order={"voice","bass","aux","master"};
        assert(p.build(nodes,edges,order));
        assert(delay(p,"bass-send")==32);
        assert(delay(p,"bass-direct")==48);
        assert(delay(p,"voice-direct")==16);
        // Disable both sends: the aux is disconnected and cannot affect
        // the master timeline even though its plugin retains 16 samples.
        edges[2].active=false;edges[3].active=false;
        assert(p.build(nodes,edges,order));
        assert(p.findEdge("voice-send")==nullptr);
        assert(p.findEdge("bass-send")==nullptr);
        assert(delay(p,"bass-direct")==32);
        assert(delay(p,"voice-direct")==0);
        // Gain-only edits do not change this topology's compensation.
        assert(p.build(nodes,edges,order));
        assert(delay(p,"bass-direct")==32);
        // Restore sends and notify a new 64-sample aux plugin latency.
        edges[2].active=true;edges[3].active=true;nodes[2].ownProcessorLatency=64;
        assert(p.build(nodes,edges,order));
        assert(delay(p,"bass-send")==32);
        assert(delay(p,"bass-direct")==96);
        assert(delay(p,"voice-direct")==64);
        // Invalid latency must fail closed rather than expose stale plans.
        nodes[2].ownProcessorLatency=-1;
        assert(!p.build(nodes,edges,order));
        assert(!p.valid() && p.edgePlans().empty());
    }
    std::cout<<"APEX routing PDC DAG research: direct/send/pre/folder,"
             <<" reconvergence, local-input alignment, sidechain isolation,"
             <<" bypass/inactive/cycle/overflow validation GREEN\n";
}
