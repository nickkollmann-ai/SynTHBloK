// MidiFile.hpp - tiny standard MIDI file reader for SynTHBloK Player Plus.
// No Rack dependency, so it can be unit-tested on its own.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>

namespace synthblok {

struct MidiNoteEvt {
    double  t;       // seconds from start (before speed scaling)
    uint8_t lane;    // 0..15
    uint8_t note;    // 0..127
    uint8_t vel;     // 0..127
    bool    on;
};

struct MidiSong {
    std::vector<MidiNoteEvt> ev;       // sorted by time, note-offs before note-ons at equal time
    std::vector<double>      clock;    // 16th-note clock times in seconds
    std::vector<std::string> laneNames;
    int    lanes  = 0;
    double length = 0.0;               // seconds, rounded up to whole 4/4 bars
    double bpm    = 120.0;             // tempo at start
    int    ppq    = 480;
    std::string name;
    std::string warn;
};

namespace detail {
struct Reader {
    const uint8_t* p; size_t n; size_t i = 0; bool bad = false;
    Reader(const uint8_t* d, size_t len) : p(d), n(len) {}
    bool has(size_t k) const { return i + k <= n; }
    uint8_t u8() { if (i >= n) { bad = true; return 0; } return p[i++]; }
    uint32_t u16() { uint32_t a = u8(); return (a << 8) | u8(); }
    uint32_t u32() { uint32_t a = u16(); return (a << 16) | u16(); }
    uint32_t vlq() {
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7f);
            if (!(b & 0x80)) break;
        }
        return v;
    }
};
struct RawNote { uint64_t tick; uint8_t ch, note, vel; bool on; };
struct Tempo   { uint64_t tick; uint32_t us; };
struct Track   { std::vector<RawNote> notes; std::string name; };
}

