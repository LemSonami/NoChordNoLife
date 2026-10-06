#pragma once
#include "midi_rhythm.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ncnl {
constexpr double PLAYBACK_UNITS_PER_BEAT=1000000000.0;
struct ScheduledMidiNote { std::int64_t time; int pitch,velocity; bool on; };
inline std::vector<ScheduledMidiNote> midi_playback_schedule(const MidiRhythm& rhythm) {
    std::vector<ScheduledMidiNote> schedule;
    for (const auto& event:rhythm.events) {
        auto start=static_cast<std::int64_t>(std::llround(event.start*PLAYBACK_UNITS_PER_BEAT));
        auto end=static_cast<std::int64_t>(std::llround((event.start+event.duration)*PLAYBACK_UNITS_PER_BEAT));
        end=std::max(end,start+1);
        for (int pitch:event.pitches) {
            schedule.push_back({start,pitch,event.velocity,true});
            schedule.push_back({end,pitch,0,false});
        }
    }
    std::sort(schedule.begin(),schedule.end(),[](const ScheduledMidiNote& a,const ScheduledMidiNote& b){
        return a.time!=b.time ? a.time<b.time : a.on<b.on;
    });
    return schedule;
}
}
