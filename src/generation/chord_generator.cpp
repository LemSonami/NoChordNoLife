#include "chord_generator.hpp"
#include "../midi/piano_roll_notes.hpp"

#include "../algorithms/chord_algorithms.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ncnl {
namespace {

const std::array<const char*,12> NOTE_NAMES={{
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
}};

const std::vector<std::vector<int>> CHORD_SHAPES={
    {0,4,7},       // major
    {0,3,7},       // minor
    {0,3,6},       // diminished
    {0,4,8},       // augmented
    {0,2,7},       // sus2
    {0,5,7},       // sus4
    {0,4,7,10},    // dominant7
    {0,4,7,11},    // major7
    {0,3,7,10},    // minor7
    {0,3,6,10},    // half diminished7
    {0,3,6,9},     // diminished7
};

struct Candidate {
    std::string notes;
    double emotion_score;
    unsigned int pitch_mask;
    double preference;
};

struct Trial {
    std::vector<std::size_t> choices;
    double quality;
    double preference;
};

std::string chord_to_string(const Chord& chord) {
    std::ostringstream output;
    for (std::size_t i=0;i<chord.size();++i) {
        if (i>0) {
            output<<' ';
        }
        output<<NOTE_NAMES[static_cast<std::size_t>(chord[i])];
    }
    return output.str();
}

std::string normalize_chord_text(const std::string& text) {
    std::istringstream input(text);
    std::ostringstream output;
    std::string note;
    bool first=true;
    while (input>>note) {
        if (!first) {
            output<<' ';
        }
        output<<note;
        first=false;
    }
    return output.str();
}

unsigned int chord_mask(const Chord& chord) {
    unsigned int mask=0;
    for (int pc:chord) {
        mask|=1u<<static_cast<unsigned int>(pc);
    }
    return mask;
}

std::vector<Candidate> build_candidates(const std::string& mode_string) {
    auto mode=parse_mode(mode_string);
    unsigned scale_mask=0;
    for (int interval:mode.intervals) { scale_mask|=1u<<((mode.tonic+interval)%12); }
    std::vector<std::pair<std::string,unsigned int>> chord_texts;
    std::set<unsigned int> seen;

    // 二音结构保证五个情感区间都有候选；三和弦和七和弦负责提供常用和声结构。
    for (int root=0;root<12;++root) {
        for (int interval=1;interval<=6;++interval) {
            Chord chord={root,(root+interval)%12};
            unsigned int mask=chord_mask(chord);
            if (seen.insert(mask).second) {
                chord_texts.push_back({chord_to_string(chord),mask});
            }
        }
    }

    for (int root=0;root<12;++root) {
        for (const auto& shape:CHORD_SHAPES) {
            Chord chord;
            for (int interval:shape) {
                chord.push_back((root+interval)%12);
            }
            unsigned int mask=chord_mask(chord);
            if (seen.insert(mask).second) {
                chord_texts.push_back({chord_to_string(chord),mask});
            }
        }
    }

    std::vector<Candidate> candidates;
    for (const auto& item:chord_texts) {
        Chord parsed=parse_chord(item.first);
        int outside=0;
        for (int pc:parsed) { outside+=(scale_mask&(1u<<pc))==0; }
        // Soft preferences: each borrowed tone lowers odds; dyads remain a fallback.
        double preference=std::pow(0.15,outside)*(parsed.size()==2 ? 0.18 : 1.0);
        candidates.push_back({
            item.first,
            chord_emotion_score(mode_string,item.first),
            item.second,
            preference,
        });
    }
    return candidates;
}

Candidate fixed_candidate(
    const std::string& mode_string,
    const ChordConstraint& constraint
) {
    std::string notes=normalize_chord_text(constraint.fixed_notes);
    Chord parsed=parse_roll_notes(notes);
    double score=roll_emotion_score(mode_string,notes);
    return {notes,score,chord_mask(parsed),1.0}; // Explicit user notes are never penalized or replaced.
}

}  // namespace

bool score_matches_preset(double score,int preset) {
    if (preset==-1) {
        return true;
    }
    if (preset<0 || preset>4) {
        throw std::invalid_argument("情感预设索引必须为 -1（未设置）或位于 0 到 4。");
    }
    if (preset==0) {
        return score>=0.0 && score<=20.0;
    }
    double lower=20.0*preset;
    double upper=20.0*(preset+1);
    return score>lower && score<=upper;
}

const char* emotion_preset_label(int preset) {
    static const std::array<const char*,5> labels={{
        "I 黯然","II 伤心","III 暧昧","IV 希望","V 光明"
    }};
    if (preset==-1) {
        return "ᗜ𖥦ᗜ";
    }
    if (preset<0 || preset>=static_cast<int>(labels.size())) {
        return "未知";
    }
    return labels[static_cast<std::size_t>(preset)];
}

