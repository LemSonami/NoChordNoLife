#include "chord_algorithms.hpp"
#include "../config/progression_config.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace ncnl{
namespace {




const std::map<std::string, std::vector<int>> MODE_INTERVALS={
    {"Ionian",    {0,2,4,5,7,9,11}},
    {"Dorian",    {0,2,3,5,7,9,10}},
    {"Phrygian",  {0,1,3,5,7,8,10}},
    {"Lydian",    {0,2,4,6,7,9,11}},
    {"Mixolydian",{0,2,4,5,7,9,10}},
    {"Aeolian",   {0,2,3,5,7,8,10}},
    {"Locrian",   {0,1,3,5,6,8,10}},
};


const std::map<char, int> NATURAL_PC={
    {'C',0},
    {'D',2},
    {'E',4},
    {'F',5},
    {'G',7},
    {'A',9},
    {'B',11},
};



const std::vector<std::set<int>> CHORD_TEMPLATES={
    {0,4,7},
    {0,3,7},
    {0,3,6},
    {0,4,8},
    {0,2,7},
    {0,5,7},
    {0,4,7,10},
    {0,4,7,11},
    {0,3,7,10},
    {0,3,6,10},
    {0,3,6,9},
};

bool contains(const std::vector<int>& values, int value) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}

}



int parse_note(const std::string& note) {
    static const std::regex note_pattern(R"([A-G](?:#|b)?)");
    if (!std::regex_match(note,note_pattern)) {
        throw std::invalid_argument(
            "你好坏！这是非法音名: '"+note+"'。"
            "必须形如 C C# Db 喔，且音名字母必须大写捏~"
        );
    }
    int pc=NATURAL_PC.at(note[0]);
    if (note.size()==2) {
        if (note[1]=='#') {
            pc+=1;
        }
        else if (note[1]=='b') {
            pc-=1;
        }
    }
    return (pc%12+12)%12;
}



ModeData parse_mode(const std::string& mode_string) {
    std::istringstream input(mode_string);
    std::string tonic_name;
    std::string mode_name;
    std::string extra;
    if (!(input>>tonic_name>>mode_name) || (input>>extra)) {
        throw std::invalid_argument(
            "调式必须写成类似于 C Ionian 的格式捏~（中间用空格分开）"
        );
    }
    int tonic=parse_note(tonic_name);
    auto mode=MODE_INTERVALS.find(mode_name);
    if (mode==MODE_INTERVALS.end()) {
        throw std::invalid_argument(
            "调式名称必须严格为以下七种之一捏：\n"
            "Ionian/Dorian/Phrygian/Lydian/"
            "Mixolydian/Aeolian/Locrian"
        );
    }
    return {tonic,mode_name,mode->second};
}



Chord parse_chord(const std::string& chord_string) {
    std::istringstream input(chord_string);
    std::vector<std::string> notes;
    std::string note;
    while (input>>note) {
        notes.push_back(note);
    }
    if (notes.size()<2) {
        throw std::invalid_argument("只有一个音不能算和弦喵！哈！！！");
    }
    Chord pcs;
    for (const auto& current_note:notes) {
        int pc=parse_note(current_note);


        if (!contains(pcs,pc)) {
            pcs.push_back(pc);
        }
    }
    if (pcs.size()<2) {
        throw std::invalid_argument("只有一个音不能算和弦喵！哈！！！");
    }
    return pcs;
}



int pitch_class_distance(int a,int b) {







    int d=std::abs(a-b)%12;
    return std::min(d,12-d);
}



VoiceLeadingResult minimum_voice_leading(const Chord& chord1,const Chord& chord2) {




    Chord source=chord1;
    Chord target=chord2;
    bool swapped=false;

    if (source.size()>target.size()) {
        std::swap(source,target);
        swapped=true;
    }

    std::unordered_map<unsigned long long,VoiceLeadingResult> cache;
    std::function<VoiceLeadingResult(std::size_t,unsigned int)> dp;
    dp=[&](std::size_t i,unsigned int used_mask)->VoiceLeadingResult {
        if (i==source.size()) {
            return {0,{}};
        }
        unsigned long long key=(static_cast<unsigned long long>(i)<<32)|used_mask;
        auto cached=cache.find(key);
        if (cached!=cache.end()) {
            return cached->second;
        }
        int best_cost=std::numeric_limits<int>::max();
        std::vector<VoicePair> best_pairs;
        for (std::size_t j=0;j<target.size();++j) {
            if (used_mask&(1u<<j)) {
                continue;
            }
            int d=pitch_class_distance(source[i],target[j]);
            auto remaining=dp(i+1,used_mask|(1u<<j));
            int total_cost=d+remaining.cost;
            VoicePair pair=swapped
                ? VoicePair{target[j],source[i]}
                : VoicePair{source[i],target[j]};
            if (total_cost<best_cost) {
                best_cost=total_cost;
                best_pairs=remaining.pairs;
                best_pairs.insert(best_pairs.begin(),pair);
            }
        }
        VoiceLeadingResult result{best_cost,best_pairs};
        cache.emplace(key,result);
        return result;
    };
    return dp(0,0);
}



