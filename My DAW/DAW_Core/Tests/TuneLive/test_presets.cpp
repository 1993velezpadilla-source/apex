#include "../../Source/TuneLiveCore/ApexTuneLivePresets.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace APEX::TuneLive;
    static_assert(kVocalStyles.size() == 3, "Keep Natural/Modern/Hard profiles");
    const auto& natural=preset(VocalStyle::Natural);
    const auto& modern=preset(VocalStyle::Modern);
    const auto& hard=preset(VocalStyle::HardTune);
    assert(natural.humanize>modern.humanize && modern.humanize>hard.humanize);
    assert(natural.retune>modern.retune && modern.retune>hard.retune);
    assert(natural.amount<modern.amount && modern.amount<=hard.amount);
    assert(natural.preserveFormants && modern.preserveFormants && hard.preserveFormants);
    for(const auto& p:kVocalStyles) {
        assert(p.amount>=0.0f && p.amount<=1.0f);
        assert(p.retune>=0.0f && p.retune<=1.0f);
        assert(p.humanize>=0.0f && p.humanize<=1.0f);
        assert(p.stability>=0.0f && p.stability<=1.0f);
        assert(p.mix>=0.0f && p.mix<=1.0f);
    }
    assert(&preset(static_cast<VocalStyle>(333))==&natural);
    std::cout<<"PASS: artist profiles differ musically without corrupting ranges\n";
}