// Returns true on success. On failure err describes the problem.
inline bool parseMidi(const uint8_t* data, size_t len, MidiSong& out, std::string& err) {
    using namespace detail;
    out = MidiSong();
    Reader r(data, len);
    if (len < 14 || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
        err = "Not a MIDI file"; return false;
    }
    r.i = 4;
    uint32_t hlen = r.u32();
    uint32_t format = r.u16();
    uint32_t ntrks = r.u16();
    uint32_t division = r.u16();
    if (hlen > 6) r.i += hlen - 6;
    if (division & 0x8000) { err = "SMPTE time division is not supported"; return false; }
    if (division == 0) { err = "Bad time division"; return false; }
    int ppq = (int) division;

    std::vector<Track> tracks;
    std::vector<Tempo> tempos;
    uint64_t maxTick = 0;

    for (uint32_t t = 0; t < ntrks; t++) {
        if (!r.has(8)) break;
        uint8_t id[4]; for (int k = 0; k < 4; k++) id[k] = r.u8();
        uint32_t clen = r.u32();
        size_t end = std::min(r.n, r.i + (size_t) clen);
        if (!(id[0]=='M' && id[1]=='T' && id[2]=='r' && id[3]=='k')) { r.i = end; continue; }
        Track tr;
        uint64_t tick = 0; uint8_t status = 0;
        while (r.i < end && !r.bad) {
            tick += r.vlq();
            if (r.i >= end) break;
            uint8_t b = r.p[r.i];
            if (b & 0x80) { status = b; r.i++; }
            else if (status == 0) { break; }
            if (status == 0xFF) {
                uint8_t type = r.u8(); uint32_t ml = r.vlq();
                size_t me = std::min(end, r.i + (size_t) ml);
                if (type == 0x51 && ml == 3 && r.i + 3 <= end) {
                    uint32_t us = ((uint32_t) r.p[r.i] << 16) | ((uint32_t) r.p[r.i+1] << 8) | r.p[r.i+2];
                    if (us > 0) tempos.push_back({tick, us});
                } else if (type == 0x03 && tr.name.empty()) {
                    tr.name.assign((const char*) r.p + r.i, (size_t)(me - r.i));
                }
                r.i = me;
                if (type == 0x2F) break;
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t sl = r.vlq(); r.i = std::min(end, r.i + (size_t) sl);
            } else {
                uint8_t hi = status & 0xF0, ch = status & 0x0F;
                if (hi == 0xC0 || hi == 0xD0) { r.u8(); }
                else {
                    uint8_t d1 = r.u8(), d2 = r.u8();
                    if (hi == 0x90 && d2 > 0) tr.notes.push_back({tick, ch, d1, d2, true});
                    else if (hi == 0x80 || hi == 0x90) tr.notes.push_back({tick, ch, d1, d2, false});
                }
            }
            if (tick > maxTick) maxTick = tick;
        }
        r.i = end;
        tracks.push_back(std::move(tr));
    }
    if (tracks.empty()) { err = "No tracks found"; return false; }

    // Decide how to split into lanes
    std::vector<int> withNotes;
    for (size_t t = 0; t < tracks.size(); t++) if (!tracks[t].notes.empty()) withNotes.push_back((int) t);
    if (withNotes.empty()) { err = "No notes in file"; return false; }

    struct LaneKey { int track; int ch; };
    std::vector<LaneKey> keys;
    bool byChannel = (format == 0) || withNotes.size() == 1;
    if (byChannel) {
        bool seen[16] = {false};
        for (int t : withNotes) for (auto& n : tracks[t].notes) seen[n.ch] = true;
        for (int c = 0; c < 16; c++) if (seen[c]) keys.push_back({-1, c});
    } else {
        for (int t : withNotes) keys.push_back({t, -1});
    }
    if (keys.size() > 16) { out.warn = "More than 16 tracks, extra ones ignored"; keys.resize(16); }
    out.lanes = (int) keys.size();
    for (auto& k : keys) {
        if (k.track >= 0) {
            std::string nm = tracks[k.track].name;
            if (nm.empty()) nm = "Track " + std::to_string(k.track + 1);
            out.laneNames.push_back(nm);
        } else {
            out.laneNames.push_back("Ch " + std::to_string(k.ch + 1));
        }
    }

    // Tempo map
    std::sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b){ return a.tick < b.tick; });
    if (tempos.empty() || tempos[0].tick > 0) tempos.insert(tempos.begin(), {0, 500000});
    out.bpm = 60000000.0 / tempos[0].us;
    std::vector<double> segStartSec(tempos.size(), 0.0);
    for (size_t k = 1; k < tempos.size(); k++)
        segStartSec[k] = segStartSec[k-1] + (double)(tempos[k].tick - tempos[k-1].tick) * tempos[k-1].us / 1e6 / ppq;
    auto toSec = [&](uint64_t tk) {
        size_t k = 0;
        while (k + 1 < tempos.size() && tempos[k+1].tick <= tk) k++;
        return segStartSec[k] + (double)(tk - tempos[k].tick) * tempos[k].us / 1e6 / ppq;
    };

    for (size_t li = 0; li < keys.size(); li++) {
        auto collect = [&](const Track& tr) {
            for (auto& n : tr.notes) {
                if (keys[li].ch >= 0 && n.ch != keys[li].ch) continue;
                out.ev.push_back({toSec(n.tick), (uint8_t) li, n.note, n.vel, n.on});
            }
        };
        if (keys[li].track >= 0) collect(tracks[keys[li].track]);
        else for (int t : withNotes) collect(tracks[t]);
    }
    std::stable_sort(out.ev.begin(), out.ev.end(), [](const MidiNoteEvt& a, const MidiNoteEvt& b) {
        if (a.t != b.t) return a.t < b.t;
        return (int) a.on < (int) b.on;   // offs first
    });

    // Length: round up to whole bars of 4 beats
    uint64_t barTicks = (uint64_t) ppq * 4;
    uint64_t lastNote = 0;
    for (int t : withNotes) for (auto& n : tracks[t].notes) lastNote = std::max(lastNote, n.tick);
    lastNote = std::max(lastNote, maxTick);   // markers (e.g. an END marker) keep the full arrangement length
    uint64_t endTick = ((lastNote + barTicks - 1) / barTicks) * barTicks;
    if (endTick == 0) endTick = barTicks;
    out.length = toSec(endTick);

    // 16th note clock
    uint64_t step = std::max<uint64_t>(1, ppq / 4);
    for (uint64_t tk = 0; tk < endTick; tk += step) out.clock.push_back(toSec(tk));

    out.ppq = ppq;
    return true;
}

} // namespace synthblok
