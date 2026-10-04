#include "midi_rhythm.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace ncnl {
namespace {
struct Reader {
    const std::vector<unsigned char>& bytes;
    std::size_t pos,end;
    unsigned byte() {
        if (pos>=end) { throw std::runtime_error("MIDI 文件不完整。"); }
        return bytes[pos++];
    }
    unsigned number(int count) {
        unsigned value=0;
        while (count--) { value=(value<<8)|byte(); }
        return value;
    }
    unsigned vlq() {
        unsigned value=0;
        for (int i=0;i<4;++i) {
            unsigned b=byte(); value=(value<<7)|(b&127);
            if (!(b&128)) { return value; }
        }
        throw std::runtime_error("MIDI 的变长整数无效。");
    }
    void skip(std::size_t length) {
        if (length>end-pos) { throw std::runtime_error("MIDI 事件长度无效。"); }
        pos+=length;
    }
    bool tag(const char* text) {
        for (int i=0;i<4;++i) { if (byte()!=static_cast<unsigned char>(text[i])) { return false; } }
        return true;
    }
};
struct Note { std::uint64_t start,end; int pitch,velocity; };
void be(std::vector<unsigned char>& out,unsigned value,int size) {
    for (int i=size-1;i>=0;--i) { out.push_back(static_cast<unsigned char>(value>>(8*i))); }
}
void vlq(std::vector<unsigned char>& out,unsigned value) {
    unsigned buffer=value&127;
    while ((value>>=7)) { buffer=(buffer<<8)|((value&127)|128); }
    for (;;) { out.push_back(static_cast<unsigned char>(buffer)); if (buffer&128) { buffer>>=8; } else { break; } }
}
}

MidiRhythm parse_midi_rhythm(const std::vector<unsigned char>& bytes) {
    Reader file{bytes,0,bytes.size()};
    if (!file.tag("MThd")) { throw std::runtime_error("请选择标准 MIDI 文件（.mid/.midi）。"); }
    unsigned header=file.number(4);
    if (header<6 || header>file.end-file.pos) { throw std::runtime_error("MIDI 文件头无效。"); }
    unsigned format=file.number(2),tracks=file.number(2),division=file.number(2);
    if (format>1 || !tracks) { throw std::runtime_error("当前支持同步的 MIDI Type 0 和 Type 1。"); }
    if ((division&0x8000) || !division) { throw std::runtime_error("请使用按拍计时（PPQN）的 MIDI 文件。"); }
    file.skip(header-6);
    std::vector<Note> notes;
    std::uint64_t file_end=0;
    for (unsigned track=0;track<tracks;++track) {
        if (!file.tag("MTrk")) { throw std::runtime_error("MIDI 轨道头无效。"); }
        unsigned length=file.number(4);
        if (length>file.end-file.pos) { throw std::runtime_error("MIDI 轨道长度无效。"); }
        Reader input{bytes,file.pos,file.pos+length}; file.skip(length);
        std::map<int,std::deque<std::pair<std::uint64_t,int>>> active;
        std::uint64_t tick=0;
        unsigned running=0;
        while (input.pos<input.end) {
            tick+=input.vlq();
            if (tick>0x0fffffff) { throw std::runtime_error("MIDI 时间范围过大。"); }
            unsigned status=input.byte();
            if (status<128) {
                if (!running) { throw std::runtime_error("MIDI running status 无效。"); }
                --input.pos; status=running;
            }
            if (status==255) {
                unsigned type=input.byte(),size=input.vlq();
                input.skip(size);
                if (type==47) { break; }
                continue;
            }
            if (status==240 || status==247) {
                running=0; input.skip(input.vlq()); continue;
            }
            if (status<128 || status>=240) { throw std::runtime_error("不支持的 MIDI 事件。"); }
            running=status;
            unsigned kind=status&240,channel=status&15;
            unsigned pitch=input.byte();
            unsigned value=(kind==192 || kind==208) ? 0 : input.byte();
            if (pitch>127 || value>127) { throw std::runtime_error("MIDI 数据字节无效。"); }
            int key=static_cast<int>(channel*128+pitch);
            if (kind==144 && value) {
                active[key].push_back({tick,static_cast<int>(value)});
            }
            else if (kind==128 || (kind==144 && !value)) {
                auto& queue=active[key];
                if (!queue.empty()) {
                    auto on=queue.front(); queue.pop_front();
                    if (tick>on.first) { notes.push_back({on.first,tick,static_cast<int>(pitch),on.second}); }
                }
            }
        }
        file_end=std::max(file_end,tick);
        for (const auto& entry:active) {
            for (const auto& on:entry.second) {
                if (tick>on.first) { notes.push_back({on.first,tick,entry.first%128,on.second}); }
            }
        }
    }
    if (notes.empty()) { throw std::runtime_error("MIDI 中没有可导入的音符。"); }
    // 扫描全部轨道的音符边界：每个时间片只保留最低的活动音符。
    struct Edge { std::uint64_t time; std::size_t note; bool on; };
    std::vector<Edge> edges;
    for (std::size_t i=0;i<notes.size();++i) {
        edges.push_back({notes[i].start,i,true}); edges.push_back({notes[i].end,i,false});
    }
    std::sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){ return a.time<b.time; });
    std::set<std::pair<int,std::size_t>> active;
    MidiRhythm result;
    std::size_t last_note=notes.size();
    std::uint64_t last_end=0;
    for (std::size_t i=0;i<edges.size();) {
        std::uint64_t time=edges[i].time;
        while (i<edges.size() && edges[i].time==time) {
            const auto& edge=edges[i++];
            auto key=std::make_pair(notes[edge.note].pitch,edge.note);
            if (edge.on) { active.insert(key); } else { active.erase(key); }
        }
        if (i==edges.size() || active.empty()) { last_note=notes.size(); continue; }
        std::uint64_t end=edges[i].time;
        std::size_t selected=active.begin()->second;
        double duration=static_cast<double>(end-time)/division;
        if (selected==last_note && time==last_end && !result.events.empty()) {
            result.events.back().duration+=duration;
        }
        else {
            const auto& note=notes[selected];
            result.events.push_back({static_cast<double>(time)/division,duration,note.velocity,{note.pitch}});
        }
        last_note=selected; last_end=end;
    }
    result.length=std::max(static_cast<double>(file_end)/division,
        result.events.back().start+result.events.back().duration);
    return result;
}

