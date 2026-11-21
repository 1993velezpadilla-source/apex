// ============================================================================
// ArrangementHorizontalRingCore.h — content-to-image horizontal ring mapping.
// No JUCE dependency: tested with the portable C++17 touch-navigation suite.
// ============================================================================
// Each scroll advances the image's logical origin AND the storage offset by
// the same number of pixels. Existing visible columns therefore keep exactly
// the same physical memory address; only newly exposed stripes are rasterized.
#pragma once

#include <cstdint>

namespace ArrangementEditor
{
    struct ArrangementHorizontalRingCore final
    {
        static constexpr int advance(int wrap, std::int64_t delta, int capacity) noexcept
        {
            if (capacity <= 0)
                return 0;
            const auto width = static_cast<std::int64_t>(capacity);
            const auto remainder = (static_cast<std::int64_t>(wrap) + delta) % width;
            return static_cast<int>(remainder < 0 ? remainder + width : remainder);
        }

        static constexpr int storageX(int contentX, int frameX,
                                      int wrap, int capacity) noexcept
        {
            return advance(wrap, static_cast<std::int64_t>(contentX) - frameX,
                           capacity);
        }

        // Content columns before the seam live at image X [wrap, width).
        // Columns after the seam live at image X [0, wrap).
        static constexpr int seamOffset(int wrap, int capacity) noexcept
        {
            return capacity > 0 ? capacity - wrap : 0;
        }
    };
} // namespace ArrangementEditor
