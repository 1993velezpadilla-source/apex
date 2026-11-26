#include "../Source/ApexOboeInputContinuity.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using apex_oboe::InputContinuity;
    InputContinuity a(24), b(24);
    std::array<float, 64> x{}, y{};
    x.fill(0.48f); y=x;
    a.process(x.data(),64,64);
    b.process(y.data(),64,64);
    assert(x==y && x.back()==0.48f);
    x.fill(99.0f); y=x;
    // Completely empty reads must decay continuously across callbacks,
    // and must not leak a pre-existing input sample after the fade.
    a.process(x.data(),64,0);
    b.process(y.data(),7,0);
    b.process(y.data()+7,11,0);
    b.process(y.data()+18,46,0);
    assert(x==y);
    assert(std::abs(x[0]-0.48f)<0.021f);
    for(int i=24;i<64;++i) assert(x[i]==0.0f);
    x.fill(0.48f); y=x;
    a.process(x.data(),64,64);
    b.process(y.data(),3,3);
    b.process(y.data()+3,61,61);
    assert(x==y && x.back()==0.48f);
    // Partial read followed by full and partial callbacks; missing suffix
    // is synthesized, not stale. Once valid again, normal samples are exact.
    x.fill(0.48f);
    a.process(x.data(),64,13);
    assert(x[12]==0.48f);
    assert(std::abs(x[13]-x[12])<0.021f);
    assert(x.back()==0.0f);
    x.fill(0.48f);
    a.process(x.data(),64,64);
    assert(x.back()==0.48f);
    a.reset();
    x.fill(9.0f);
    a.process(x.data(),64,0);
    for(auto sample:x) assert(sample==0.0f);
    std::cout<<"OBOE_INPUT_CONTINUITY_PASS: zero/partial reads, recovery, partition invariance\n";
}
