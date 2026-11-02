#pragma once
// One JAM, many modules: every JAM module can open the same MIDI device, but only the owner reacts to
// it and drives its lights. Clicking a module (or switching its MIDI button on) makes it the owner;
// with no owner, the first module with MIDI on takes it.
#include <atomic>
#include <cstdint>

namespace jamown {

inline std::atomic<int64_t>& owner() { static std::atomic<int64_t> o{-1}; return o; }

struct Link {
    bool active = false, wasEnabled = false;
    // Call every sample. Returns +1 when this module just became the owner, -1 when it just lost it.
    int update(int64_t id, bool enabled) {
        std::atomic<int64_t>& o = owner();
        if (enabled && !wasEnabled) o = id;                    // MIDI switched on: take the JAM
        if (!enabled && o == id) o = -1;
        int64_t none = -1;
        if (enabled) o.compare_exchange_strong(none, id);
        wasEnabled = enabled;
        bool a = enabled && o == id;
        int r = a == active ? 0 : a ? 1 : -1;
        active = a;
        return r;
    }
};
inline void claim(int64_t id) { owner() = id; }
inline void release(int64_t id) { int64_t me = id; owner().compare_exchange_strong(me, -1); }

}  // namespace jamown
