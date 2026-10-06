#pragma once
#include "BubblegumCableTypes.h"
#include <map>

namespace DAW {

class BubblegumCableRenderCacheCore
{
public:
    struct CacheEntry
    {
        juce::Image image;
        float localOx = 0, localOy = 0;
        float relTx = 0, relTy = 0, thickness = 0;
        bool  valid = false;
    };

    bool needsRebuild(const BubblegumCableGeometry& geo,
                      const BubblegumCableInput&    in) const noexcept
    {
        auto it = cache_.find(in.id());
        if (it == cache_.end() || !it->second.valid) return true;
        // During drag, reuse the stale cache — rebuilding every sub-pixel
        // mouse move causes full repaint of all slime layers every frame.
        // The positional error is imperceptible while dragging.
        if (in.dragTension > 0.f) return false;
        const auto& e = it->second;
        return (std::abs(e.relTx - (geo.tx - geo.sx)) > 0.5f ||
                std::abs(e.relTy - (geo.ty - geo.sy)) > 0.5f ||
                std::abs(e.thickness - geo.thickness)  > 0.3f);
    }

    juce::Graphics* beginRecord(const BubblegumCableGeometry& geo,
                                const BubblegumCableInput&    in,
                                float padding = 28.f)
    {
        float mnX = geo.sx, mxX = geo.sx, mnY = geo.sy, mxY = geo.sy;
        for (const auto& p : geo.points)
        {
            const float hw = juce::jmax(p.topW, p.bottomW);
            mnX = juce::jmin(mnX, p.x - hw - padding);
            mxX = juce::jmax(mxX, p.x + hw + padding);
            mnY = juce::jmin(mnY, p.y - hw - padding);
            mxY = juce::jmax(mxY, p.y + hw + padding);
        }
        mxY += geo.thickness * 4.5f + 24.f;

        CacheEntry& e = cache_[in.id()];
        e.localOx = mnX - geo.sx;  e.localOy = mnY - geo.sy;
        e.relTx   = geo.tx - geo.sx; e.relTy = geo.ty - geo.sy;
        e.thickness = geo.thickness; e.valid = false;

        const int w = juce::jmax(4, (int)std::ceil(mxX - mnX));
        const int h = juce::jmax(4, (int)std::ceil(mxY - mnY));
        e.image = juce::Image(juce::Image::ARGB, w, h, true);
        recordingId_ = in.id();
        g_.reset(new juce::Graphics(e.image));
        g_->addTransform(juce::AffineTransform::translation(-mnX, -mnY));
        return g_.get();
    }

    void endRecord() noexcept
    {
        if (recordingId_ >= 0.f)
        {
            auto it = cache_.find(recordingId_);
            if (it != cache_.end()) it->second.valid = true;
        }
        g_.reset();
        recordingId_ = -1.f;
    }

    void drawCache(juce::Graphics& g,
                   const BubblegumCableGeometry& geo,
                   const BubblegumCableInput&    in) const
    {
        auto it = cache_.find(in.id());
        if (it == cache_.end() || !it->second.valid || !it->second.image.isValid()) return;
        const auto& e = it->second;
        g.drawImageAt(e.image,
                      (int)std::round(geo.sx + e.localOx),
                      (int)std::round(geo.sy + e.localOy));
    }

    void invalidate(float id) { cache_.erase(id); }
    void invalidateAll()      { cache_.clear(); }

private:
    std::map<float, CacheEntry>     cache_;
    std::unique_ptr<juce::Graphics> g_;
    float                           recordingId_ = -1.f;
};

} // namespace DAW
