// PDC V2 mainline migration: real JUCE-hosted latency + AudioEngine
// Track/QuickSend/Aux/Master sample-accurate routing regression.
// All code is test-only, with shipping routing still gated off.
#include <JuceHeader.h>
#include "../../../Source/AudioEngineCore/AudioEngine.h"
#include "../../../Source/PDCCore/ProgrammeRoutePdcSnapshotHandoffResearchCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace {
class ExactHostDelayResearchPlugin final : public juce::AudioPluginInstance {
public:
    explicit ExactHostDelayResearchPlugin(int samples=32)
      : AudioPluginInstance(BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        latency_(samples) { jassert(samples==16 || samples==32); }
    const juce::String getName() const override {return "APEX Exact Latency Test";}
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name=getName();
        d.descriptiveName=getName();
        d.pluginFormatName="APEX Test";
        d.manufacturerName="APEX";
        d.version="1";
        d.uniqueId=0x454C3332;
        d.deprecatedUid=d.uniqueId;
        d.numInputChannels=2;
        d.numOutputChannels=2;
        d.isInstrument=false;
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sr,int block) override {
        setRateAndBufferSizeDetails(sr,block);
        reset();
        setLatencySamples(latency_);
    }
    void releaseResources() override {}
    // A real JUCE AudioPluginInstance changes its advertised latency while
    // remaining inserted. Models a VST3 lookahead/quality change without
    // structurally rebuilding PluginChainCore.
    void setResearchLatencyOnControlThread(int samples) {
        jassert(samples==16 || samples==32);
        if(samples!=16 && samples!=32)return;
        latency_=samples;
        reset();
        setLatencySamples(samples);
    }
    void reset() override {for(auto& v:delay_)v.fill(0.f);index_=0;}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {
        if(b.getNumChannels()<2)return;
        ++processCalls;
        float* l=b.getWritePointer(0);
        float* r=b.getWritePointer(1);
        for(int n=0;n<b.getNumSamples();++n) {
            const float oldL=delay_[0][index_],oldR=delay_[1][index_];
            delay_[0][index_]=l[n];delay_[1][index_]=r[n];
            l[n]=oldL;r[n]=oldR;
            index_=(index_+1)%static_cast<std::size_t>(latency_);
        }
    }
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 0.0;}
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Default";}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*,int) override {}
    int processCalls=0; // research test probe for actual 64-frame slicing
private:
    std::array<std::array<float,32>,2> delay_{};
    std::size_t index_=0;
    int latency_=32;
};

} // namespace

