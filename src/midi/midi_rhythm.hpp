#pragma once
#include <cstdint>
#include <vector>

namespace ncnl {

struct RhythmEvent {
    double start;
    double duration;
    int velocity;
    std::vector<int> pitches; // 实际 MIDI 音高，编辑/播放/渲染共用。
};

struct MidiRhythm {
    std::vector<RhythmEvent> events;
    double length=4.0; // 四分音符为一拍，保留文件中的休止。
};

MidiRhythm parse_midi_rhythm(const std::vector<unsigned char>& bytes);
std::vector<unsigned char> encode_midi(const MidiRhythm& rhythm,int bpm);

}
