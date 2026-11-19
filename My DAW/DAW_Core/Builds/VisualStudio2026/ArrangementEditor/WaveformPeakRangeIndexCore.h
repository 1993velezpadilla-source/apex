// ============================================================================
// WaveformPeakRangeIndexCore.h
// Compact, EXACT min/max range index for audio peaks (JUCE-free, C++17).
// A 32:1 / 1024:1 hierarchy gives ~3.2% extra peak storage, unlike a full
// per-column sparse table. Builds OUTSIDE the paint callback. Critically,
// the index never averages or skips a transient inside an output pixel.
// ============================================================================
#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace ArrangementEditor
{
    class WaveformPeakRangeIndexCore
    {
    public:
        static constexpr std::size_t kBlock = 32;
        static constexpr std::size_t kSuper = kBlock * kBlock;

        void rebuild(const std::vector<float>& mins,
                     const std::vector<float>& maxs)
        {
            sourceSize_ = 0;
            minBlock_.clear(); maxBlock_.clear();
            minSuper_.clear(); maxSuper_.clear();
            if (mins.empty() || mins.size() != maxs.size())
                return;

            const std::size_t blockCount = 1 + (mins.size() - 1) / kBlock;
            minBlock_.reserve(blockCount);
            maxBlock_.reserve(blockCount);
            for (std::size_t b = 0; b < blockCount; ++b)
            {
                const std::size_t first = b * kBlock;
                const std::size_t end = std::min(first + kBlock, mins.size());
                float lo = mins[first], hi = maxs[first];
                for (std::size_t i = first + 1; i < end; ++i)
                {
                    lo = std::min(lo, mins[i]);
                    hi = std::max(hi, maxs[i]);
                }
                minBlock_.push_back(lo);
                maxBlock_.push_back(hi);
            }

            const std::size_t superCount = 1 + (blockCount - 1) / kBlock;
            minSuper_.reserve(superCount);
            maxSuper_.reserve(superCount);
            for (std::size_t s = 0; s < superCount; ++s)
            {
                const std::size_t first = s * kBlock;
                const std::size_t end = std::min(first + kBlock, blockCount);
                float lo = minBlock_[first], hi = maxBlock_[first];
                for (std::size_t b = first + 1; b < end; ++b)
                {
                    lo = std::min(lo, minBlock_[b]);
                    hi = std::max(hi, maxBlock_[b]);
                }
                minSuper_.push_back(lo);
                maxSuper_.push_back(hi);
            }

            sourceSize_ = mins.size();
        }

        bool isReadyFor(std::size_t size) const noexcept
        {
            return sourceSize_ != 0 && sourceSize_ == size;
        }

        // Inclusive source-peak interval [first,last].
        // Output is identical to scanning every peak. "inspected" counts
        // source/index buckets actually visited, not skipped source peaks.
        void query(const std::vector<float>& mins,
                   const std::vector<float>& maxs,
                   int first, int last,
                   float& minOut, float& maxOut,
                   std::size_t* inspected = nullptr) const noexcept
        {
            minOut = 0.0f; maxOut = 0.0f;
            if (mins.empty() || mins.size() != maxs.size())
                return;

            const auto n = mins.size();
            const auto begin = static_cast<std::size_t>(std::max(0, first));
            const auto end = static_cast<std::size_t>(std::max(0, last));
            if (begin >= n || end < begin)
                return;
            const auto boundedEnd = std::min(n - 1, end);

            float lo = std::numeric_limits<float>::infinity();
            float hi = -std::numeric_limits<float>::infinity();
            auto useRaw = [&](std::size_t i)
            {
                lo = std::min(lo, mins[i]);
                hi = std::max(hi, maxs[i]);
                if (inspected) ++*inspected;
            };
            auto useBlock = [&](std::size_t b)
            {
                lo = std::min(lo, minBlock_[b]);
                hi = std::max(hi, maxBlock_[b]);
                if (inspected) ++*inspected;
            };
            auto useSuper = [&](std::size_t s)
            {
                lo = std::min(lo, minSuper_[s]);
                hi = std::max(hi, maxSuper_[s]);
                if (inspected) ++*inspected;
            };

            // Small ranges are faster with sequential reads; also provide a
            // correctness fallback if an index has not been built yet.
            if (!isReadyFor(n) || boundedEnd - begin < 64)
            {
                for (std::size_t i = begin; i <= boundedEnd; ++i)
                    useRaw(i);
            }
            else
            {
                std::size_t i = begin;
                // Align to a 32-peak block boundary.
                while (i <= boundedEnd && i % kBlock != 0)
                    useRaw(i++);
                // Then align to a 1024-peak superblock, without overreading.
                while (i <= boundedEnd && i % kSuper != 0 && boundedEnd - i + 1 >= kBlock)
                {
                    useBlock(i / kBlock);
                    i += kBlock;
                }
                while (i <= boundedEnd && boundedEnd - i + 1 >= kSuper)
                {
                    useSuper(i / kSuper);
                    i += kSuper;
                }
                while (i <= boundedEnd && boundedEnd - i + 1 >= kBlock)
                {
                    useBlock(i / kBlock);
                    i += kBlock;
                }
                while (i <= boundedEnd)
                    useRaw(i++);
            }
            minOut = lo;
            maxOut = hi;
        }

        std::size_t extraBytes() const noexcept
        {
            return (minBlock_.size() + maxBlock_.size()
                    + minSuper_.size() + maxSuper_.size()) * sizeof(float);
        }

    private:
        std::size_t sourceSize_ = 0;
        std::vector<float> minBlock_, maxBlock_, minSuper_, maxSuper_;
    };
}