class PdcV2MainlineJuceTests final : public juce::UnitTest {
public:
    PdcV2MainlineJuceTests()
       : juce::UnitTest("PDC V2 Mainline JUCE routing", "APEX.PDCV2") {}
    void runTest() override {
        constexpr int sr=48000;
        juce::MidiBuffer midi;
        beginTest("Hosted JUCE plugin dynamic latency 16->32 restages actual PDC routes");
        for(const int frames:{31,64,128,256,512}) {
            DAW::TrackManager tracks;
            auto* voice=tracks.createTrack("JUCE PDC voice");
            auto* auxTrack=tracks.createTrack("JUCE PDC aux");
            auxTrack->setRole(DAW::TrackRole::Bus);
            DAW::RoutingGraph routing;
            auto* v=routing.addNode("voice",DAW::RoutingNodeType::Track,voice->getID());
            auto* b=routing.addNode("aux",DAW::RoutingNodeType::Bus,auxTrack->getID());
            auto* send=routing.connect(v->id,b->id,DAW::ConnectionType::Send);
            expect(send!=nullptr);
            if(!send)continue;
            auto chain=std::make_shared<DAW::PluginChainCore>();
            chain->prepare(48000.0,frames);
            auto probe=std::make_unique<ExactHostDelayResearchPlugin>(16);
            auto* hosted=probe.get();
            expectEquals(chain->appendPluginInstanceForTesting(std::move(probe)),0);
            expectEquals(chain->totalLatencySamples(),16);
            DAW::AudioEngine engine;
            engine.setSubsystems(&tracks,nullptr,nullptr,&routing);
            engine.prepare(48000.0,frames);
            auto chains=std::make_shared<DAW::AudioEngine::PluginChainSnapshotMap>();
            (*chains)[auxTrack->getID()]=chain;
            engine.publishPluginChainsSnapshot(chains);
            const auto publishLatencies=[&](){
                auto latency=std::make_shared<DAW::AudioEngine::ChainLatencyMap>();
                (*latency)[voice->getID()]=0;
                (*latency)[auxTrack->getID()]=chain->totalLatencySamples();
                engine.publishChainLatencies(latency);
                return latency;
            };
            auto latencies=publishLatencies();
            const auto stage=[&]() {
                auto graph=routing.getSnapshotPublisher().get();
                return graph && engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    *graph,latencies.get(),2,48000.0);
            };
            expect(stage());
            struct Source {std::int64_t pos=0,impulse=333;};
            Source input;
            engine.setGraphPdcTrackInputForTests(
                &input,[](void* ptr,const DAW::TrackID&,
                          juce::AudioBuffer<float>& output,int n) noexcept {
                    auto& state=*static_cast<Source*>(ptr);
                    for(int i=0;i<n;++i)
                        if(state.pos++==state.impulse) {
                            output.setSample(0,i,.30f);
                            output.setSample(1,i,-.15f);
                        }
                });
            engine.setExperimentalGraphPdcRenderEnabledForTests(true);
            const auto pass=[&](std::int64_t stop,int expectedDelay){
                float observed=0.f;
                while(input.pos<stop) {
                    const auto begin=input.pos;
                    const int n=static_cast<int>(std::min<std::int64_t>(
                        frames,stop-begin));
                    juce::AudioBuffer<float> output(2,n);
                    output.clear();
                    expect(engine.renderOfflineBlock(output,n,begin));
                    expectEquals(input.pos,begin+n);
                    for(int i=0;i<n;++i) {
                        const auto sampleTime=begin+i;
                        const float left=output.getSample(0,i);
                        const float right=output.getSample(1,i);
                        if(sampleTime==input.impulse+expectedDelay) {
                            observed=left;
                            expect(left>.08f && right<-.04f,
                                   "direct + wet paths must align at new latency");
                        } else {
                            expectWithinAbsoluteError(
                                static_cast<double>(left),0.0,2.e-5);
                            expectWithinAbsoluteError(
                                static_cast<double>(right),0.0,2.e-5);
                        }
                    }
                }
                return observed;
            };
            const float first=pass(1200,16);
            hosted->setResearchLatencyOnControlThread(32);
            expectEquals(hosted->getLatencySamples(),32);
            expectEquals(chain->totalLatencySamples(),32);
            latencies=publishLatencies();
            // Invalid revision MUST reject the old plan until publication.
            juce::AudioBuffer<float> stale(2,1);
            stale.clear();
            auto snapshot=routing.getSnapshotPublisher().get();
            expect(!engine.renderOnePreparedGraphEdgeForTests(
                *snapshot,0,stale),
                "changed JUCE AudioProcessor latency must reject stale PDC bank");
            expect(stage());
            input.impulse=1500;
            const float second=pass(2300,32);
            expectWithinAbsoluteError(static_cast<double>(first),
                                      static_cast<double>(second),.02,
                                      "changing VST3-like latency must preserve wet/dry amplitude");
            expect(engine.getGraphPdcPreflightCounters().renderedBlocks>0);
        }

#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
        beginTest("Live JUCE hosted latency 16->32 aligns slot and chain Dry/Wet");
        for(const double sampleRate:{44100.0,48000.0}) {
            DAW::PluginChainCore chain;
            chain.prepare(sampleRate,64);
            auto plugin=std::make_unique<ExactHostDelayResearchPlugin>(16);
            auto* hosted=plugin.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(plugin)),0);
            chain.setSlotMix(0,.5f);
            chain.setChainOutputMix(.5f);
            expectEquals(chain.totalLatencySamples(),16);
            constexpr double factor=1.0+0.7071067811865475244;
            const auto runImpulse=[&](int latency) {
                const float impulse=.25f;
                float measured=0.f;
                for(int base=0;base<512;base+=64) {
                    juce::AudioBuffer<float> audio(2,64);
                    audio.clear();
                    if(base==128) {
                        audio.setSample(0,0,impulse);
                        audio.setSample(1,0,-impulse);
                    }
                    chain.processBlock(audio,64);
                    for(int n=0;n<64;++n) {
                        const int absolute=base+n;
                        const double expected=absolute==128+latency
                            ?impulse*factor:0.0;
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(0,n)),
                            expected,4.e-5,
                            "VST3-like latency must align both slot and chain Dry/Wet");
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(1,n)),
                            -expected,4.e-5);
                        if(absolute==128+latency)
                            measured=audio.getSample(0,n);
                    }
                }
                return measured;
            };
            const float initial=runImpulse(16);
            hosted->setResearchLatencyOnControlThread(32);
            expectEquals(chain.totalLatencySamples(),32);
            expect(chain.refreshResearchHostDryPdcForChangedLatencyOnControlThread(),
                   "latency notification must refresh slot AND chain dry rings");
            const float updated=runImpulse(32);
            expectWithinAbsoluteError(static_cast<double>(updated),
                                      static_cast<double>(initial),4.e-5);
        }
