#pragma once

#include <array>
#include <string>
#include <vector>

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
    std::vector<GeneratedChord> chords;
    double quality_score;
};

bool score_matches_preset(double score,int preset);
bool quality_matches_generation_range(double score);
const char* emotion_preset_label(int preset);
GeneratedProgression generate_progression(
    const std::string& mode_string,
    const std::vector<ChordConstraint>& constraints
);

template<std::size_t N>
GeneratedProgression generate_progression(
    const std::string& mode_string,const std::array<ChordConstraint,N>& constraints
) {
    return generate_progression(mode_string,
        std::vector<ChordConstraint>(constraints.begin(),constraints.end()));
}

}  // namespace ncnl
