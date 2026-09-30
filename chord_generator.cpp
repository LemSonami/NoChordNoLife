#include "chord_generator.hpp"

#include "chord_algorithms.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
};

struct Trial {
    std::array<std::size_t,4> choices;
    double quality;
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
        candidates.push_back({
            item.first,
            chord_emotion_score(mode_string,item.first),
            item.second,
        });
    }
    return candidates;
}

Candidate fixed_candidate(
    const std::string& mode_string,
    const ChordConstraint& constraint
) {
    std::string notes=normalize_chord_text(constraint.fixed_notes);
    Chord parsed=parse_chord(notes);
    double score=chord_emotion_score(mode_string,notes);
    return {notes,score,chord_mask(parsed)};
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
        "0-20","21-40","41-60","61-80","81-100"
    }};
    if (preset==-1) {
        return "未设置";
    }
    if (preset<0 || preset>=static_cast<int>(labels.size())) {
        return "未知";
    }
    return labels[static_cast<std::size_t>(preset)];
}

GeneratedProgression generate_progression(
    const std::string& mode_string,
    const std::array<ChordConstraint,4>& constraints
) {
    // 先让原算法验证调式格式，再构建候选库。
    parse_mode(mode_string);
    std::vector<Candidate> all_candidates=build_candidates(mode_string);
    std::array<std::vector<Candidate>,4> pools;

    for (int position=0;position<4;++position) {
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
    std::array<std::map<std::pair<unsigned int,unsigned int>,double>,3>
        transition_cache;

    for (int sample=0;sample<SAMPLE_COUNT;++sample) {
        Trial trial{};
        for (int position=0;position<4;++position) {
            std::uniform_int_distribution<std::size_t> choose(
                0,pools[position].size()-1
            );
            trial.choices[position]=choose(random_engine);
        }

        double transition_total=0.0;
        int repeated_chords=0;
        for (int position=0;position<3;++position) {
            const Candidate& current=pools[position][trial.choices[position]];
            const Candidate& next=pools[position+1][trial.choices[position+1]];
            auto key=std::make_pair(current.pitch_mask,next.pitch_mask);
            auto cached=transition_cache[position].find(key);
            if (cached==transition_cache[position].end()) {
                double score=chord_progression_score(
                    mode_string,current.notes,next.notes
                );
                cached=transition_cache[position].insert({key,score}).first;
            }
            transition_total+=cached->second;
            repeated_chords+=current.pitch_mask==next.pitch_mask;
        }
        // 原进行分数偏爱完全静止；生成层轻度惩罚连续重复，避免四个位置反复同一和弦。
        trial.quality=transition_total/3.0-14.0*repeated_chords;
        trials.push_back(trial);
    }

    std::sort(
        trials.begin(),trials.end(),
        [](const Trial& left,const Trial& right) {
            return left.quality>right.quality;
        }
    );

    std::vector<const Trial*> top_unique;
    std::set<std::array<std::size_t,4>> seen_progressions;
    for (const auto& trial:trials) {
        if (seen_progressions.insert(trial.choices).second) {
            top_unique.push_back(&trial);
            if (top_unique.size()==30) {
                break;
            }
        }
    }
    std::uniform_int_distribution<std::size_t> choose_top(0,top_unique.size()-1);
    const Trial& selected=*top_unique[choose_top(random_engine)];

    GeneratedProgression result{};
    result.quality_score=selected.quality;
    for (int position=0;position<4;++position) {
        const Candidate& candidate=pools[position][selected.choices[position]];
        result.chords[position]={candidate.notes,candidate.emotion_score};
    }
    return result;
}

}  // namespace ncnl