#endif

        beginTest("Two independent sidechain PDC histories targeting one compressor (Track + Bus)");
        // Regression: the old destNodeId-indexed ring made these two
        // completely different sources overwrite EACH OTHER on every block.
        // Route IDs, not compressor destination IDs, own detector history.
        for(const double sampleRate:{44100.0,48000.0,96000.0})
            for(const int blockSize:{1,31,128})
            {
                DAW::TrackManager tracks;
                auto* voice=tracks.createTrack("sidechain-voice");
                auto* bus=tracks.createTrack("sidechain-bus");
                auto* compressor=tracks.createTrack("sidechain-target");
                bus->setRole(DAW::TrackRole::Bus);
                DAW::RoutingGraph routing;
                auto* voiceNode=routing.addNode(
                    "Voice SC",DAW::RoutingNodeType::Track,voice->getID());
                auto* busNode=routing.addNode(
                    "Bus SC",DAW::RoutingNodeType::Bus,bus->getID());
                auto* targetNode=routing.addNode(
                    "Target SC",DAW::RoutingNodeType::Track,compressor->getID());
                auto* voiceKey=routing.connect(
                    voiceNode->id,targetNode->id,DAW::ConnectionType::Sidechain);
                auto* busKey=routing.connect(
                    busNode->id,targetNode->id,DAW::ConnectionType::Sidechain);
                expect(voiceKey!=nullptr && busKey!=nullptr);
                if(!voiceKey || !busKey)continue;
                expect(voiceKey->id!=busKey->id);
                DAW::AudioEngine engine;
                engine.setSubsystems(&tracks,nullptr,nullptr,&routing);
                auto chainLatency=std::make_shared<DAW::AudioEngine::ChainLatencyMap>();
                (*chainLatency)[voice->getID()]=64;
                (*chainLatency)[bus->getID()]=24;
                (*chainLatency)[compressor->getID()]=16;
                engine.publishChainLatencies(chainLatency);
                engine.prepare(sampleRate,blockSize);
                expectEquals(engine.getSidechainPdcDelayForTests(voiceKey->id),48);
                expectEquals(engine.getSidechainPdcDelayForTests(busKey->id),8);
                for(int start=0;start<128;start+=blockSize)
                {
                    const int frames=std::min(blockSize,128-start);
                    juce::AudioBuffer<float> v(2,frames),b(2,frames);
                    v.clear();b.clear();
                    if(start==0) {
                        v.setSample(0,0,.5f);v.setSample(1,0,-.25f);
                        b.setSample(0,0,-.75f);b.setSample(1,0,.375f);
                    }
                    expect(engine.processSidechainPdcForTests(voiceKey->id,v));
                    expect(engine.processSidechainPdcForTests(busKey->id,b));
                    for(int i=0;i<frames;++i)
                    {
                        const auto t=start+i;
                        expectWithinAbsoluteError(
                            static_cast<double>(v.getSample(0,i)),
                            t==48?.5:0.0,1.e-6,
                            "Track sidechain must retain its OWN 48-sample history");
                        expectWithinAbsoluteError(
                            static_cast<double>(v.getSample(1,i)),
                            t==48?-.25:0.0,1.e-6);
                        expectWithinAbsoluteError(
                            static_cast<double>(b.getSample(0,i)),
                            t==8?-.75:0.0,1.e-6,
                            "Bus sidechain must retain its OWN 8-sample history");
                        expectWithinAbsoluteError(
                            static_cast<double>(b.getSample(1,i)),
                            t==8?.375:0.0,1.e-6);
                    }
                }
            }


        beginTest("Legacy monitor oversized callback chunks stay exact and allocation-free");
        // A 16k-frame offline callback intentionally exceeds the prepared
        // 64/128/256/512-frame RT scratch. Both the in-block impulse and a
        // delayed tail crossing into the next callback must be sample exact.
        for(const double rate:{44100.0,48000.0,96000.0})
        for(const int frames:{64,128,256,512})
        {
            DAW::RoutingSnapshot graph;
            graph.version=1004;
            graph.nodes={
                {"fast","fast",DAW::RoutingNodeType::Track,true},
                {"slow","slow",DAW::RoutingNodeType::Track,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            graph.nodeIndexById["fast"]=0;
            graph.nodeIndexById["slow"]=1;
            graph.nodeIndexById["master"]=2;
            DAW::RoutingSnapshot::EdgeSnapshot fast,slow;
            fast.id="fast-to-master";fast.sourceNodeId="fast";
            fast.destNodeId="master";fast.type=DAW::ConnectionType::Direct;
            slow.id="slow-to-master";slow.sourceNodeId="slow";
            slow.destNodeId="master";slow.type=DAW::ConnectionType::Direct;
            graph.edges={fast,slow};
            const std::map<DAW::TrackID,int> latency={{"fast",0},{"slow",32}};
            DAW::MasterPdcCore legacy;
            legacy.prepare(rate,frames);
            legacy.sync(graph,&latency,frames);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),32);
            std::vector<float> left(16384,0.f),right(16384,0.f);
            left[0]=.25f;right[0]=-.125f;
            left[16370]=.625f;right[16370]=-.3125f;
            {
                juce::UnitTestAllocationChecker checker(*this);
                legacy.processEdge("fast-to-master",left.data(),right.data(),16384);
            }
            expectWithinAbsoluteError((double)left[32],.25,1.e-6);
            expectWithinAbsoluteError((double)right[32],-.125,1.e-6);
            expectEquals(left[0],0.f);
            expectEquals(left[16370],0.f);
            float tailL[64]{},tailR[64]{};
            {
                juce::UnitTestAllocationChecker checker(*this);
                legacy.processEdge("fast-to-master",tailL,tailR,64);
            }
            for(int i=0;i<64;++i) {
                expectWithinAbsoluteError((double)tailL[i],
                    i==18?.625:0.,1.e-6);
                expectWithinAbsoluteError((double)tailR[i],
                    i==18?-.3125:0.,1.e-6);
            }
        }

        beginTest("Prewarmed legacy monitoring PDC sync is allocation-free");
        // Covers steady-state plan recomputation and changing the active
        // master route set without reallocating old delay banks. It does NOT
        // certify the still-unprepared first appearance of a new route.
        for(const double rate:{44100.0,48000.0,96000.0})
        for(const int frames:{64,128,256,512})
        {
            DAW::RoutingSnapshot graph;
            graph.version=1005;
            graph.nodes={
                {"fast","fast",DAW::RoutingNodeType::Track,true},
                {"slow","slow",DAW::RoutingNodeType::Track,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            graph.nodeIndexById["fast"]=0;
            graph.nodeIndexById["slow"]=1;
            graph.nodeIndexById["master"]=2;
            DAW::RoutingSnapshot::EdgeSnapshot fast,slow;
            fast.id="fast-to-master";fast.sourceNodeId="fast";
            fast.destNodeId="master";fast.type=DAW::ConnectionType::Direct;
            slow.id="slow-to-master";slow.sourceNodeId="slow";
            slow.destNodeId="master";slow.type=DAW::ConnectionType::Direct;
            graph.edges={fast,slow};
            const std::map<DAW::TrackID,int> latency={{"fast",0},{"slow",256}};
            DAW::MasterPdcCore legacy;
            legacy.prepare(rate,8192);
            legacy.sync(graph,&latency,frames); // allocate once outside callback
            expectEquals(legacy.getEdgeDelay("fast-to-master"),256);
            float left[512]{},right[512]{};
            {
                juce::UnitTestAllocationChecker checker(*this);
                for(int iteration=0;iteration<64;++iteration)
                {
                    graph.edges[1].active=(iteration%4)!=1;
                    graph.edges[1].bypassed=(iteration%4)==2;
                    legacy.sync(graph,&latency,frames);
                    legacy.processEdge("fast-to-master",left,right,frames);
                }
            }
            graph.edges[1].active=true;
            graph.edges[1].bypassed=false;
            legacy.sync(graph,&latency,frames);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),256);
        }

        beginTest("Legacy monitoring PDC ignores inactive and bypassed master routes");
        {
            DAW::RoutingSnapshot routes;
            routes.version=1006;
            routes.nodes={
                {"fast","fast",DAW::RoutingNodeType::Track,true},
                {"slow","slow",DAW::RoutingNodeType::Track,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            routes.nodeIndexById["fast"]=0;
            routes.nodeIndexById["slow"]=1;
            routes.nodeIndexById["master"]=2;
            DAW::RoutingSnapshot::EdgeSnapshot fast,slow;
            fast.id="fast-to-master";fast.sourceNodeId="fast";
            fast.destNodeId="master";fast.type=DAW::ConnectionType::Direct;
            slow.id="slow-to-master";slow.sourceNodeId="slow";
            slow.destNodeId="master";slow.type=DAW::ConnectionType::Direct;
            routes.edges={fast,slow};
            const std::map<DAW::TrackID,int> latencies={{"fast",0},{"slow",8192}};
            DAW::MasterPdcCore legacy;
            legacy.prepare(48000.0,128);
            legacy.sync(routes,&latencies,128);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),8192,
                "active high-latency route must compensate the faster track");
            routes.edges[1].active=false;
            legacy.sync(routes,&latencies,128);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),0,
                "inactive high-latency route cannot stall live monitoring");
            routes.edges[1].active=true;
            routes.edges[1].bypassed=true;
            legacy.sync(routes,&latencies,128);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),0,
                "bypassed high-latency route cannot raise the master target");
            routes.edges[1].bypassed=false;
            routes.nodes[1].active=false;
            legacy.sync(routes,&latencies,128);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),0,
                "inactive source node cannot contribute PDC latency");
            routes.nodes[1].active=true;
            legacy.sync(routes,&latencies,128);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),8192,
                "reactivation must restore sample-exact master compensation");
        }

        beginTest("Legacy PDC bypass must not replay an old pending impulse");
        for (const double rate : {44100.0,48000.0,96000.0})
        for (const int frames : {64,128,256,512})
        for (const bool bypassRoute : {false,true})
        {
            DAW::RoutingSnapshot routes;
            routes.version=1008;
            routes.nodes={
                {"fast","fast",DAW::RoutingNodeType::Track,true},
                {"slow","slow",DAW::RoutingNodeType::Track,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            routes.nodeIndexById["fast"]=0;
            routes.nodeIndexById["slow"]=1;
            routes.nodeIndexById["master"]=2;
            DAW::RoutingSnapshot::EdgeSnapshot fast,slow;
            fast.id="fast-to-master";fast.sourceNodeId="fast";
            fast.destNodeId="master";fast.type=DAW::ConnectionType::Direct;
            slow.id="slow-to-master";slow.sourceNodeId="slow";
            slow.destNodeId="master";slow.type=DAW::ConnectionType::Direct;
            routes.edges={fast,slow};
            const std::map<DAW::TrackID,int> latencies={{"fast",0},{"slow",32}};
            DAW::MasterPdcCore legacy;
            legacy.prepare(rate,512);
            legacy.sync(routes,&latencies,frames);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),32);
            float left[512]{},right[512]{};
            // Store an impulse in the last ten samples so the remaining
            // 32-sample delay holds it across the next block.
            left[frames-10]=.625f;
            right[frames-10]=-.3125f;
            legacy.processEdge("fast-to-master",left,right,frames);
            if(bypassRoute)routes.edges[1].bypassed=true;
            else routes.edges[1].active=false;
            legacy.sync(routes,&latencies,frames);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),0);
            if(bypassRoute)routes.edges[1].bypassed=false;
            else routes.edges[1].active=true;
            legacy.sync(routes,&latencies,frames);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),32);
            std::fill_n(left,frames,0.0f);
            std::fill_n(right,frames,0.0f);
            legacy.processEdge("fast-to-master",left,right,frames);
            for(int i=0;i<frames;++i) {
                expectEquals(left[i],0.f,
                    "master PDC must not replay a pre-bypass pending impulse");
                expectEquals(right[i],0.f);
            }
            std::fill_n(left,frames,0.0f);
            std::fill_n(right,frames,0.0f);
            left[0]=.25f;right[0]=-.125f;
            legacy.processEdge("fast-to-master",left,right,frames);
            for(int i=0;i<frames;++i) {
                expectWithinAbsoluteError(static_cast<double>(left[i]),
                    i==32?.25:0.0,1.e-6);
                expectWithinAbsoluteError(static_cast<double>(right[i]),
                    i==32?-.125:0.0,1.e-6);
            }
        }

        beginTest("Legacy master PDC lazy invalidation never replays dormant ring samples");
        {
            DAW::RoutingSnapshot routes;
            routes.version=1007;
            routes.nodes={
                {"fast","fast",DAW::RoutingNodeType::Track,true},
                {"slow","slow",DAW::RoutingNodeType::Track,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            routes.nodeIndexById["fast"]=0;
            routes.nodeIndexById["slow"]=1;
            routes.nodeIndexById["master"]=2;
            DAW::RoutingSnapshot::EdgeSnapshot fast,slow;
            fast.id="fast-to-master";fast.sourceNodeId="fast";
            fast.destNodeId="master";fast.type=DAW::ConnectionType::Direct;
            slow.id="slow-to-master";slow.sourceNodeId="slow";
            slow.destNodeId="master";slow.type=DAW::ConnectionType::Direct;
            routes.edges={fast,slow};
            const std::map<DAW::TrackID,int> latencies={{"fast",0},{"slow",32}};
            DAW::MasterPdcCore legacy;
            legacy.prepare(48000.0,64);
            legacy.sync(routes,&latencies,64);
            expectEquals(legacy.getEdgeDelay("fast-to-master"),32);
            float firstL[64]{},firstR[64]{};
            firstL[50]=.75f;firstR[50]=-.375f;
            legacy.processEdge("fast-to-master",firstL,firstR,64);
            // That impulse is still pending in legacy's 32-sample ring.
            // While V2 owns the route, legacy advances NO samples.
            legacy.invalidateStreamingHistoryForTimelineSwitch();
            legacy.sync(routes,&latencies,64); // same route and same latency
            float freshL[64]{},freshR[64]{};
            legacy.processEdge("fast-to-master",freshL,freshR,64);
            for(int i=0;i<64;++i) {
                expectEquals(freshL[i],0.f,
                    "returning to legacy must not replay pre-V2 audio");
                expectEquals(freshR[i],0.f,
                    "both channels must suppress stale history");
            }
            float newL[64]{},newR[64]{};
            newL[0]=.25f;newR[0]=-.125f;
            legacy.processEdge("fast-to-master",newL,newR,64);
            for(int i=0;i<64;++i) {
                expectWithinAbsoluteError(static_cast<double>(newL[i]),
                                          i==32?.25:0.0,1.e-6);
                expectWithinAbsoluteError(static_cast<double>(newR[i]),
                                          i==32?-.125:0.0,1.e-6);
            }
        }
        beginTest("Real AudioEngine track + bus + master loops follow live QuickSend toggles");
        {
            DAW::TrackManager tracks;
            auto* vocal=tracks.createTrack("PDC route probe");
            DAW::RoutingGraph routing;
            auto* source=vocal?routing.addNode(
                "PDC track",DAW::RoutingNodeType::Track,vocal->getID()):nullptr;
            auto* aux=routing.addNode("PDC aux",DAW::RoutingNodeType::Bus,"pdc-test-aux");
            auto* send=source&&aux?routing.connect(
                source->id,aux->id,DAW::ConnectionType::Send):nullptr;
            expect(vocal!=nullptr && source!=nullptr && aux!=nullptr && send!=nullptr);
            if(vocal && source && aux && send) {
                send->active.store(false,std::memory_order_relaxed);
                routing.notifyGraphChanged();
                DAW::AudioEngine engine;
                engine.setSubsystems(&tracks,nullptr,nullptr,&routing);
                engine.prepare(48000.0,128);
                struct SignalState {std::int64_t sample=0;};
                SignalState input;
                engine.setGraphPdcTrackInputForTests(
                    &input,[](void* context,const DAW::TrackID&,
                              juce::AudioBuffer<float>& audio,int frames) noexcept {
                        auto& sourceState=*static_cast<SignalState*>(context);
                        for(int n=0;n<frames;++n) {
                            const double phase=2.0*juce::MathConstants<double>::pi*
                                1000.0*static_cast<double>(sourceState.sample++)/48000.0;
                            audio.setSample(0,n,static_cast<float>(0.15*std::sin(phase)));
                            audio.setSample(1,n,static_cast<float>(0.10*std::cos(phase)));
                        }
                    });
#if !defined(APEX_PDC_V2_PRODUCTION_CANDIDATE) || !APEX_PDC_V2_PRODUCTION_CANDIDATE
                engine.setExperimentalGraphPdcRenderEnabledForTests(true);
#endif
                // In the production-candidate regression configuration this
                // fixture deliberately does NOT flip the test override: the
                // live APEX AudioEngine must opt into V2 by default, then the
                // armed-track guard and control refresh must still work.
                const std::map<DAW::TrackID,int> latency={
                    {vocal->getID(),0},{"pdc-test-aux",0}};
                const auto stage=[&]() {
                    auto graph=routing.getSnapshotPublisher().get();
                    return graph && engine.stageProgrammeGraphPdcPreflightOnControlThread(
                        *graph,&latency,2,48000.0);
                };
                expect(stage());
                std::int64_t timeline=0;
                const auto measureRms=[&]() {
                    double squares=0.0;
                    int measured=0;
                    juce::AudioBuffer<float> output(2,128);
                    for(int block=0;block<30;++block) {
                        output.clear();
                        expect(engine.renderOfflineBlock(output,128,timeline));
                        timeline+=128;
                        if(block>=10)
                            for(int n=0;n<128;++n) {
                                const auto sample=output.getSample(0,n);
                                squares+=static_cast<double>(sample)*sample;
                                ++measured;
                            }
                    }
                    return std::sqrt(squares/static_cast<double>(measured));
                };
                const double direct=measureRms();
                expect(direct>1.e-4,"direct vocal signal must reach master");
                send->active.store(true,std::memory_order_relaxed);
                routing.notifyGraphChanged();
                expect(stage());
                const double parallel=measureRms();
                expectWithinAbsoluteError(parallel,2.0*direct,
                    std::max(.002,0.10*direct),
                    "real track->send->bus->master must double the direct path");
                send->active.store(false,std::memory_order_relaxed);
                routing.notifyGraphChanged();
                expect(stage());
                const double restored=measureRms();
                expectWithinAbsoluteError(restored,direct,
                    std::max(.002,0.10*direct),
                    "disabling the send must remove its routed signal");
                const auto beforeArm=engine.getGraphPdcPreflightCounters();
                expect(beforeArm.matchingBlocks>0);
                expect(beforeArm.renderedBlocks>0,
                       "match is NOT enough: an actual AudioEngine block must run graph PDC");
                expectEquals(beforeArm.legacyResyncs,static_cast<std::uint64_t>(0),
                    "while V2 is active the legacy graph planner must NOT rebuild");
                vocal->setArmed(true);
                juce::AudioBuffer<float> monitorBuffer(2,128);
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                const auto duringArm=engine.getGraphPdcPreflightCounters();
                expectEquals(duringArm.renderedBlocks,beforeArm.renderedBlocks,
                    "record-armed monitoring MUST use legacy compensation authority");
                expect(duringArm.monitoringFallbackBlocks>
                       beforeArm.monitoringFallbackBlocks);
                expect(duringArm.legacyResyncs>beforeArm.legacyResyncs,
                    "first armed fallback block must synchronize legacy PDC before rendering");
                vocal->setArmed(false);
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                const auto staleReturn=engine.getGraphPdcPreflightCounters();
                expectEquals(staleReturn.renderedBlocks,duringArm.renderedBlocks,
                    "stale V2 delay history must NOT resume after monitoring gap");
                // The UI/message thread stages fresh histories before
                // allowing V2 to resume. This cannot run in the audio thread.
                expect(stage());
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                const auto afterArm=engine.getGraphPdcPreflightCounters();
                expect(afterArm.renderedBlocks>duringArm.renderedBlocks,
                       "PDC V2 must resume after disarming the track");
                // Regression: after the first monitoring fallback, legacy
                // MasterPdcCore is clean but then stops advancing while V2
                // renders. Re-arming WITHOUT a graph/latency version change
                // must still force a second fresh legacy resync.
                vocal->setArmed(true);
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                const auto secondArm=engine.getGraphPdcPreflightCounters();
                expectEquals(secondArm.renderedBlocks,afterArm.renderedBlocks,
                    "second monitoring arm must also disable V2 programme PDC");
                expect(secondArm.monitoringFallbackBlocks>
                       afterArm.monitoringFallbackBlocks);
                expect(secondArm.legacyResyncs>afterArm.legacyResyncs,
                    "second fallback MUST resync legacy PDC even with unchanged routing");
                vocal->setArmed(false);
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                const auto secondStaleReturn=engine.getGraphPdcPreflightCounters();
                expectEquals(secondStaleReturn.renderedBlocks,secondArm.renderedBlocks,
                    "second monitoring gap must also reject old V2 histories");
                expect(stage());
                monitorBuffer.clear();
                expect(engine.renderOfflineBlock(monitorBuffer,128,timeline));
                timeline+=128;
                expect(engine.getGraphPdcPreflightCounters().renderedBlocks>
                       secondStaleReturn.renderedBlocks,
                       "V2 must resume only after the second fresh control stage");
                (void)engine.collectGraphPdcRetiredOnControlThread();
            }
        }

        beginTest("Real routed AudioEngine impulse: post-fader Send / PreSend, aux latency edit");
        for(const auto sendKind:{DAW::ConnectionType::Send,DAW::ConnectionType::PreSend})
        {
        for(const double sr:{44100.0,48000.0,96000.0})
        {
            for(const int maxFrames:{31,64,128,256,512})
            {
                DAW::TrackManager tracks;
                auto* vocal=tracks.createTrack("PDC impulse vocal");
                DAW::RoutingGraph graph;
                auto* trackNode=vocal?graph.addNode(
                    "vocal",DAW::RoutingNodeType::Track,vocal->getID()):nullptr;
                auto* bus=graph.addNode("FX bus",DAW::RoutingNodeType::Bus,
                                        "pdc-impulse-bus");
                auto* send=trackNode&&bus?graph.connect(
                    trackNode->id,bus->id,sendKind):nullptr;
                expect(vocal!=nullptr && trackNode!=nullptr &&
                       bus!=nullptr && send!=nullptr);
                if(!vocal || !trackNode || !bus || !send)continue;
                struct Fixture {
                    DAW::TrackID vocalId;
                    DAW::TrackID busId;
                    DAW::PluginParallelDryDelayResearchCore vocalFx,busFx;
                    std::int64_t sample=0;
                    std::int64_t impulseAt=3100;
                } fixture;
                fixture.vocalId=vocal->getID();
                fixture.busId=bus->trackId;
                expect(fixture.vocalFx.prepare(2,32));
                expect(fixture.busFx.prepare(2,16));
                DAW::AudioEngine engine;
                engine.setSubsystems(&tracks,nullptr,nullptr,&graph);
                engine.prepare(sr,maxFrames);
                engine.setExperimentalGraphPdcRenderEnabledForTests(true);
                engine.setGraphPdcTrackInputForTests(
                    &fixture,[](void* ctx,const DAW::TrackID&,
                                juce::AudioBuffer<float>& buffer,int frames) noexcept {
                        auto& f=*static_cast<Fixture*>(ctx);
                        for(int n=0;n<frames;++n) {
                            if(f.sample++!=f.impulseAt)continue;
                            buffer.setSample(0,n,.25f);
                            buffer.setSample(1,n,-.125f);
                        }
                    });
                engine.setGraphPdcInsertForTests(
                    &fixture,[](void* ctx,const DAW::TrackID& id,
                                juce::AudioBuffer<float>& buffer,int n) noexcept {
                        auto& f=*static_cast<Fixture*>(ctx);
                        if(id==f.vocalId)
                            f.vocalFx.process(buffer.getArrayOfWritePointers(),2,n);
                        else if(id==f.busId)
                            f.busFx.process(buffer.getArrayOfWritePointers(),2,n);
                    });
                std::map<DAW::TrackID,int> latencies={
                    {fixture.vocalId,32},{fixture.busId,16}};
                const auto stage=[&]() {
                    auto snap=graph.getSnapshotPublisher().get();
                    return snap && engine.stageProgrammeGraphPdcPreflightOnControlThread(
                        *snap,&latencies,2,sr);
                };
                expect(stage());
                const auto renderTo=[&](std::int64_t stop,int expectedDelay,
                                         double copies) {
                    while(fixture.sample<stop) {
                        const auto begin=fixture.sample;
                        const int n=static_cast<int>(std::min<std::int64_t>(
                            maxFrames,stop-begin));
                        juce::AudioBuffer<float> output(2,n);
                        output.clear();
                        expect(engine.renderOfflineBlock(output,n,begin));
                        expectEquals(fixture.sample,begin+n);
                        for(int i=0;i<n;++i) {
                            const auto at=begin+i;
                            for(int ch=0;ch<2;++ch) {
                                const double amplitude=ch==0?.25:-.125;
                                const double expected=
                                    at==fixture.impulseAt+expectedDelay
                                    ? amplitude*std::sqrt(.5)*copies : 0.0;
                                expectWithinAbsoluteError(
                                    static_cast<double>(output.getSample(ch,i)),
                                    expected,2.e-4,
                                    "whole-graph impulse: all paths must align to one sample");
                            }
                        }
                    }
                };
                // Track insert 32, aux insert 16: direct must gain 16
                // programme samples, and both impulses reach master at 48.
                // PreSend bypasses the source center-pan, while post-fader
                // Send includes it. Both programme paths arrive at sample 48.
                const double fanoutCopies=sendKind==DAW::ConnectionType::PreSend
                    ?1.0+std::sqrt(2.0):2.0;
                renderTo(4100,48,fanoutCopies);
                send->active.store(false,std::memory_order_relaxed);
                graph.notifyGraphChanged();
                fixture.impulseAt=4600;
                expect(stage());
                // Without a send, only the vocal's own 32-sample delay remains.
                renderTo(5200,32,1.0);
                send->active.store(true,std::memory_order_relaxed);
                graph.notifyGraphChanged();
                expect(fixture.busFx.prepare(2,64));
                latencies[fixture.busId]=64;
                fixture.impulseAt=5700;
                expect(stage());
                // Updated VST3-like aux latency: BOTH paths reconverge at 96.
                renderTo(6400,96,fanoutCopies);
                const auto gauntlet=engine.getGraphPdcPreflightCounters();
                expect(gauntlet.matchingBlocks>0 && gauntlet.renderedBlocks>0,
                       "sample-exact PDC pass must use LIVE experimental route, not legacy");
            }
        }
        }

        beginTest("AudioEngine research opt-in dispatch aligns direct + send + bus exactly once");
        // Exercise 44.1/48/96 kHz and ASIO-sized as well as irregular blocks.
        for(const double rate:{44100.0,48000.0,96000.0})
        {
            for(const int frames:{1,31,64,128,256,512,1024})
            {
                DAW::AudioEngine engine;
                // Match the engine device format before staging a PDC bank.
                // The realtime safety gate rejects banks prepared for any
                // other sample rate; a default-constructed engine is 44.1k.
                engine.prepare(rate,frames);
                // No Release audio rendering is enabled: runtime opt-in
                // exists exclusively in the APEXTests preprocessor build.
                expectEquals(static_cast<int>(
                    engine.getMonitoringPdcMode()),
                    static_cast<int>(DAW::LiveMonitoringBypassCore::Mode::Bypass));
                engine.setExperimentalGraphPdcRenderEnabledForTests(true);
                DAW::RoutingSnapshot graph;
                graph.version=610;
                graph.nodes={
                    {"vocal-node","vocal",DAW::RoutingNodeType::Track,true},
                    {"aux-node","aux",DAW::RoutingNodeType::Bus,true},
                    {"master","master",DAW::RoutingNodeType::Master,true}};
                graph.processingOrder={"vocal-node","aux-node","master"};
                DAW::RoutingSnapshot::EdgeSnapshot direct,send,bus;
                direct.id="direct";direct.sourceNodeId="vocal-node";
                direct.destNodeId="master";direct.type=DAW::ConnectionType::Direct;
                send.id="send";send.sourceNodeId="vocal-node";
                send.destNodeId="aux-node";send.type=DAW::ConnectionType::Send;
                bus.id="bus";bus.sourceNodeId="aux-node";
                bus.destNodeId="master";bus.type=DAW::ConnectionType::Direct;
                graph.edges={direct,send,bus};
                std::map<DAW::TrackID,int> delays={{"vocal",32},{"aux",16}};
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,rate));
                DAW::PluginParallelDryDelayResearchCore vocalFx,auxFx;
                expect(vocalFx.prepare(2,32));
                expect(auxFx.prepare(2,16));
                constexpr int total=499;
                for(int start=0;start<total;start+=frames)
                {
                    const int n=std::min(frames,total-start);
                    juce::AudioBuffer<float> dry(2,n),wet(2,n);
                    for(int i=0;i<n;++i)
                    {
                        const int at=start+i;
                        dry.setSample(0,i,static_cast<float>(.14*std::sin(at*.057)));
                        dry.setSample(1,i,static_cast<float>(.18*std::cos(at*.041)));
                    }
                    vocalFx.process(dry.getArrayOfWritePointers(),2,n);
                    for(int ch=0;ch<2;++ch)
                        wet.copyFrom(ch,0,dry,ch,0,n);
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,dry));
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,1,wet));
                    auxFx.process(wet.getArrayOfWritePointers(),2,n);
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,2,wet));
                    for(int i=0;i<n;++i)
                    {
                        const int at=start+i-48;
                        const double expectedL=at<0?0.0:2.*.14*std::sin(at*.057);
                        const double expectedR=at<0?0.0:2.*.18*std::cos(at*.041);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(0,i)+wet.getSample(0,i)),
                            expectedL,2.e-6);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(1,i)+wet.getSample(1,i)),
                            expectedR,2.e-6);
                    }
                }
                juce::AudioBuffer<float> check(2,31);
                check.clear();
                graph.version=611;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check),
                       "stale graph cannot dispatch a mismatched PDC bank");
                // A same-version snapshot may still change its sends:
                // RoutingGraph::publishSnapshotOnly() does not bump version.
                // Never apply a bank prepared for a different active route.
                graph.version=610;
                graph.edges[1].active=false;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[1].active=true;
                graph.edges[0].gain=.42f;
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].retiring=true;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].retiring=false;
                graph.edges[0].tapPoint=DAW::TapPoint::PostFX;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].tapPoint=DAW::TapPoint::PreFX;
                graph.version=611;
                // A format reprepare must replace histories atomically.
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,rate));
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                // A device-rate switch rejects the old DSP bank even when
                // the routing snapshot is otherwise still identical.
                const double nextRate=rate==44100.0?48000.0:44100.0;
                engine.prepare(nextRate,frames);
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check),
                       "device rate change must reject stale PDC history");
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,nextRate));
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                // A SAME-rate restart is a new signal timeline: the old
                // 16-sample direct-path impulse cannot spill into playback.
                juce::AudioBuffer<float> seed(2,1);
                seed.setSample(0,0,1.f);
                seed.setSample(1,0,-.5f);
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,seed));
                engine.prepare(nextRate,frames);
                juce::AudioBuffer<float> cleared(2,64);
                cleared.clear();
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,cleared));
                for(int ch=0;ch<2;++ch)
                    for(int i=0;i<cleared.getNumSamples();++i)
                        expectEquals(cleared.getSample(ch,i),0.f,
                            "device restart must clear stale programme PDC");
                // The offline-render barrier likewise starts an empty history.
                seed.setSample(0,0,.8f);seed.setSample(1,0,-.4f);
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,seed));
                engine.resetOfflineRenderBuffers(frames);
                cleared.clear();
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,cleared));
                for(int ch=0;ch<2;++ch)
                    for(int i=0;i<cleared.getNumSamples();++i)
                        expectEquals(cleared.getSample(ch,i),0.f,
                            "offline export must clear stale programme PDC");
                expect(engine.collectGraphPdcRetiredOnControlThread()>0);
            }
        }


    }
};
static PdcV2MainlineJuceTests pdcV2MainlineJuceTests;
