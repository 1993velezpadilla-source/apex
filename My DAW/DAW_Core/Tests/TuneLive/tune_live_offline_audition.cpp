// APEX Tune Live OFFLINE VOCAL AUDITION utility (not a realtime plug-in).
// Usage: tune_live_offline_audition --input vocal.wav --prefix name
//        tune_live_offline_audition --self-test name
// Produces separate dry, SHIPPING 40ms HQ, and RESEARCH causal WAV files.
// Files have DIFFERENT timings: NOT level/time-aligned for ABX by default.
// No proprietary source, no telemetry network, no silently uploaded recordings.
// Reader: RIFF WAVE, mono/stereo PCM16/24/32 or IEEE float32, <= 96kHz.
// Writer: 24-bit PCM with clipping report. Cannot certify human voice quality.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveTrackedPrototype.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Wave {
    std::uint32_t rate=48000;
    int channels=1;
    std::vector<float> data; // interleaved samples
    std::size_t frames() const {return data.size()/static_cast<std::size_t>(channels);}
};
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p) {
    if(p>b.size() || b.size()-p<2)throw std::runtime_error("WAV truncated u16");
    return static_cast<std::uint16_t>(b[p]|(static_cast<unsigned>(b[p+1])<<8));
}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p) {
    if(p>b.size() || b.size()-p<4)throw std::runtime_error("WAV truncated u32");
    return static_cast<std::uint32_t>(b[p] | (static_cast<std::uint32_t>(b[p+1])<<8)
           | (static_cast<std::uint32_t>(b[p+2])<<16)
           | (static_cast<std::uint32_t>(b[p+3])<<24));
}
bool id(const std::vector<unsigned char>& bytes,std::size_t p,const char* tag){
    return p<=bytes.size() && bytes.size()-p>=4 &&
           bytes[p]==static_cast<unsigned char>(tag[0]) &&
           bytes[p+1]==static_cast<unsigned char>(tag[1]) &&
           bytes[p+2]==static_cast<unsigned char>(tag[2]) &&
           bytes[p+3]==static_cast<unsigned char>(tag[3]);
}
Wave readWave(const std::string& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open WAV input: "+path);
    const auto length=input.tellg();
    if(length<44 || length>static_cast<std::streamoff>(128*1024*1024))
        throw std::runtime_error("WAV length outside 44B..128MiB input limit");
    input.seekg(0);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if(!input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Truncated WAV file");
    if(!id(bytes,0,"RIFF")||!id(bytes,8,"WAVE"))
        throw std::runtime_error("Expected little-endian RIFF WAVE");
    const std::size_t riffEnd=static_cast<std::size_t>(u32(bytes,4))+8;
    if(riffEnd>bytes.size()||riffEnd<44)
        throw std::runtime_error("Invalid RIFF size");
    int format=0,channels=0,bits=0,align=0;
    std::uint32_t rate=0;
    std::size_t dataStart=0,dataLen=0;
    bool foundFormat=false,foundData=false;
    for(std::size_t pos=12;pos+8<=riffEnd;) {
        const std::size_t chunk=static_cast<std::size_t>(u32(bytes,pos+4));
        const std::size_t start=pos+8;
        if(chunk>riffEnd-start)throw std::runtime_error("WAV chunk overflow");
        if(id(bytes,pos,"fmt ")) {
            if(chunk<16)throw std::runtime_error("WAV invalid fmt");
            format=u16(bytes,start);
            channels=u16(bytes,start+2);
            rate=u32(bytes,start+4);
            align=u16(bytes,start+12);
            bits=u16(bytes,start+14);
            foundFormat=true;
        } else if(id(bytes,pos,"data") && !foundData) {
            dataStart=start;dataLen=chunk;foundData=true;
        }
        const std::size_t step=chunk+(chunk&1u);
        if(step>riffEnd-start) {
            if(chunk==riffEnd-start)break;
            throw std::runtime_error("WAV invalid odd-size padding");
        }
        pos=start+step;
    }
    if(!foundFormat||!foundData || channels<1 || channels>2 ||
       rate<8000 ||rate>96000 || (format!=1 && format!=3) ||
       !((format==1 && (bits==16||bits==24||bits==32)) ||
         (format==3 && bits==32)) ||
       align!=channels*(bits/8) ||dataLen%static_cast<std::size_t>(align)!=0)
        throw std::runtime_error("Unsupported WAV: use PCM 16/24/32 or IEEE float32 mono/stereo <=96kHz");
    Wave wave;
    wave.rate=rate;wave.channels=channels;
    const std::size_t frames=dataLen/static_cast<std::size_t>(align);
    if(frames>static_cast<std::size_t>(rate)*120)
        throw std::runtime_error("Audition input duration exceeds 120 seconds");
    wave.data.resize(frames*static_cast<std::size_t>(channels));
    for(std::size_t i=0;i<wave.data.size();++i) {
        const std::size_t p=dataStart+i*static_cast<std::size_t>(bits/8);
        float sample=0.0f;
        if(format==3) {
            const std::uint32_t raw=u32(bytes,p);
            static_assert(sizeof(float)==4,"requires IEEE float32");
            std::memcpy(&sample,&raw,sizeof(sample));
        } else if(bits==16) {
            const std::int16_t raw=static_cast<std::int16_t>(u16(bytes,p));
            sample=static_cast<float>(raw/32768.0);
        } else if(bits==24) {
            std::int32_t raw=static_cast<std::int32_t>(
                 static_cast<std::uint32_t>(bytes[p])|
                 (static_cast<std::uint32_t>(bytes[p+1])<<8)|
                 (static_cast<std::uint32_t>(bytes[p+2])<<16));
            if(raw&0x800000)raw|=~0xFFFFFF;
            sample=static_cast<float>(raw/8388608.0);
        } else {
            const auto raw=static_cast<std::int32_t>(u32(bytes,p));
            sample=static_cast<float>(raw/2147483648.0);
        }
        // Invalid audio samples must not poison the shifter.
        wave.data[i]=std::isfinite(sample)?std::clamp(sample,-1.0f,1.0f):0.0f;
    }
    return wave;
}
void w16(std::ofstream& out,std::uint16_t v) {
    const char buf[2]={static_cast<char>(v&255),static_cast<char>((v>>8)&255)};
    out.write(buf,2);
}
void w32(std::ofstream& out,std::uint32_t v) {
    w16(out,static_cast<std::uint16_t>(v&65535u));
    w16(out,static_cast<std::uint16_t>(v>>16));
}
struct Level {double peak=0.0,rms=0.0;std::size_t clips=0;};
Level level(const Wave& w) {
    Level result;
    double energy=0.0;
    for(float s:w.data) {
        if(!std::isfinite(s))throw std::runtime_error("Nonfinite output");
        const double a=std::abs(static_cast<double>(s));
        result.peak=std::max(result.peak,a);
        energy+=static_cast<double>(s)*s;
        if(a>=1.0)++result.clips;
    }
    result.rms=std::sqrt(energy/std::max<std::size_t>(1,w.data.size()));
    return result;
}
void writeWave24(const std::string& path,const Wave& w) {
    const std::size_t samples=w.data.size();
    if(samples>std::numeric_limits<std::uint32_t>::max()/3)
        throw std::runtime_error("Wave too large for RIFF PCM24");
    const std::uint32_t dataSize=static_cast<std::uint32_t>(samples*3);
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot create: "+path);
    out.write("RIFF",4);w32(out,36u+dataSize);out.write("WAVE",4);
    out.write("fmt ",4);w32(out,16);w16(out,1);
    w16(out,static_cast<std::uint16_t>(w.channels));
    w32(out,w.rate);w32(out,w.rate*static_cast<std::uint32_t>(w.channels)*3);
    w16(out,static_cast<std::uint16_t>(w.channels*3));w16(out,24);
    out.write("data",4);w32(out,dataSize);
    for(float sample:w.data) {
        const double bounded=std::clamp(static_cast<double>(sample),-1.0,8388607.0/8388608.0);
        const auto value=static_cast<std::int32_t>(std::llround(bounded*8388608.0));
        const char bytes[3]={static_cast<char>(value&255),
                   static_cast<char>((value>>8)&255),
                   static_cast<char>((value>>16)&255)};
        out.write(bytes,3);
    }
    if(!out)throw std::runtime_error("Failed writing: "+path);
}
Wave makeFixture() {
    Wave w;
    w.rate=48000;w.channels=1;
    const int frames=96000;
    w.data.resize(static_cast<std::size_t>(frames));
    constexpr double pi=3.14159265358979323846;
    double phase=0.0;
    std::uint32_t noise=0x41504558u;
    for(int i=0;i<frames;++i) {
        const double t=static_cast<double>(i)/w.rate;
        const double inputHz=t<1.0?220.0*std::pow(2.0,0.30/12.0):
                                     440.0*std::pow(2.0,-0.30/12.0);
        phase+=2*pi*inputHz/w.rate;
        double v=0.27*std::sin(phase);
        for(int h=2;h<=9;++h) {
            const double harmonic=inputHz*h;
            const double envelope=0.03+0.5*std::exp(-0.5*std::pow((harmonic-740)/230.0,2))
                                      +0.3*std::exp(-0.5*std::pow((harmonic-1200)/280.0,2));
            v+=envelope/h*std::sin(h*phase+0.13*h);
        }
        noise=noise*1664525u+1013904223u;
        const double breath=(static_cast<std::int32_t>(noise)/2147483648.0)*0.02;
        if(t<0.2 || (t>0.95 && t<1.05) || t>1.85) v=breath;
        w.data[static_cast<std::size_t>(i)]=static_cast<float>(v);
    }
    return w;
}
Wave processed(const Wave& source,bool causal) {
    Wave result=source;
    constexpr int block=128;
    APEX::TuneLive::DspCore hq;
    APEX::TuneLive::Research::TrackedPrototype research;
    if(causal) {
        research.prepare(result.rate,result.channels);
        APEX::TuneLive::VoiceSettings v;
        v.root=9;v.scale=0;v.correction=1.0f;v.humanize=0.0f;
        v.flexTune=0.0f;v.referenceHz=440.0f;v.noteHysteresis=0.10f;
        research.setVoiceSettings(v);
        research.enableExperimentalEarlyOnset(false); // low-octave-safe default
    } else {
        hq.prepare(result.rate,block,result.channels,
            APEX::TuneLive::ShifterProfile::HighQuality40ms);
    }
    APEX::TuneLive::DspSettings settings;
    settings.correction=1.0f;settings.key=9;settings.scale=0;
    settings.wet=1.0f;settings.retune=0.0f;
    settings.humanize=0.0f;settings.flexTune=0.0f;
    std::vector<float> left(block),right(block);
    for(std::size_t frame=0;frame<result.frames();frame+=block) {
        const int count=static_cast<int>(std::min<std::size_t>(block,result.frames()-frame));
        for(int i=0;i<count;++i){
            left[static_cast<std::size_t>(i)]=result.data[(frame+i)*result.channels];
            if(result.channels==2)
                right[static_cast<std::size_t>(i)]=result.data[(frame+i)*2+1];
        }
        float* channels[2]={left.data(),result.channels==2?right.data():nullptr};
        if(causal)research.process(channels,result.channels,count);
        else hq.process(channels,result.channels,count,settings);
        for(int i=0;i<count;++i) {
            result.data[(frame+i)*result.channels]=left[static_cast<std::size_t>(i)];
            if(result.channels==2)
                result.data[(frame+i)*2+1]=right[static_cast<std::size_t>(i)];
        }
    }
    if(causal)
        std::cout<<"RESEARCH causal tracked_first_f0_ms="
                 <<1000.0*research.firstVoicedSample()/source.rate
                 <<" variable_history_samples="<<research.requiredShifterHistorySamples()
                 <<" fixed_PDC=UNRESOLVED\n";
    else std::cout<<"SHIPPING 40ms host_reported_PDC_samples="
                  <<hq.latencySamples()<<"\n";
    return result;
}
void report(const std::string& label,const Wave& w) {
    const Level l=level(w);
    std::cout<<label<<" frames="<<w.frames()<<" sr="<<w.rate
       <<" channels="<<w.channels<<" peak="<<l.peak<<" rms="<<l.rms
       <<" output_samples_clipped_at_0dBFS="<<l.clips<<"\n";
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc<3) {
            std::cerr<<"Usage: "<<argv[0]<<" --self-test prefix OR --input vocal.wav --prefix prefix\n";
            return 2;
        }
        Wave source;
        std::string prefix;
        if(argc==3 && std::string(argv[1])=="--self-test") {
            source=makeFixture();
            prefix=argv[2];
            writeWave24(prefix+"_source.wav",source);
            const Wave reread=readWave(prefix+"_source.wav");
            if(reread.frames()!=source.frames()||reread.rate!=source.rate||
               reread.channels!=source.channels)
                throw std::runtime_error("Failed PCM24 fixture round-trip");
            source=reread;
        } else if(argc==5 && std::string(argv[1])=="--input" &&
                  std::string(argv[3])=="--prefix") {
            source=readWave(argv[2]);
            prefix=argv[4];
        } else {
            throw std::runtime_error("Expected --self-test prefix or --input vocal.wav --prefix prefix");
        }
        if(prefix.empty())throw std::runtime_error("Empty output prefix");
        std::cout<<"WARNING: Offline A/B files are NOT time/level aligned; research shifter has variable delay.\n";
        report("SOURCE",source);
        writeWave24(prefix+"_dry.wav",source);
        const Wave hq=processed(source,false);
        report("HQ40",hq);
        writeWave24(prefix+"_hq40.wav",hq);
        const Wave research=processed(source,true);
        report("CAUSAL_EXPERIMENT",research);
        writeWave24(prefix+"_causal_research.wav",research);
        const Wave hqVerify=readWave(prefix+"_hq40.wav");
        const Wave researchVerify=readWave(prefix+"_causal_research.wav");
        if(hqVerify.frames()!=source.frames()||researchVerify.frames()!=source.frames())
            throw std::runtime_error("Rendered audio length mismatch");
        std::cout<<"AUDITION_WAV_PASS source, HQ40, causal research; PCM24; "
                    "manual singer evaluation still REQUIRED\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"AUDITION_WAV_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
