#pragma once

#include "../algorithms/chord_algorithms.hpp"
#include <algorithm>
#include <sstream>

namespace ncnl{

// 卷帘编辑允许空音组和单音；两个及以上音仍使用原算法评分
inline Chord parse_roll_notes(const std::string& text) {
    Chord notes;
    std::istringstream input(text);
    std::string name;
    while (input>>name) {
        int pitch=parse_note(name);
        if (std::find(notes.begin(),notes.end(),pitch)==notes.end()) {
            notes.push_back(pitch);
        }
    }
    return notes;
}

inline double roll_emotion_score(const std::string& mode,const std::string& notes) {
    return parse_roll_notes(notes).size()<2 ? 0.0 : chord_emotion_score(mode,notes);
}

inline double roll_transition_score(
    const std::string& mode,const std::string& source,const std::string& target
) {
    return parse_roll_notes(source).size()<2 || parse_roll_notes(target).size()<2
        ? 0.0 : chord_progression_score(mode,source,target);
}

}
