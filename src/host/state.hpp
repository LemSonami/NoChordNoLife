#pragma once
#include "../midi/midi_rhythm.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ncnlplug {
constexpr std::size_t MAX_EVENTS=8192,MAX_NOTES=65536,MAX_STATE=8*1024*1024;
struct State {
    ncnl::MidiRhythm rhythm;
    std::vector<bool> splits;
    std::vector<std::string> chords;
    std::vector<int> presets;
    std::string mode="C Ionian",file;
    std::array<double,5> weights={{0.45,0.15,0.15,0.10,0.15}};
    int low=60,high=71,active=0;
    State() {
        for(int i=0;i<4;++i) rhythm.events.push_back({double(i),0.9,92,{}});
        splits.assign(3,true); chords.resize(4); presets.assign(4,-1);
    }
};
class Writer {
public:
    std::vector<unsigned char> data;
    template<class T> void number(T value) {
        auto p=reinterpret_cast<const unsigned char*>(&value); data.insert(data.end(),p,p+sizeof(T));
    }
    void text(const std::string& s) {
        number<std::uint32_t>(static_cast<std::uint32_t>(s.size())); data.insert(data.end(),s.begin(),s.end());
    }
};
class Reader {
    const unsigned char* p; std::size_t left;
public:
    Reader(const unsigned char* bytes,std::size_t size):p(bytes),left(size) {}
    template<class T> T number() {
        if(left<sizeof(T)) throw std::runtime_error("插件工程状态不完整。");
        T v; std::memcpy(&v,p,sizeof(v)); p+=sizeof(v); left-=sizeof(v); return v;
    }
    std::string text(std::size_t limit=65536) {
        auto n=number<std::uint32_t>(); if(n>limit || n>left) throw std::runtime_error("插件工程文本过长。");
        std::string s(reinterpret_cast<const char*>(p),n); p+=n; left-=n; return s;
    }
    std::size_t remaining() const { return left; }
};
inline void validate(const State& s) {
    if(s.rhythm.events.empty() || s.rhythm.events.size()>MAX_EVENTS || s.splits.size()+1!=s.rhythm.events.size() ||
        !std::isfinite(s.rhythm.length) || s.rhythm.length<1e-6 || s.rhythm.length>100000 ||
        s.low<0 || s.high>127 || s.high<s.low || s.mode.empty()) throw std::runtime_error("插件工程数据无效。");
    std::size_t blocks=1,notes=0;
    for(bool split:s.splits) if(split) ++blocks;
    if(s.chords.size()!=blocks || s.presets.size()!=blocks || s.active<0 || std::size_t(s.active)>=blocks)
        throw std::runtime_error("插件分块数据无效。");
    for(int p:s.presets) if(p< -1 || p>4) throw std::runtime_error("插件情感预设无效。");
    double last=-1,sum=0;
    for(double w:s.weights) { if(!std::isfinite(w)||w<0||w>1) throw std::runtime_error("插件权重无效。"); sum+=w; }
    if(std::abs(sum-1)>1e-6) throw std::runtime_error("插件权重总和必须为 1。");
    for(const auto& e:s.rhythm.events) {
        if(!std::isfinite(e.start)||!std::isfinite(e.duration)||e.start<0||e.start<last||e.duration<=0 ||
            e.start>=s.rhythm.length || e.duration>s.rhythm.length-e.start+1e-8 || e.velocity<1||e.velocity>127 ||e.pitches.size()>128)
            throw std::runtime_error("插件 MIDI 节奏无效。");
        last=e.start; notes+=e.pitches.size();
        for(int pitch:e.pitches) if(pitch<0||pitch>127) throw std::runtime_error("插件 MIDI 音高无效。");
    }
    if(notes>MAX_NOTES) throw std::runtime_error("插件 MIDI 音符过多。");
}
inline std::vector<unsigned char> encode(const State& s) {
    validate(s); Writer w;
    w.number<std::uint32_t>(0x4e434e4c); w.number<std::uint32_t>(1);
    w.text(s.mode); w.text(s.file); w.number(s.low); w.number(s.high); w.number(s.active);
    for(double x:s.weights) w.number(x);
    w.number(s.rhythm.length); w.number<std::uint32_t>(static_cast<std::uint32_t>(s.rhythm.events.size()));
    for(const auto& e:s.rhythm.events) {
        w.number(e.start); w.number(e.duration); w.number(e.velocity);
        w.number<std::uint32_t>(static_cast<std::uint32_t>(e.pitches.size())); for(int p:e.pitches) w.number(p);
    }
    for(bool b:s.splits) w.number<std::uint8_t>(b?1:0);
    w.number<std::uint32_t>(static_cast<std::uint32_t>(s.chords.size()));
    for(std::size_t i=0;i<s.chords.size();++i) { w.text(s.chords[i]); w.number(s.presets[i]); }
    if(w.data.size()>MAX_STATE) throw std::runtime_error("插件工程状态过大。");
    return w.data;
}
inline State decode(const unsigned char* bytes,std::size_t size) {
    if(!bytes || size>MAX_STATE) throw std::runtime_error("插件工程状态过大或为空。");
    Reader r(bytes,size); State s;
    if(r.number<std::uint32_t>()!=0x4e434e4c || r.number<std::uint32_t>()!=1) throw std::runtime_error("不支持的插件工程版本。");
    s.mode=r.text(128); s.file=r.text(); s.low=r.number<int>(); s.high=r.number<int>(); s.active=r.number<int>();
    for(double& x:s.weights) x=r.number<double>();
    s.rhythm.length=r.number<double>(); auto count=r.number<std::uint32_t>();
    if(count==0 || count>MAX_EVENTS) throw std::runtime_error("插件节奏事件数量无效。");
    s.rhythm.events.clear(); s.rhythm.events.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        ncnl::RhythmEvent e; e.start=r.number<double>(); e.duration=r.number<double>(); e.velocity=r.number<int>();
        auto n=r.number<std::uint32_t>(); if(n>128) throw std::runtime_error("插件音符数量无效。");
        for(std::uint32_t j=0;j<n;++j) e.pitches.push_back(r.number<int>());
        s.rhythm.events.push_back(std::move(e));
    }
    s.splits.clear(); for(std::uint32_t i=1;i<count;++i) {
        auto b=r.number<std::uint8_t>(); if(b>1) throw std::runtime_error("插件分块标记无效。"); s.splits.push_back(b!=0);
    }
    count=r.number<std::uint32_t>(); if(count==0 || count>MAX_EVENTS) throw std::runtime_error("插件和弦数量无效。");
    s.chords.clear(); s.presets.clear();
    for(std::uint32_t i=0;i<count;++i) { s.chords.push_back(r.text(512)); s.presets.push_back(r.number<int>()); }
    if(r.remaining()) throw std::runtime_error("插件工程状态包含未知数据。"); validate(s); return s;
}
}
