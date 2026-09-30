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

// 现代音乐理论中的和弦情感色彩四维评估模型：
// 1.十二平均律中不同interval class的经验参数:
//   0=同音
//   1=小二度/大七度
//   2=大二度/小七度
//   3=小三度/大六度
//   4=大三度/小六度
//   5=纯四度/纯五度
//   6=三全音
// 2.consonance:越高越协和
// 3.valence:
//   -1=偏负向/阴暗
//    0=中性
//   +1=偏正向/明亮
// 4.tension:越高越紧张
// ex.调式介入修正（如离调音）
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


// 各调式的整体明暗倾向（仅用于小幅修正）
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

}  // namespace


// I.获取和弦中所有音程
std::vector<int> get_intervals(const Chord& chord) {
    /*
    输入pitch-class和弦：[0,4,7]
    得到其中所有两两音程：
        C-E=4
        C-G=5
        E-G=3
    返回：[4,5,3]
    */
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


// II.计算协和度
double calculate_consonance(const std::vector<int>& intervals) {
    /*
    计算和弦整体协和度，映射为[0.0,1.0]
    */
    std::vector<double> values;
    for (int interval:intervals) {
        values.push_back(INTERVAL_PROFILE[interval].consonance);
    }
    return average(values);
}


// III.计算紧张度
double calculate_tension(const std::vector<int>& intervals) {
    /*
    紧张度不能简单取平均
    一个和弦中只要存在非常尖锐的：小二度/大七度/三全音
    即使其它音程很协和，听觉上仍然可能具有明显tension(紧张感)
    所以采用加权：65%-最紧张音程，35%-所有音程平均值
    */
    std::vector<double> tensions;
    for (int interval:intervals) {
        tensions.push_back(INTERVAL_PROFILE[interval].tension);
    }
    double average_tension=average(tensions);
    double peak_tension=*std::max_element(tensions.begin(),tensions.end());
    double tension=0.65*peak_tension+0.35*average_tension;
    return tension;
}


// IV.音程情感
double calculate_interval_valence(const std::vector<int>& intervals) {
    /*
    根据和弦内部所有音程估计valence
    例如，大三度倾向正向；小三度倾向负向；
    小二度、三全音等强烈不协和音程倾向降低valence
    映射并返回[-1.0,+1.0]
    */
    std::vector<double> values;
    for (int interval:intervals) {
        values.push_back(INTERVAL_PROFILE[interval].valence);
    }
    return average(values);
}


// V.尝试检测 Major/Minor三度结构
double detect_third_character(const Chord& chord) {
    /*
    先推断和弦根音，再检查根音上方的大三度（+4）或小三度（+3）。
    大三度返回 +1.0，小三度返回 -1.0；两者同时存在或都不存在时返回 0.0。
    */
    std::set<int> chord_set(chord.begin(),chord.end());
    int root=infer_chord_root(chord);
    bool has_major_third=chord_set.count((root+4)%12)!=0;
    bool has_minor_third=chord_set.count((root+3)%12)!=0;

    // 只用推断根音上方的三度判断大小调色彩；变化和弦同时含有两种三度时保持中性
    if (has_major_third && !has_minor_third) {
        return 1.0;
    }
    if (has_minor_third && !has_major_third) {
        return -1.0;
    }
    return 0.0;
}


// VI.音级密集度
double calculate_cluster_density(const std::vector<int>& intervals) {
    /*
    检测半音/全音簇
    大量相邻音级往往意味着roughness↑ / tension↑
    映射并返回[0.0~1.0]
    */
    int cluster_count=0;
    for (int interval:intervals) {
        cluster_count+=interval<=2;
    }
    return cluster_count/static_cast<double>(intervals.size());
}


// VII.“调式语境”
ModeContext calculate_mode_context(
    const Chord& chord,
    int tonic,
    const std::string& mode_name,
    const std::vector<int>& mode_intervals
) {
    /*
    计算调式对单个和弦情感色彩的修正量
    ·调式整体明暗只做轻度修正；
    ·根音越不稳定，负向修正越强；
    ·调外音会带来最明显的负向修正，避免借用和弦被自身的大三度误判为明亮
    */
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


// VIII.最终情感评分
double chord_emotion_score(
    const std::string& mode_string,
    const std::string& chord_string
) {
    /*
    0~极阴暗/强烈负向
    50~中性/暧昧
    100~极明亮/强烈正向
    */
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


    // 赋权：
    // interval valence     45%
    // major/minor character 30%
    // consonance           15%
    // tension              -7%
    // cluster              -3%
    // mode context         调式明暗、根音稳定度、调外音比例
    // 最后把-1~+1映射到0~100
    double raw_valence=(
        0.45*interval_valence
        +0.30*third_character
        +0.15*(2*consonance-1)
        -0.07*tension
        -0.03*cluster_density
        +mode_context.adjustment
    );

    // 防止越界检测
    raw_valence=std::max(-1.0,std::min(1.0,raw_valence));
    double score=50.0*(raw_valence+1.0);
    return std::max(0.0,std::min(100.0,score));
}


// IX.情感标签（Optional）
std::string emotion_label(double score) {
    /*
    数值区域映射为情感
    */
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


// X.综合分析（Optional）
ChordEmotionAnalysis analyze_chord_emotion(
    const std::string& mode_string,
    const std::string& chord_string
) {
    /*
    做 VST GUI时再用。比单独返回score更有用
    主接口chord_emotion_score()仍然只返回0~100
    */
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