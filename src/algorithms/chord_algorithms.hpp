#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ncnl{

using Chord=std::vector<int>;
using VoicePair=std::pair<int, int>;

struct ModeData{
    int tonic;
    std::string name;
    std::vector<int> intervals;
};

struct VoiceLeadingResult{
    int cost;
    std::vector<VoicePair> pairs;
};

struct ModeContext{
    double mode_fit;
    double mode_brightness;
    double root_stability;
    double adjustment;
};

struct ChordEmotionAnalysis{
    double score;
    std::string emotion;
    double consonance;
    double tension;
    double mode_fit;
    double root_stability;
    double mode_adjustment;
};

int parse_note(const std::string& note);
ModeData parse_mode(const std::string& mode_string);
Chord parse_chord(const std::string& chord_string);
int pitch_class_distance(int a, int b);
VoiceLeadingResult minimum_voice_leading(const Chord& chord1, const Chord& chord2);
int infer_chord_root(const Chord& chord);
double tonal_stability(int pc, int tonic, const std::vector<int>& mode_intervals);
double root_motion_score(int root1, int root2);
double attraction_score(
    const std::vector<VoicePair>& voice_pairs,
    int tonic,
    const std::vector<int>& mode_intervals
);
double chord_progression_score(
    const std::string& mode_string,
    const std::string& chord1_string,
    const std::string& chord2_string
);

std::vector<int> get_intervals(const Chord& chord);
double calculate_consonance(const std::vector<int>& intervals);
double calculate_tension(const std::vector<int>& intervals);
double calculate_interval_valence(const std::vector<int>& intervals);
double detect_third_character(const Chord& chord);
double calculate_cluster_density(const std::vector<int>& intervals);
ModeContext calculate_mode_context(
    const Chord& chord,
    int tonic,
    const std::string& mode_name,
    const std::vector<int>& mode_intervals
);
double chord_emotion_score(
    const std::string& mode_string,
    const std::string& chord_string
);
std::string emotion_label(double score);
ChordEmotionAnalysis analyze_chord_emotion(
    const std::string& mode_string,
    const std::string& chord_string
);

}