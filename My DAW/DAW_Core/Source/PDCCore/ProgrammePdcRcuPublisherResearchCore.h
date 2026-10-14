#pragma once
// APEX research-only control->audio prepared PDC snapshot handoff.
// NOT wired to shipping AudioEngine. A caller must own/control one publisher,
// serialize control edits, and stop all audio readers before destruction.
//
// Motivation: std::atomic_load(shared_ptr) can decrement its LAST reference
// inside an audio callback, causing expensive state destruction on the RT
// thread. This handoff instead publishes a RAW non-owning pointer, while the
// message/control plane retains every old bank until reader quiescence.
//
// C++17 ordering contract (seq_cst on both atomics):
// audio: ++readers BEFORE loading current_; --readers AFTER last access.
// control: retire old ownership BEFORE storing new current_; reclaim retired
//          banks ONLY after checking readers==0 AFTER store.
// A late entrant observes the new bank, while an earlier entrant that still
// observes the old bank prevents that bank from being reclaimed.
//
// Reader acquisition/destruction: no mutex, allocations, waits or shared_ptr.
// Tradeoff: maxRetired bounds memory; publication fails closed if audio readers
// never drain and control edits exhaust the retained-history budget.
// The control thread may call collectRetired() opportunistically.
// Stateful delay lines MUST be driven by a single audio writer at a time.
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace DAW::PdcResearch {
template <typename PreparedState>
class ProgrammePdcRcuPublisherResearchCore final {
public:
    static constexpr std::size_t maxRetiredBanks=64;
    class Reader final {
    public:
        Reader(const Reader&)=delete;
        Reader& operator=(const Reader&)=delete;
        Reader(Reader&& other) noexcept
            : owner_(std::exchange(other.owner_,nullptr)),
              state_(std::exchange(other.state_,nullptr)) {}
        Reader& operator=(Reader&&)=delete;
        ~Reader() noexcept {
            if(owner_)
                owner_->readers_.fetch_sub(1,std::memory_order_seq_cst);
        }
        explicit operator bool() const noexcept{return state_!=nullptr;}
        PreparedState* get() const noexcept{return state_;}
        PreparedState* operator->() const noexcept{return state_;}
    private:
        friend class ProgrammePdcRcuPublisherResearchCore;
        explicit Reader(ProgrammePdcRcuPublisherResearchCore& owner) noexcept
            : owner_(&owner) {
            owner_->readers_.fetch_add(1,std::memory_order_seq_cst);
            state_=owner_->current_.load(std::memory_order_seq_cst);
        }
        ProgrammePdcRcuPublisherResearchCore* owner_=nullptr;
        PreparedState* state_=nullptr;
    };
    ProgrammePdcRcuPublisherResearchCore()=default;
    ProgrammePdcRcuPublisherResearchCore(
        const ProgrammePdcRcuPublisherResearchCore&)=delete;
    ProgrammePdcRcuPublisherResearchCore& operator=(
        const ProgrammePdcRcuPublisherResearchCore&)=delete;
    // Caller must have fully stopped realtime readers before destroying
    // this publisher; destruction remains on control thread.
    Reader readRealtime() noexcept{return Reader(*this);}
    bool publishPrepared(std::unique_ptr<PreparedState> next) {
        if(!next)return false;
        collectRetired();
        if(active_&&retired_.size()>=maxRetiredBanks)
            return false;
        if(active_)
            retired_.push_back(std::move(active_));
        // Both old and new banks already have stable control ownership.
        // Old readers may continue processing their old snapshot; they will
        // not force any destruction when their Reader leaves the callback.
        active_=std::move(next);
        current_.store(active_.get(),std::memory_order_seq_cst);
        return true;
    }
    // No collection or deallocation is allowed from readRealtime() or
    // the audio callback. Only the message/control thread calls this method.
    std::size_t collectRetired() {
        if(readers_.load(std::memory_order_seq_cst)!=0)return 0;
        const auto count=retired_.size();
        retired_.clear();
        return count;
    }
    std::size_t retiredCountControlOnly() const noexcept{
        return retired_.size();
    }
    unsigned activeReaders() const noexcept{
        return readers_.load(std::memory_order_seq_cst);
    }
private:
    std::atomic<PreparedState*> current_{nullptr};
    std::atomic<unsigned> readers_{0};
    std::unique_ptr<PreparedState> active_{};
    std::vector<std::unique_ptr<PreparedState>> retired_{};
};
} // namespace DAW::PdcResearch