bool quality_matches_generation_range(double score) {
    return score>65.0 && score<90.0;
}

GeneratedProgression generate_progression(
    const std::string& mode_string,
    const std::vector<ChordConstraint>& constraints
) {
    if (constraints.empty()) {
        throw std::invalid_argument("请先导入节奏或创建和弦分块。");
    }
    const std::size_t count=constraints.size();
    // 先让原算法验证调式格式，再构建候选库。
    parse_mode(mode_string);
    std::vector<Candidate> all_candidates=build_candidates(mode_string);
    std::vector<std::vector<Candidate>> pools(count);

    for (std::size_t position=0;position<count;++position) {
        if (!normalize_chord_text(constraints[position].fixed_notes).empty()) {
            pools[position].push_back(
                fixed_candidate(mode_string,constraints[position])
            );
            continue;
        }
        for (const auto& candidate:all_candidates) {
            if (score_matches_preset(
                candidate.emotion_score,
                constraints[position].emotion_preset
            )) {
                pools[position].push_back(candidate);
            }
        }
        if (pools[position].empty()) {
            std::ostringstream error;
            error<<"第 "<<position+1<<" 个和弦的情感预设 "
                 <<emotion_preset_label(constraints[position].emotion_preset)
                 <<" 在当前调式下没有候选。";
            throw std::runtime_error(error.str());
        }
    }

    static std::mt19937 random_engine([]() {
        unsigned int clock_seed=static_cast<unsigned int>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
        return clock_seed^std::random_device{}();
    }());
    constexpr int SAMPLE_COUNT=6000;
    std::vector<Trial> trials;
    trials.reserve(SAMPLE_COUNT);
    // 各分块复用同一衔接缓存，分块数量增加时仍避免重复计算。
    std::map<std::pair<unsigned int,unsigned int>,double> transition_cache;
    std::vector<std::discrete_distribution<std::size_t>> choices;
    for (const auto& pool:pools) {
        std::vector<double> weights;
        for (const auto& candidate:pool) { weights.push_back(candidate.preference); }
        choices.emplace_back(weights.begin(),weights.end());
    }

    for (int sample=0;sample<SAMPLE_COUNT;++sample) {
        Trial trial{};
        trial.preference=0.0; // Log-space keeps long progressions numerically stable.
        trial.choices.resize(count);
        for (std::size_t position=0;position<count;++position) {
            trial.choices[position]=choices[position](random_engine);
            trial.preference+=std::log(pools[position][trial.choices[position]].preference);
        }

        double transition_total=0.0;
        int repeated_chords=0;
        for (std::size_t position=0;position<count;++position) {
            const Candidate& current=pools[position][trial.choices[position]];
            std::size_t next_position=(position+1)%count;
            const Candidate& next=
                pools[next_position][trial.choices[next_position]];
            auto key=std::make_pair(current.pitch_mask,next.pitch_mask);
            auto cached=transition_cache.find(key);
            if (cached==transition_cache.end()) {
                double score=roll_transition_score(
                    mode_string,current.notes,next.notes
                );
                cached=transition_cache.insert({key,score}).first;
            }
            transition_total+=cached->second;
            repeated_chords+=count>1 && current.pitch_mask==next.pitch_mask;
        }
        // 循环进行包含尾→头；重复惩罚按分块数量归一化，保持四块时的原行为。
        trial.quality=transition_total/count-56.0*repeated_chords/count;
        trials.push_back(trial);
    }

    std::vector<const Trial*> eligible_unique;
    std::set<std::vector<std::size_t>> seen_progressions;
    for (const auto& trial:trials) {
        if (!quality_matches_generation_range(trial.quality)) { continue; }
        if (seen_progressions.insert(trial.choices).second) {
            eligible_unique.push_back(&trial);
        }
    }
    if (eligible_unique.empty()) {
        throw std::runtime_error("此次采样未找到进行质量大于 65 且小于 90 的结果。请调整分块、情感预设或行进权重后重试。");
    }
    // Prefer modal, fuller voicings over a slightly higher progression score.
    std::vector<double> result_weights;
    double best_preference=eligible_unique.front()->preference;
    for (auto trial:eligible_unique) { best_preference=std::max(best_preference,trial->preference); }
    for (auto trial:eligible_unique) { result_weights.push_back(std::exp(trial->preference-best_preference)); }
    std::discrete_distribution<std::size_t> choose_result(result_weights.begin(),result_weights.end());
    const Trial& selected=*eligible_unique[choose_result(random_engine)];

    GeneratedProgression result{};
    result.chords.resize(count);
    result.quality_score=selected.quality;
    for (std::size_t position=0;position<count;++position) {
        const Candidate& candidate=pools[position][selected.choices[position]];
        result.chords[position]={candidate.notes,candidate.emotion_score};
    }
    return result;
}

}
