#pragma once

#include <array>
#include <string>

namespace ncnl {

struct ChordConstraint {
    int emotion_preset;
    std::string fixed_notes;
};

struct GeneratedChord {
    std::string notes;
    double emotion_score;
};

struct GeneratedProgression {
    std::array<GeneratedChord,4> chords;
    double quality_score;
};

bool score_matches_preset(double score,int preset);
const char* emotion_preset_label(int preset);
GeneratedProgression generate_progression(
    const std::string& mode_string,
    const std::array<ChordConstraint,4>& constraints
);

}  // namespace ncnl
