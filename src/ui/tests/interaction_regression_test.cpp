#include "../chord_gui.cpp"
#include <cassert>
#include <iostream>
#include <map>

int main() {
    assert(std::all_of(slot_presets.begin(),slot_presets.end(),[](int preset){return preset==-1;}));
    initialize_rhythm();
    rhythm_splits.assign(3,false);
    chord_blocks={{0,4}};
    displayed_progression.chords.assign(1,{"C E G",0});
    slot_presets.assign(1,4);
    rhythm_splits[1]=true;
    rebuild_chord_blocks();
    assert(chord_blocks.size()==2 && slot_presets[0]==4 && slot_presets[1]==-1);
    slot_presets[1]=1;
    rhythm_splits[1]=false;
    rebuild_chord_blocks();
    assert(slot_presets.size()==1 && slot_presets[0]==4);
    assert(ROLL_TOP==255 && ROLL_GRID_TOP==307 && ROLL_GRID_HEIGHT==338);

    auto imported=ncnl::parse_midi_rhythm(read_midi_file(L"idea/presents/4.mid"));
    assert(imported.events.size()==6);
    const auto& previous=imported.events[4];
    const auto& final=imported.events[5];
    assert(previous.start+previous.duration>final.start);
    for (auto& event:imported.events) { event.pitches={60,64,67}; }
    auto schedule=ncnl::midi_playback_schedule(imported);
    assert(schedule.size()==36);
    std::map<int,bool> sounding;
    int starts=0;
    for (std::size_t i=0;i<schedule.size();) {
        auto time=schedule[i].time;
        bool any_on=false;
        while (i<schedule.size() && schedule[i].time==time) {
            const auto& note=schedule[i++];
            if (!note.on) { assert(!any_on); }
            any_on=any_on || note.on;
            if (note.on) { ++starts; }
            sounding[note.pitch]=note.on;
        }
        auto last_start=std::llround(final.start*ncnl::PLAYBACK_UNITS_PER_BEAT);
        if (time==last_start) { assert(sounding[60] && sounding[64] && sounding[67]); }
    }
    assert(starts==18 && !sounding[60] && !sounding[64] && !sounding[67]);
    ncnl::MidiRhythm adjacent;
    adjacent.length=2;
    adjacent.events={{0,1,90,{60}},{1,1,90,{60}}};
    auto ordered=ncnl::midi_playback_schedule(adjacent);
    assert(ordered[1].time==ordered[2].time && !ordered[1].on && ordered[2].on);
    std::cout<<"通过：分块默认空情感、合并保留左侧设置、卷帘坐标、4.mid 最后一拍与同音连奏。\n";
}
