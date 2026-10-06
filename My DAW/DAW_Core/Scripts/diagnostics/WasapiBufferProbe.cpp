// Silent, output-only WASAPI regression probe. Never opens a microphone or song.
// Measures endpoint starvation with the same block-copy loop used by pinned JUCE.
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmsystem.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr, const char* operation)
{
    if (FAILED(hr)) { std::printf("ERROR %s 0x%08lx\n", operation, (unsigned long)hr); throw std::runtime_error(operation); }
}
int main(int argc, char** argv)
{
    try
    {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
        // Match JUCE's existing 1 ms timer resolution and WASAPI priority.
        struct Scheduling { HANDLE mmcss; Scheduling() { timeBeginPeriod(1); DWORD index=0; mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio", &index); } ~Scheduling() { if(mmcss) AvRevertMmThreadCharacteristics(mmcss); timeEndPeriod(1); } } scheduling;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "enumerator");
        ComPtr<IMMDevice> device;
        check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device), "default render endpoint");
        ComPtr<IPropertyStore> properties;
        check(device->OpenPropertyStore(STGM_READ, &properties), "properties");
        PROPVARIANT name; PropVariantInit(&name);
        check(properties->GetValue(PKEY_Device_FriendlyName, &name), "name");
        if (name.vt == VT_LPWSTR) std::printf("endpoint=%ls\n", name.pwszVal);
        PropVariantClear(&name);
        for (bool buffered : {false, true})
        for (int block : {64, 128, 256, 512, 1024, 2048})
        {
            ComPtr<IAudioClient> client;
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "activate");
            WAVEFORMATEX* format = nullptr;
            check(client->GetMixFormat(&format), "format");
            REFERENCE_TIME period=0, minimum=0;
            check(client->GetDevicePeriod(&period, &minimum), "period");
            const int periodFrames = (int)((period * format->nSamplesPerSec + 9999999) / 10000000);
            const bool poll = buffered && block > periodFrames;
            const auto duration = poll ? (REFERENCE_TIME)((10000000LL * (block + periodFrames) + format->nSamplesPerSec - 1) / format->nSamplesPerSec) : period;
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, poll ? 0 : AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     duration, 0, format, nullptr), "initialize");
            UINT32 capacity=0;
            check(client->GetBufferSize(&capacity), "capacity");
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!poll) check(client->SetEventHandle(event), "set event");
            ComPtr<IAudioRenderClient> render;
            check(client->GetService(IID_PPV_ARGS(&render)), "render");
            BYTE* data=nullptr;
            check(render->GetBuffer(capacity, &data), "prime");
            check(render->ReleaseBuffer(capacity, AUDCLNT_BUFFERFLAGS_SILENT), "prime release");
            check(client->Start(), "start");
            const auto begin=std::chrono::steady_clock::now();
            const double duty = argc > 1 ? std::atof(argv[1]) : 0.40;
            const double costMs = duty * 1000.0 * block / format->nSamplesPerSec;
            int starved=0, callbacks=0; UINT32 minPadding=capacity;
            while (std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count() < 1.5)
            {
                const auto workBegin=std::chrono::steady_clock::now();
                while (std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-workBegin).count() < costMs) YieldProcessor();
                UINT32 padding=0; check(client->GetCurrentPadding(&padding), "padding before copy");
                if (callbacks > 1) { minPadding=std::min(minPadding,padding); if(padding==0) ++starved; }
                int remaining=block;
                while (remaining>0)
                {
                    check(client->GetCurrentPadding(&padding), "padding");
                    const UINT32 frames=std::min((UINT32)remaining,capacity-padding);
                    if(frames==0) { WaitForSingleObject(event,poll ? 1 : 1000); continue; }
                    check(render->GetBuffer(frames,&data), "get render");
                    check(render->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT), "release render");
                    remaining -= (int)frames;
                }
                ++callbacks;
            }
            check(client->Stop(), "stop");
            std::printf("mode=%s block=%d rate=%lu period=%d capacity=%u costMs=%.3f callbacks=%d emptyBeforeWrite=%d minPadding=%u\n",
                        buffered?"buffered":"legacy",block,format->nSamplesPerSec,periodFrames,capacity,costMs,callbacks,starved,minPadding);
            std::fflush(stdout);
            CloseHandle(event); CoTaskMemFree(format);
        }
        return 0;
    }
    catch (...) { return 1; }
}
