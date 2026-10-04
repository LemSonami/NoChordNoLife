#pragma once
#include <cstdint>
#include <vector>

namespace ncnl {

struct RhythmEvent {
    double start;
    double duration;
    int velocity;
    std::vector<int> pitches;
};

struct MidiRhythm {
    std::vector<RhythmEvent> events;
    double length=4.0;
};

MidiRhythm parse_midi_rhythm(const std::vector<unsigned char>& bytes);
std::vector<unsigned char> encode_midi(const MidiRhythm& rhythm,int bpm);

}