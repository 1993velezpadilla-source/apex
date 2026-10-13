// APEX 6AQ5-like EL-driver research gates; no measured UA/Teletronix curves.
#include "../../Source/CompressorCore/Apex6AQ5DriverResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace APEX::Compressor;
static void near(double a,double b,double tol=1.e-7) {
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tol) {
        std::cerr<<"mismatch "<<a<<" vs "<<b<<"\n";std::abort();
    }
}
int main() {
    Apex6AQ5DriverResearchCore driver;
    driver.prepare(48000);
    near(driver.elLevel(),0);
    assert(driver.idlePlateCurrentAmps()>0);
    double last=-1;
    for(int i=0;i<=100;++i) {
        const double grid=-2.0+4.0*i/100.0;
        const double current=driver.currentAtGridVolts(grid);
        assert(current>=last-1.e-12);
        last=current;
    }
    // DC grid bias is the dark reference, external detector controls drive.
    for(int i=0;i<4800;++i)driver.tick(0);
    near(driver.elLevel(),0);
    for(int i=0;i<9600;++i)driver.tick(1);
    const double lit=driver.elLevel();
    assert(lit>.7&&lit<=1.0);
    for(int i=0;i<240;++i)driver.tick(0);
    const double decaying=driver.elLevel();
    assert(decaying<lit*.3 && decaying>=0.0);
    for(int i=0;i<4800;++i)driver.tick(0);
    assert(driver.elLevel()<1.e-8);

    // A stable 100ms exposure has invariant optical output across
    // sample rates and any host block partitions.
    const auto litAt=[](double sr) {
        Apex6AQ5DriverResearchCore d;d.prepare(sr);
        for(int i=0;i<static_cast<int>(sr*.1);++i)d.tick(.55);
        return d.elLevel();
    };
    const double reference=litAt(48000);
    assert(reference>.03 && reference<1);
    for(double sr:{44100.0,96000.0,192000.0})
        near(litAt(sr),reference,.005);

    // Deterministic sample clock even when host buffers vary.
    Apex6AQ5DriverResearchCore whole,sliced;
    whole.prepare(48000);sliced.prepare(48000);
    std::vector<double> values(9000);
    for(int n=0;n<9000;++n)values[n]=n<1500?.65:n<3500?.2:0;
    for(int n=0;n<9000;++n)whole.tick(values[n]);
    for(int start=0;start<9000;start+=31)
        for(int i=start;i<std::min(9000,start+31);++i)sliced.tick(values[i]);
    near(whole.elLevel(),sliced.elLevel(),1.e-12);

    // Invalid controls do not produce NaN or non-deterministic attack.
    Apex6AQ5DriverCircuit broken;
    broken.gridBiasVolts=NAN;broken.perveance=-INFINITY;
    broken.elTurnOnVolts=INFINITY;broken.elFullScaleVolts=NAN;
    broken.rectifierReleaseSec=NAN;
    Apex6AQ5DriverResearchCore safe;
    safe.prepare(NAN,broken);
    for(double value:{std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),-1.0,100.0})
        for(int i=0;i<100;++i) {
            const double v=safe.tick(value);
            assert(std::isfinite(v)&&v>=0&&v<=1);
        }
    std::cout<<"APEX 6AQ5 inspired EL driver research PASS; calibration pending\n";
}