int infer_chord_root(const Chord& chord) {









    std::set<int> chord_set(chord.begin(),chord.end());
    bool has_best=false;
    int best_error=0;
    int best_root=0;
    for (int root:chord_set) {
        std::set<int> relative;
        for (int pc:chord_set) {
            relative.insert((pc-root+12)%12);
        }
        for (const auto& chord_template:CHORD_TEMPLATES) {
            int missing=0;
            int extra=0;
            for (int pc:chord_template) {
                missing+=relative.count(pc)==0;
            }
            for (int pc:relative) {
                extra+=chord_template.count(pc)==0;
            }


            int error=missing*2+extra;
            if (!has_best || std::tie(error,root)<std::tie(best_error,best_root)) {
                has_best=true;
                best_error=error;
                best_root=root;
            }
        }
    }

    if (!has_best || best_error>2) {
        return chord.front();
    }
    return best_root;
}



double tonal_stability(int pc,int tonic,const std::vector<int>& mode_intervals) {






    int relative_pc=(pc-tonic+12)%12;
    auto found=std::find(mode_intervals.begin(),mode_intervals.end(),relative_pc);
    if (found==mode_intervals.end()) {
        return 0.10;
    }
    std::size_t degree=static_cast<std::size_t>(found-mode_intervals.begin());

    const std::vector<double> stability_by_degree={
        1.00,
        0.55,
        0.75,
        0.62,
        0.88,
        0.50,
        0.42,
    };
    return stability_by_degree[degree];
}



double root_motion_score(int root1,int root2) {
    int d=pitch_class_distance(root1,root2);
    const std::map<int,double> scores={
        {0,1.00},
        {1,0.68},
        {2,0.72},
        {3,0.82},
        {4,0.84},
        {5,1.00},
        {6,0.35},
    };
    return scores.at(d);
}



double attraction_score(
    const std::vector<VoicePair>& voice_pairs,
    int tonic,
    const std::vector<int>& mode_intervals
) {







    std::vector<double> scores;
    for (const auto& voice_pair:voice_pairs) {
        int source=voice_pair.first;
        int target=voice_pair.second;
        int distance=pitch_class_distance(
            source,
            target
        );
        double source_stability=tonal_stability(
            source,
            tonic,
            mode_intervals
        );
        double target_stability=tonal_stability(
            target,
            tonic,
            mode_intervals
        );
        double stability_gain=std::max(
            0.0,
            target_stability-source_stability
        );

        double score;

        if (distance==0) {
            score=0.65;
        }


        else if (distance==1) {
            score=0.75+0.25*stability_gain;
        }


        else if (distance==2) {
            score=0.55+0.25*stability_gain;
        }


        else if (distance<=4) {
            score=0.30+0.20*stability_gain;
        }


        else {
            score=0.12;
        }

        scores.push_back(std::min(1.0,score));
    }
    double total=0.0;
    for (double score:scores) {
        total+=score;
    }
    return total/static_cast<double>(scores.size());
}



double chord_progression_score(
    const std::string& mode_string,
    const std::string& chord1_string,
    const std::string& chord2_string
) {
    auto mode=parse_mode(mode_string);
    Chord chord1=parse_chord(chord1_string);
    Chord chord2=parse_chord(chord2_string);


    auto voice_leading=minimum_voice_leading(chord1,chord2);


    auto cardinality_difference=std::abs(
        static_cast<int>(chord1.size())-static_cast<int>(chord2.size())
    );
    double unmatched_penalty=2.5*cardinality_difference;
    double average_voice_distance=(voice_leading.cost+unmatched_penalty)/
        static_cast<double>(std::max(chord1.size(),chord2.size()));


    double voice_leading_score=std::exp(-average_voice_distance/2.8);


    std::set<int> chord1_set(chord1.begin(),chord1.end());
    std::set<int> chord2_set(chord2.begin(),chord2.end());
    int common_tones=0;
    for (int pc:chord1_set) {
        common_tones+=chord2_set.count(pc)!=0;
    }
    double common_tone_score=common_tones/
        static_cast<double>(std::min(chord1.size(),chord2.size()));


    std::set<int> mode_pitch_classes;
    for (int interval:mode.intervals) {
        mode_pitch_classes.insert((mode.tonic+interval)%12);
    }
    int chord1_mode_count=0;
    for (int pc:chord1) {
        chord1_mode_count+=mode_pitch_classes.count(pc)!=0;
    }
    int chord2_mode_count=0;
    for (int pc:chord2) {
        chord2_mode_count+=mode_pitch_classes.count(pc)!=0;
    }
    double chord1_mode_ratio=chord1_mode_count/static_cast<double>(chord1.size());
    double chord2_mode_ratio=chord2_mode_count/static_cast<double>(chord2.size());
    double modal_score=(chord1_mode_ratio+chord2_mode_ratio)/2.0;


    int root1=infer_chord_root(chord1);
    int root2=infer_chord_root(chord2);
    double harmonic_root_score=root_motion_score(root1,root2);


    double resolution_score=attraction_score(
        voice_leading.pairs,
        mode.tonic,
        mode.intervals
    );


    const auto& weights=progression_weights().values;
    double final_score=100.0*(
        weights[static_cast<std::size_t>(ProgressionWeight::voice_leading)]*
            voice_leading_score
        +weights[static_cast<std::size_t>(ProgressionWeight::common_tone)]*
            common_tone_score
        +weights[static_cast<std::size_t>(ProgressionWeight::modal_consistency)]*
            modal_score
        +weights[static_cast<std::size_t>(ProgressionWeight::root_motion)]*
            harmonic_root_score
        +weights[static_cast<std::size_t>(ProgressionWeight::tonal_attraction)]*
            resolution_score);
    return std::max(0.0,std::min(100.0,final_score));
}

}
