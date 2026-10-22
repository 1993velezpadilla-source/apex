// Build: c++ -std=c++17 -Wall -Wextra -Werror ApexRadialGeometryPortableTests.cpp -o radial && ./radial
#include "../../Source/UICore/ApexRadialGeometry.h"
#include <cassert>
#include <iostream>

int main()
{
    using DAW::ApexRadialGeometry;
    constexpr int n = 8;
    // Each pointer kind uses identical screen-space geometry.
    assert(ApexRadialGeometry::sectorAt(100, 40, 100, 100, 20, 80, n) == 0);
    assert(ApexRadialGeometry::sectorAt(150, 50, 100, 100, 20, 80, n) == 1);
    assert(ApexRadialGeometry::sectorAt(160, 100, 100, 100, 20, 80, n) == 2);
    assert(ApexRadialGeometry::sectorAt(150, 150, 100, 100, 20, 80, n) == 3);
    assert(ApexRadialGeometry::sectorAt(100, 160, 100, 100, 20, 80, n) == 4);
    assert(ApexRadialGeometry::sectorAt(50, 150, 100, 100, 20, 80, n) == 5);
    assert(ApexRadialGeometry::sectorAt(40, 100, 100, 100, 20, 80, n) == 6);
    assert(ApexRadialGeometry::sectorAt(50, 50, 100, 100, 20, 80, n) == 7);
    assert(ApexRadialGeometry::sectorAt(100, 100, 100, 100, 20, 80, n) == -1);
    assert(ApexRadialGeometry::sectorAt(190, 100, 100, 100, 20, 80, n) == -1);
    assert(ApexRadialGeometry::sectorAt(100, 40, 100, 100, 20, 80, 0) == -1);
    assert(ApexRadialGeometry::isCentre(100, 100, 100, 100, 20));
    assert(!ApexRadialGeometry::isCentre(160, 100, 100, 100, 20));
    std::cout << "APEX Flow radial geometry: 13 assertions passed\n";
}
