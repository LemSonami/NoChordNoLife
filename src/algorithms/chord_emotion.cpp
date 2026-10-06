#include "chord_algorithms.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ncnl{
namespace {

















struct IntervalProfile {
    double consonance;
    double valence;
    double tension;
};

const std::array<IntervalProfile,7> INTERVAL_PROFILE={{
    {1.00, 0.10, 0.00},
    {0.08,-1.00, 1.00},
    {0.38,-0.35, 0.72},
    {0.62,-0.30, 0.32},
    {0.88, 1.00, 0.12},
    {0.90, 0.45, 0.10},
    {0.15,-0.75, 0.92},
}};



const std::map<std::string,double> MODE_BRIGHTNESS={
    {"Lydian",0.35},
    {"Ionian",0.20},
    {"Mixolydian",0.05},
    {"Dorian",0.00},
    {"Aeolian",-0.20},
    {"Phrygian",-0.35},
    {"Locrian",-0.50},
};

double average(const std::vector<double>& values) {
    double total=0.0;
    for (double value:values) {
        total+=value;
    }
    return total/static_cast<double>(values.size());
}

double round_to_two(double value) {
    return std::round(value*100.0)/100.0;
}

}



std::vector<int> get_intervals(const Chord& chord) {








    std::vector<int> intervals;
    for (std::size_t i=0;i<chord.size();++i) {
        for (std::size_t j=i+1;j<chord.size();++j) {
            intervals.push_back(
                pitch_class_distance(chord[i],chord[j])
            );
        }
    }
    return intervals;
}



double calculate_consonance(const std::vector<int>& intervals) {



    std::vector<double> values;
    for (int interval:intervals) {
        values.push_back(INTERVAL_PROFILE[interval].consonance);
    }
    return average(values);
}



double calculate_tension(const std::vector<int>& intervals) {






    std::vector<double> tensions;
    for (int interval:intervals) {
        tensions.push_back(INTERVAL_PROFILE[interval].tension);
    }
    double average_tension=average(tensions);
    double peak_tension=*std::max_element(tensions.begin(),tensions.end());
    double tension=0.65*peak_tension+0.35*average_tension;
    return tension;
}



double calculate_interval_valence(const std::vector<int>& intervals) {






    std::vector<double> values;
    for (int interval:intervals) {
        values.push_back(INTERVAL_PROFILE[interval].valence);
    }
    return average(values);
}



double detect_third_character(const Chord& chord) {




    std::set<int> chord_set(chord.begin(),chord.end());
    int root=infer_chord_root(chord);
    bool has_major_third=chord_set.count((root+4)%12)!=0;
    bool has_minor_third=chord_set.count((root+3)%12)!=0;


    if (has_major_third && !has_minor_third) {
        return 1.0;
    }
    if (has_minor_third && !has_major_third) {
        return -1.0;
    }
    return 0.0;
}



double calculate_cluster_density(const std::vector<int>& intervals) {





    int cluster_count=0;
    for (int interval:intervals) {
        cluster_count+=interval<=2;
    }
    return cluster_count/static_cast<double>(intervals.size());
}



ModeContext calculate_mode_context(
    const Chord& chord,
    int tonic,
    const std::string& mode_name,
    const std::vector<int>& mode_intervals
) {






    std::set<int> mode_pitch_classes;
    for (int interval:mode_intervals) {
        mode_pitch_classes.insert((tonic+interval)%12);
    }
    int out_of_mode_count=0;
    for (int pc:chord) {
        out_of_mode_count+=mode_pitch_classes.count(pc)==0;
    }
    double chromatic_ratio=out_of_mode_count/static_cast<double>(chord.size());

    int root=infer_chord_root(chord);
    double root_stability=tonal_stability(root,tonic,mode_intervals);
    double mode_brightness=MODE_BRIGHTNESS.at(mode_name);

    double adjustment=(
        0.25*mode_brightness
        -0.20*(1.0-root_stability)
        -0.90*chromatic_ratio
    );
    return {
        1.0-chromatic_ratio,
        mode_brightness,
        root_stability,
        adjustment,
    };
}



double chord_emotion_score(
    const std::string& mode_string,
    const std::string& chord_string
) {





    auto mode=parse_mode(mode_string);
    Chord chord=parse_chord(chord_string);
    auto intervals=get_intervals(chord);
    double consonance=calculate_consonance(intervals);
    double tension=calculate_tension(intervals);
    double interval_valence=calculate_interval_valence(intervals);
    double third_character=detect_third_character(chord);
    double cluster_density=calculate_cluster_density(intervals);
    auto mode_context=calculate_mode_context(
        chord,
        mode.tonic,
        mode.name,
        mode.intervals
    );










    double raw_valence=(
        0.45*interval_valence
        +0.30*third_character
        +0.15*(2*consonance-1)
        -0.07*tension
        -0.03*cluster_density
        +mode_context.adjustment
    );


    raw_valence=std::max(-1.0,std::min(1.0,raw_valence));
    double score=50.0*(raw_valence+1.0);
    return std::max(0.0,std::min(100.0,score));
}



std::string emotion_label(double score) {



    if (score<20) {
        return "强烈阴暗/压迫";
    }
    else if (score<35) {
        return "阴暗/紧张";
    }
    else if (score<45) {
        return "忧郁/冷峻";
    }
    else if (score<55) {
        return "中性/暧昧";
    }
    else if (score<70) {
        return "温暖/平和";
    }
    else if (score<85) {
        return "明亮/愉悦";
    }
    return "强烈明亮/欢乐";
}



ChordEmotionAnalysis analyze_chord_emotion(
    const std::string& mode_string,
    const std::string& chord_string
) {




    auto mode=parse_mode(mode_string);
    Chord chord=parse_chord(chord_string);
    auto intervals=get_intervals(chord);
    double score=chord_emotion_score(mode_string,chord_string);
    double consonance=calculate_consonance(intervals);
    double tension=calculate_tension(intervals);
    auto mode_context=calculate_mode_context(
        chord,
        mode.tonic,
        mode.name,
        mode.intervals
    );
    return {
        round_to_two(score),
        emotion_label(score),
        round_to_two(consonance*100),
        round_to_two(tension*100),
        round_to_two(mode_context.mode_fit*100),
        round_to_two(mode_context.root_stability*100),
        round_to_two(mode_context.adjustment*50),
    };
}

}
