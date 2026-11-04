#pragma once
// MATRIX-SONG's link to the MATRIX modules, with no cables: a module that can be conducted derives from
// songlink::Member and joins in its constructor (leaves in its destructor). MATRIX-SONG finds the members by
// module id and, on a section change, asks each for a pattern, a mute or a loop action; its level strip sets
// the member's level, and the member reports its output peak for the strip's meter. The requests are
// atomics the member takes in its own process() (songPoll), so nothing crosses engine threads unguarded.
#include "plugin.hpp"
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

namespace songlink {

// loop actions a section can hold for a module with a looper
enum Loop { L_NONE, L_PLAY, L_STOP, L_REC, L_ONCE, L_COUNT };
static const char* const kLoopName[L_COUNT] = {"-", "PLAY", "STOP", "REC", "ONCE"};

struct Member {
    Module* self = NULL;
    const char* songName = "?";   // the row label, e.g. "DRUM"
    int songPatterns = 8;         // 0: no patterns (MATRIX-ECHO)
    bool songHasLoop = false;     // takes loop actions
    // requests from MATRIX-SONG (-1 / L_NONE: none)
    std::atomic<int> reqPattern{-1}, reqLoop{L_NONE};
    std::atomic<bool> songMuted{false}, songLinked{false};
    // the Composer conducting this module (its module id; -1: free). One at a time: a second Composer leaves an
    // owned module alone until its owner lets it go (empties the row, or is deleted).
    std::atomic<int64_t> songOwner{-1};
    std::atomic<float> songLevel{1.f};   // MATRIX-SONG's level strip for this module (0..1)
    std::atomic<float> songPk{0.f};      // the member's output peak (V), for the strip's meter
    float songGainSm = 1.f, songDt = 1.f / 48000.f, songPkHold = 0.f;

    // the member's own pattern now (0-based; -1: none), read by MATRIX-SONG's display
    virtual int songPatternNow() { return -1; }
    // called from songPoll() in the member's process(): switch to pattern p (0-based) / do a loop action
    virtual void songSetPattern(int p) {}
    virtual void songDoLoop(int a) {}

    // every sample, first thing in process(): takes the requests, and glides to the level (0 while muted;
    // 5 ms, so strip moves and mutes never click). Returns the gain for the member's main outputs.
    float songPoll(float dt) {
        int p = reqPattern.exchange(-1);
        if (p >= 0) songSetPattern(p);
        int a = reqLoop.exchange(L_NONE);
        if (a != L_NONE) songDoLoop(a);
        float tg = songMuted.load() ? 0.f : clamp(songLevel.load(), 0.f, 1.f);
        songGainSm += (tg - songGainSm) * std::min(1.f, dt / 0.005f);
        songDt = dt;
        return songGainSm;
    }
    // after the outputs are set: their peak (falls about 20 dB a second), shown on MATRIX-SONG's strip
    void songMeter(float l, float r) {
        float a = std::max(std::fabs(l), std::fabs(r));
        songPkHold = std::max(a, songPkHold * (1.f - 2.3f * songDt));
        songPk.store(songPkHold, std::memory_order_relaxed);
    }
    virtual ~Member() {}
};

struct Registry {
    std::mutex m;
    std::vector<Member*> v;
};
inline Registry& reg() { static Registry r; return r; }

inline void join(Member* mb, Module* self, const char* name, int patterns, bool hasLoop) {
    mb->self = self; mb->songName = name; mb->songPatterns = patterns; mb->songHasLoop = hasLoop;
    Registry& r = reg();
    std::lock_guard<std::mutex> lk(r.m);
    r.v.push_back(mb);
}
inline void leave(Member* mb) {
    Registry& r = reg();
    std::lock_guard<std::mutex> lk(r.m);
    r.v.erase(std::remove(r.v.begin(), r.v.end(), mb), r.v.end());
}

// what MATRIX-SONG may know of a member (copied out under the lock)
struct Info { int64_t id; const char* name; int patterns; bool hasLoop; int patNow; int64_t owner; };
inline std::vector<Info> list() {
    Registry& r = reg();
    std::lock_guard<std::mutex> lk(r.m);
    std::vector<Info> out;
    for (Member* mb : r.v)
        if (mb->self && mb->self->id >= 0)
            out.push_back({mb->self->id, mb->songName, mb->songPatterns, mb->songHasLoop, mb->songPatternNow(), mb->songOwner.load()});
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.id < b.id; });
    return out;
}
// run f on the member with this module id, under the lock (false: it is gone)
template <typename F> bool with(int64_t id, F f) {
    Registry& r = reg();
    std::lock_guard<std::mutex> lk(r.m);
    for (Member* mb : r.v) if (mb->self && mb->self->id == id) { f(*mb); return true; }
    return false;
}

}  // namespace songlink
