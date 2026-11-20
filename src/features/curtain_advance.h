#pragma once
#include <cstdint>

namespace istaroth {
struct ContinueKey {
    std::uintptr_t native=0,managed=0;
    std::uint32_t instance=0;
    bool Valid() const { return native>=0x10000&&managed>=0x10000&&instance; }
    bool operator==(const ContinueKey& other) const {
        return native==other.native&&managed==other.managed&&instance==other.instance;
    }
};
enum class ContinueObservation { Unknown, Inactive, Waiting };

// A control instance is an interaction identity, not a proven multi-page ID.
// Missing samples and permission changes never re-arm a submitted interaction.
struct ContinueLatch {
    ContinueKey candidate{},submitted{};
    std::uint64_t since=0,last_observed=0;
    bool Observe(ContinueObservation observation,ContinueKey key,std::uint64_t now,bool permit) {
        if(observation==ContinueObservation::Inactive&&key.Valid()) {
            if(submitted==key)submitted={};
            if(candidate==key){candidate={};since=last_observed=0;}
            return false;
        }
        if(observation!=ContinueObservation::Waiting||!key.Valid()) {
            candidate={};since=last_observed=0;return false;
        }
        if(!(candidate==key)||now<last_observed||now-last_observed>250){candidate=key;since=now;}
        last_observed=now;
        // Observation may accumulate while paused. Permission is rechecked at
        // submission; it never clears an already-submitted interaction.
        if(!permit||submitted.Valid()||now-since<500)return false;
        submitted=key;return true;
    }
};
}
