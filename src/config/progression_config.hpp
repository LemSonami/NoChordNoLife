#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace ncnl{

enum class ProgressionWeight : std::size_t {
    voice_leading=0,
    common_tone=1,
    modal_consistency=2,
    root_motion=3,
    tonal_attraction=4,
};

struct ProgressionWeights {
    std::array<double,5> values;
};

ProgressionWeights default_progression_weights();
const ProgressionWeights& progression_weights();
void set_progression_weights(const ProgressionWeights& weights);
void adjust_progression_weight(std::size_t index,double value);
bool load_progression_config(const std::string& path);
bool save_progression_config(const std::string& path);

}