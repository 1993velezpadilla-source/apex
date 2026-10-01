#pragma once

namespace DAW {

class ExportThreadStartStateCore
{
public:
    struct Outcome
    {
        bool running = false;
        bool releaseOwnership = true;
    };

    static constexpr Outcome resolve(bool nativeStarted) noexcept
    {
        return { nativeStarted, !nativeStarted };
    }
};

} // namespace DAW