std::vector<unsigned char> encode_midi(const MidiRhythm& rhythm,int bpm) {
    constexpr unsigned ppqn=480;
    const double max_beats=0x0fffffff/static_cast<double>(ppqn);
    if (!std::isfinite(rhythm.length) || rhythm.length<=0 || rhythm.length>max_beats) {
        throw std::runtime_error("节奏长度超出 MIDI 导出范围。");
    }
    struct Event { unsigned tick; int pitch,velocity; bool on; };
    std::vector<Event> events;
    for (const auto& note:rhythm.events) {
        if (!std::isfinite(note.start) || !std::isfinite(note.duration) ||
            note.start<0 || note.duration<=0 || note.start+note.duration>rhythm.length ||
            note.velocity<1 || note.velocity>127) {
            throw std::runtime_error("节奏事件的时间或力度无效。");
        }
        unsigned start=static_cast<unsigned>(std::llround(note.start*ppqn));
        unsigned end=std::max(start+1,static_cast<unsigned>(std::llround((note.start+note.duration)*ppqn)));
        for (int pitch:note.pitches) {
            if (pitch<0 || pitch>127) { throw std::runtime_error("MIDI 音高超出范围。"); }
            events.push_back({start,pitch,note.velocity,true});
            events.push_back({end,pitch,0,false});
        }
    }
    std::sort(events.begin(),events.end(),[](const Event& a,const Event& b){
        if (a.tick!=b.tick) { return a.tick<b.tick; }
        return a.on<b.on;
    });
    std::vector<unsigned char> track={0,255,81,3};
    be(track,60000000/std::max(4,std::min(1000,bpm)),3);
    unsigned tick=0;
    for (const auto& event:events) {
        vlq(track,event.tick-tick); tick=event.tick;
        track.push_back(event.on ? 144 : 128);
        track.push_back(static_cast<unsigned char>(event.pitch));
        track.push_back(static_cast<unsigned char>(event.velocity));
    }
    unsigned end=std::max(tick,static_cast<unsigned>(std::llround(rhythm.length*ppqn)));
    vlq(track,end-tick); track.insert(track.end(),{255,47,0});
    std::vector<unsigned char> out={'M','T','h','d',0,0,0,6,0,0,0,1,1,224,'M','T','r','k'};
    be(out,static_cast<unsigned>(track.size()),4); out.insert(out.end(),track.begin(),track.end());
    return out;
}
}
