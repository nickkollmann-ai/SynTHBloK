// Shared view of what the Player Plus has loaded and is playing, for the Player View expander.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "MidiFile.hpp"

struct PlayerLink {
    std::mutex m;                                              // guards the fields below
    std::vector<std::shared_ptr<synthblok::MidiSong>> songs;   // every loaded file
    std::vector<int> starts;                                   // first track out of each file
    std::vector<std::string> laneNames;
    std::vector<double> clock;                                 // the song's 16th-note clock (seconds), for bar and beat positions
    int lanes = 0;
    double length = 0.0, bpm = 120.0;
    std::string label;
    std::atomic<uint32_t> version{0};                          // bumps whenever the loaded files change
    std::atomic<float> playPos{0.f}, elapsed{0.f};
    std::atomic<int> barNow{0}, barsTotal{0};
    std::atomic<uint32_t> activeMask{0};
    std::atomic<bool> running{false};
    std::atomic<float> pitchNow[16];
    // control bridge: written by the Player View, read by the Player
    std::atomic<uint32_t> muteMask{0};                         // bit per track: muted
    std::atomic<int> loopStart{0}, loopLen{0};                 // loop region in beats (loopLen 0 = whole song)
    PlayerLink() { for (auto& a : pitchNow) a = 0.f; }
};

struct PlayerLinkProvider {
    virtual PlayerLink* playerLink() = 0;
    virtual ~PlayerLinkProvider() {}
};
