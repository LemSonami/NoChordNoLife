#pragma once
#include "state.hpp"
#include "../midi/midi_playback.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

namespace ncnlplug {
struct MidiMessage { int pitch,velocity,channel; bool on; };
struct Scheduled { double beat; int pitch,velocity; bool on; };
struct Snapshot {
    double length=4;
    std::uint64_t revision=0;
    std::size_t count=0;
    std::array<Scheduled,MAX_NOTES*2> events{};
};
class AudioEngine {
    std::unique_ptr<std::array<Snapshot,3>> snapshots=std::make_unique<std::array<Snapshot,3>>();
    std::atomic<int> published{0},reader{-1};
    std::mutex writer;
    std::uint64_t next_revision=1,last_revision=0;
    const Snapshot* current=nullptr;
    std::array<unsigned,128> held{};
    std::size_t next=0;
    double cycle=0,last_end=0;
    bool was_running=false;
    struct Voice { double phase=0,step=0,age=0,amplitude=0,release=1; int pitch=-1,channel=0; bool down=false; };
    std::array<Voice,128> voices{};
    std::array<std::uint32_t,256> queue{};
    std::atomic<unsigned> queue_read{0},queue_write{0};
    std::atomic<bool> queue_panic{false};
    double sample_rate=44100;
public:
    void publish(const State& s) {
        validate(s);
        std::lock_guard<std::mutex> lock(writer);
        int write=0;
        while(write==published.load(std::memory_order_seq_cst) || write==reader.load(std::memory_order_seq_cst)) ++write;
        auto& dest=(*snapshots)[write]; dest.length=s.rhythm.length; dest.count=0;
        for(const auto& e:ncnl::midi_playback_schedule(s.rhythm))
            dest.events[dest.count++]={std::min(e.time/ncnl::PLAYBACK_UNITS_PER_BEAT,dest.length),e.pitch,e.velocity,e.on};
        const auto& old=(*snapshots)[published.load(std::memory_order_seq_cst)];
        bool same=old.length==dest.length && old.count==dest.count;
        for(std::size_t i=0;same && i<dest.count;++i) {
            const auto& a=old.events[i]; const auto& b=dest.events[i];
            same=a.beat==b.beat && a.pitch==b.pitch && a.velocity==b.velocity && a.on==b.on;
        }
        if(same) return;
        dest.revision=next_revision++;
        published.store(write,std::memory_order_seq_cst);
    }
    void setup(double rate) { sample_rate=rate; reset(); }
    void reset() {
        for(auto& v:voices) v={}; was_running=false; last_revision=0;
        queue_read.store(queue_write.load(std::memory_order_acquire),std::memory_order_release);
    }
    void enqueue(std::uint32_t message) {
        auto write=queue_write.load(std::memory_order_relaxed);
        if(write-queue_read.load(std::memory_order_acquire)>=queue.size()) { queue_panic.store(true); return; }
        queue[write%queue.size()]=message; queue_write.store(write+1,std::memory_order_release);
    }
    void note(MidiMessage m) {
        if(m.pitch<0||m.pitch>127) return;
        if(m.on && m.velocity>0) {
            Voice* chosen=nullptr;
            for(auto& v:voices) if(v.pitch<0) { chosen=&v; break; }
            if(!chosen) chosen=&*std::min_element(voices.begin(),voices.end(),[](const Voice& a,const Voice& b){return a.amplitude*a.release<b.amplitude*b.release;});
            *chosen={0,2*3.141592653589793*440.0*std::pow(2.0,(m.pitch-69)/12.0)/sample_rate,
                0,m.velocity/127.0,1,m.pitch,m.channel,true};
        } else for(auto& v:voices) if(v.pitch==m.pitch && v.channel==m.channel && v.down) v.down=false;
    }
    template<class Emit> void drain(Emit emit) {
        if(queue_panic.exchange(false)) {
            for(auto& v:voices) if(v.channel==1 && v.pitch>=0) { emit({v.pitch,0,1,false},0); v.down=false; }
        }
        unsigned read=queue_read.load(std::memory_order_relaxed),write=queue_write.load(std::memory_order_acquire);
        while(read!=write) {
            auto bits=queue[read++%queue.size()]; int status=bits&0xf0;
            if(status==0x90 || status==0x80) {
                MidiMessage m{int((bits>>8)&127),int((bits>>16)&127),int(bits&15),status==0x90 && ((bits>>16)&127)!=0};
                note(m); emit(m,0);
            }
        }
        queue_read.store(read,std::memory_order_release);
    }
    template<class Emit> void silence(Emit emit,int offset) {
        for(int pitch=0;pitch<128;++pitch) if(held[pitch]) { held[pitch]=0; note({pitch,0,2,false}); emit({pitch,0,0,false},offset); }
    }
    template<class Emit> void begin(double beat,double step,int samples,bool running,Emit emit) {
        int index;
        do { index=published.load(std::memory_order_seq_cst); reader.store(index,std::memory_order_seq_cst); }
        while(index!=published.load(std::memory_order_seq_cst));
        current=&(*snapshots)[index];
        if(!std::isfinite(beat)||std::abs(beat)>1e9||!std::isfinite(step)||step<=0) running=false;
        bool seek=!was_running || last_revision!=current->revision || std::abs(beat-last_end)>step*2.5;
        if(!running) {
            silence(emit,0);
        } else if(seek) {
            silence(emit,0);
            cycle=std::floor(beat/current->length)*current->length;
            double phase=beat-cycle; next=0;
            std::array<int,128> velocity{};
            while(next<current->count && current->events[next].beat<phase-1e-10) {
                const auto& e=current->events[next++];
                if(e.on) { ++held[e.pitch]; velocity[e.pitch]=e.velocity; }
                else if(held[e.pitch]) --held[e.pitch];
            }
            for(int p=0;p<128;++p) if(held[p]) { note({p,velocity[p],2,true}); emit({p,velocity[p],0,true},0); }
        }
        was_running=running; last_revision=current->revision; last_end=beat+step*samples;
    }
    template<class Emit> void tick(double beat,int offset,Emit emit) {
        if(!was_running || !current || !current->count) return;
        while(true) {
            if(next==current->count) { cycle+=current->length; next=0; }
            const auto& e=current->events[next];
            if(cycle+e.beat>beat+1e-10) break;
            ++next;
            if(e.on) {
                if(held[e.pitch]++==0) { note({e.pitch,e.velocity,2,true}); emit({e.pitch,e.velocity,0,true},offset); }
            } else if(held[e.pitch] && --held[e.pitch]==0) {
                note({e.pitch,0,2,false}); emit({e.pitch,0,0,false},offset);
            }
        }
    }
    float render() {
        double result=0,delta=1/sample_rate;
        for(auto& v:voices) if(v.pitch>=0) {
            v.age+=delta;
            if(!v.down) v.release*=std::exp(-delta/0.085);
            if(v.release<0.00003) { v.pitch=-1; continue; }
            double envelope=std::min(1.0,v.age/0.003)*(0.14+0.86*std::exp(-v.age*2.4))*v.release;
            double wave=std::sin(v.phase)+0.32*std::sin(2*v.phase)*std::exp(-v.age*1.4)
                +0.14*std::sin(3*v.phase)*std::exp(-v.age*3.2)+0.055*std::sin(7*v.phase)*std::exp(-v.age*7.0);
            result+=wave*envelope*v.amplitude*0.11;
            v.phase+=v.step; if(v.phase>=6.283185307179586) v.phase-=6.283185307179586;
        }
        return static_cast<float>(std::tanh(result));
    }
    void end() { current=nullptr; reader.store(-1,std::memory_order_seq_cst); }
};
}
