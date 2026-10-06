// Links the application's actual JUCE module objects. Silent callback workload;
// no project/plugin state is opened and no input samples are stored.
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <algorithm>

struct Probe final : juce::AudioIODeviceCallback
{
    double rate=48000.0, duty=0.70;
    std::atomic<int> calls{0}, unexpected{0};
    std::atomic<juce::int64> first{0}, last{0};
    int expected=0;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { rate=d->getCurrentSampleRate(); expected=d->getCurrentBufferSizeSamples(); }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int channels, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        const auto start=juce::Time::getHighResolutionTicks();
        if (calls.load()==0) first.store(start);
        last.store(start);
        if(n!=expected) ++unexpected;
        for(int ch=0;ch<channels;++ch) if(outputs[ch]) std::fill_n(outputs[ch], n, 0.0f);
        const auto cost=(juce::int64)(duty*n/rate*juce::Time::getHighResolutionTicksPerSecond());
        while(juce::Time::getHighResolutionTicks()-start<cost) {}
        ++calls;
    }
};
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI lifetime;
    std::unique_ptr<juce::AudioIODeviceType> type(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    type->scanForDevices();
    const auto outputs=type->getDeviceNames(false), inputs=type->getDeviceNames(true);
    if(outputs.isEmpty()) { std::puts("ERROR no output endpoints"); return 1; }
    const auto out=outputs[type->getDefaultDeviceIndex(false)];
    const auto in=inputs.isEmpty() ? juce::String() : inputs[type->getDefaultDeviceIndex(true)];
    const bool duplex=argc>1 && juce::String(argv[1])=="duplex";
    const double duty=argc>2 ? std::atof(argv[2]) : 0.70;
    const int blockFilter=argc>3 ? std::atoi(argv[3]) : 0;
    const int duration=argc>4 ? std::atoi(argv[4]) : 1600;
    int failures=0;
    std::unique_ptr<juce::AudioIODevice> d(type->createDevice(out,duplex?in:juce::String()));
    if(!d) { std::puts("ERROR create device"); return 1; }
    for(double rate : {48000.0,44100.0})
    for(int block : {32,64,128,256,512,1024,2048})
    {
        if(blockFilter!=0 && blockFilter!=block) continue;
        const bool listed=d->getAvailableBufferSizes().contains(block);
        const bool rateListed=d->getAvailableSampleRates().contains(rate);
        juce::BigInteger outputMask; outputMask.setRange(0,2,true);
        juce::BigInteger inputMask; if(duplex) inputMask.setRange(0,2,true);
        const auto error=d->open(inputMask,outputMask,rate,block);
        if(error.isNotEmpty()) { std::printf("ERROR rate=%.0f block=%d %s\n",rate,block,error.toRawUTF8()); ++failures; continue; }
        Probe probe; probe.duty=duty;
        d->start(&probe);
        juce::Thread::sleep(duration);
        d->stop();
        const auto n=probe.calls.load();
        const double elapsed=(double)(probe.last.load()-probe.first.load())/juce::Time::getHighResolutionTicksPerSecond();
        const double progress=elapsed>0 ? (n-1)*probe.expected/rate/elapsed : 0;
        const bool ok=listed && rateListed && probe.expected==block && probe.unexpected.load()==0 && progress>0.97 && progress<1.03;
        std::printf("%s endpoint=%s duplex=%d rate=%.0f block=%d actual=%d listed=%d rateListed=%d callbacks=%d audioProgress=%.5f inputXruns=%d\n",ok?"PASS":"FAIL",out.toRawUTF8(),duplex,rate,block,probe.expected,listed,rateListed,n,progress,d->getXRunCount());
        std::fflush(stdout);
        d->close();
        if(!ok) ++failures;
    }
    return failures ? 1 : 0;
}
