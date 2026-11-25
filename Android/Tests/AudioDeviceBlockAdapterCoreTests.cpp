// Standalone contract tests: portable APEX callback adapter used on Android.
// Requires only a C++17 compiler, no Android SDK or JUCE UI.
#include "../../My DAW/DAW_Core/Source/DeviceCore/AudioDeviceBlockAdapterCore.h"
#include <cassert>
#include <vector>
#include <utility>
int main()
{
    using DAW::AudioDeviceBlockAdapterCore;
    std::vector<std::pair<int,int>> chunks;
    auto run = [&](int size, int prepared)
    {
        chunks.clear();
        AudioDeviceBlockAdapterCore::forEachPreparedChunk(
            size, prepared, [&](int offset, int count) {
                assert(count > 0);
                assert(count <= prepared);
                assert(offset >= 0);
                assert(offset + count <= size);
                if (!chunks.empty())
                    assert(offset == chunks.back().first + chunks.back().second);
                else
                    assert(offset == 0);
                chunks.emplace_back(offset, count);
            });
    };
    run(0, 256);
    assert(chunks.empty());
    run(128, 0);
    assert(chunks.empty());
    run(128, 256);
    assert(chunks.size() == 1 && chunks[0].second == 128);
    run(1025, 256);
    assert(chunks.size() == 5);
    assert(chunks[0].second == 256);
    assert(chunks[4].first == 1024 && chunks[4].second == 1);
    // A graph prepared for a 512-frame maximum must dispatch one native
    // callback without artificial splitting, even with a 32/96-frame queue.
    for (int n : {32,48,64,96,128,160,192,256,384,512})
    {
        run(n,512);
        assert(chunks.size()==1 && chunks.front().second==n);
    }
    run(960,512);
    assert(chunks.size()==2 && chunks[1].first==512 && chunks[1].second==448);
    return 0;
}
