#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "../host/audio_engine.hpp"
#include "../host/bridge_loader.hpp"

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
const FUID PLUGIN_ID(0xD7CF18B4,0x79A54E26,0xBFA69354,0xA2E9650B);
constexpr ParamID GAIN_ID=1;
class Plugin;
class View final : public CPluginView {
    Plugin* owner;
public:
    explicit View(Plugin* plugin);
    ~View() override;
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override;
    tresult PLUGIN_API attached(void* parent,FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onSize(ViewRect* size) override;
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* size) override {
        if(!size) return kInvalidArgument;
        int side=std::clamp(std::min(size->getWidth(),size->getHeight()),600,2000);
        size->right=size->left+side; size->bottom=size->top+side; return kResultTrue;
    }
};
class Plugin final : public SingleComponentEffect {
    ncnlplug::AudioEngine engine;
    std::mutex state_mutex;
    std::vector<unsigned char> state_bytes;
    std::atomic<bool> preview{false},telemetry_playing{false};
    std::atomic<double> telemetry_bpm{120},telemetry_beat{0};
    double free_beat=0;
    std::atomic<double> gain{0.75};
    std::atomic<bool> state_loaded{false};
    bool old_preview=false;
public:
    ncnlplug::NativeBridge bridge;
    Plugin() { ncnlplug::State state; state_bytes=ncnlplug::encode(state); engine.publish(state); }
    ~Plugin() override { bridge.close(); }
    static FUnknown* create(void*) { return static_cast<IComponent*>(new Plugin); }
    std::vector<unsigned char> state() { std::lock_guard<std::mutex> lock(state_mutex); return state_bytes; }
    std::vector<unsigned char> editorState() { return state_loaded?state():std::vector<unsigned char>{}; }
    void change(const unsigned char* data,std::uint32_t size) {
        auto decoded=ncnlplug::decode(data,size);
        std::lock_guard<std::mutex> lock(state_mutex);
        engine.publish(decoded);
        state_bytes.assign(data,data+size);
        state_loaded=true;
    }
    ncnlplug::Callbacks callbacks() {
        return {this,[](void* p,const unsigned char* bytes,std::uint32_t size) {
            try {
                auto* self=static_cast<Plugin*>(p); self->change(bytes,size);
                FUnknownPtr<IComponentHandler2> handler(self->componentHandler);
                if(handler) handler->setDirty(true);
            } catch(...) {}
        },[](void* p,std::uint32_t bits) { static_cast<Plugin*>(p)->engine.enqueue(bits); },
        [](void* p,bool enabled) { static_cast<Plugin*>(p)->preview.store(enabled,std::memory_order_release); },
        [](void* p) {
            auto* self=static_cast<Plugin*>(p);
            return ncnlplug::Telemetry{self->telemetry_bpm.load(),self->telemetry_beat.load(),self->telemetry_playing.load()};
        }};
    }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        auto result=SingleComponentEffect::initialize(context); if(result!=kResultOk) return result;
        addAudioOutput(STR16("钢琴输出"),SpeakerArr::kStereo);
        addEventInput(STR16("MIDI 输入"),16); addEventOutput(STR16("生成的 MIDI"),16);
        parameters.addParameter(STR16("音量"),nullptr,0,0.75,ParameterInfo::kCanAutomate,GAIN_ID);
        processContextRequirements.needTempo().needProjectTimeMusic().needTransportState();
        return kResultOk;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if(!std::isfinite(setup.sampleRate)||setup.sampleRate<=0||setup.sampleRate>10000000) return kInvalidArgument;
        auto result=SingleComponentEffect::setupProcessing(setup); engine.setup(setup.sampleRate); free_beat=0; return result;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override {
        return size==kSample32 || size==kSample64 ? kResultTrue:kResultFalse;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*,int32 inputs,SpeakerArrangement* outputs,int32 count) override {
        if(inputs!=0 || count!=1 || !outputs || (outputs[0]!=SpeakerArr::kStereo && outputs[0]!=SpeakerArr::kMono)) return kResultFalse;
        auto* bus=static_cast<AudioBus*>(audioOutputs.at(0).get()); if(!bus) return kResultFalse;
        bus->setArrangement(outputs[0]); return kResultTrue;
    }
    tresult PLUGIN_API setActive(TBool active) override {
        if(!active) { engine.reset(); preview.store(false); telemetry_playing.store(false); }
        return SingleComponentEffect::setActive(active);
    }
    tresult PLUGIN_API setProcessing(TBool running) override {
        if(!running) { engine.reset(); telemetry_playing.store(false); }
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return static_cast<uint32>(processSetup.sampleRate); }
    IPlugView* PLUGIN_API createView(FIDString name) override {
        return name && std::strcmp(name,ViewType::kEditor)==0 ? new View(this):nullptr;
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        if(!stream) return kInvalidArgument;
        if(bridge.api) bridge.api->flush();
        auto bytes=state(); std::uint32_t size=static_cast<std::uint32_t>(bytes.size()); int32 written=0;
        if(stream->write(&size,sizeof(size),&written)!=kResultOk || written!=sizeof(size)) return kResultFalse;
        if(stream->write(bytes.data(),size,&written)!=kResultOk || written!=int32(size)) return kResultFalse;
        double volume=gain.load();
        return stream->write(&volume,sizeof(volume),&written)==kResultOk && written==sizeof(volume)?kResultOk:kResultFalse;
    }
    tresult PLUGIN_API setState(IBStream* stream) override {
        if(!stream) return kInvalidArgument;
        std::uint32_t size=0; int32 read=0;
        if(stream->read(&size,sizeof(size),&read)!=kResultOk || read!=sizeof(size)||size>ncnlplug::MAX_STATE) return kResultFalse;
        std::vector<unsigned char> bytes(size);
        if(stream->read(bytes.data(),size,&read)!=kResultOk || read!=int32(size)) return kResultFalse;
        double volume=0;
        if(stream->read(&volume,sizeof(volume),&read)!=kResultOk || read!=sizeof(volume)||!std::isfinite(volume)||volume<0||volume>1) return kResultFalse;
        try { change(bytes.data(),size); gain.store(volume); setParamNormalized(GAIN_ID,volume);
            if(bridge.api) bridge.api->restore(bytes.data(),size); return kResultOk; }
        catch(...) { return kResultFalse; }
    }
    tresult PLUGIN_API setComponentState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& data) override {
        IParamValueQueue* volume_changes=nullptr;
        if(data.inputParameterChanges) for(int32 i=0;i<data.inputParameterChanges->getParameterCount();++i) {
            auto* queue=data.inputParameterChanges->getParameterData(i);
            if(queue && queue->getParameterId()==GAIN_ID && queue->getPointCount()) {
                volume_changes=queue;
            }
        }
        if(data.numSamples<0) return kInvalidArgument;
        if(data.numSamples==0) {
            if(volume_changes) for(int32 i=0;i<volume_changes->getPointCount();++i) {
                int32 offset=0; ParamValue value=0;
                if(volume_changes->getPoint(i,offset,value)==kResultOk && std::isfinite(value)) gain=std::clamp(value,0.0,1.0);
            }
            return kResultOk;
        }
        auto emit=[&](ncnlplug::MidiMessage m,int offset) {
            if(!data.outputEvents || data.numSamples==0) return;
            Event event{}; event.busIndex=0; event.sampleOffset=std::clamp(offset,0,data.numSamples-1);
            if(m.on) {
                event.type=Event::kNoteOnEvent; event.noteOn.channel=m.channel; event.noteOn.pitch=m.pitch;
                event.noteOn.velocity=m.velocity/127.0f; event.noteOn.noteId=-1;
            } else {
                event.type=Event::kNoteOffEvent; event.noteOff.channel=m.channel; event.noteOff.pitch=m.pitch;
                event.noteOff.velocity=0; event.noteOff.noteId=-1;
            }
            data.outputEvents->addEvent(event);
        };
        double bpm=120,beat=free_beat;
        bool host_playing=false;
        auto* context=data.processContext;
        if(context) {
            if((context->state&ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo>0) bpm=context->tempo;
            host_playing=(context->state&ProcessContext::kPlaying)!=0;
            if((context->state&ProcessContext::kProjectTimeMusicValid) && std::isfinite(context->projectTimeMusic)) beat=context->projectTimeMusic;
        }
        bpm=std::clamp(bpm,1.0,1000.0);
        bool manual=preview.load(std::memory_order_acquire);
        if(manual && !host_playing) { if(!old_preview) free_beat=0; beat=free_beat; }
        old_preview=manual;
        double step=bpm/(60.0*processSetup.sampleRate);
        bool playing=host_playing || manual;
        engine.drain(emit); engine.begin(beat,step,data.numSamples,playing,emit);
        int32 input_count=data.inputEvents?data.inputEvents->getEventCount():0,next_input=0;
        Event incoming{}; bool has_input=input_count && data.inputEvents->getEvent(0,incoming)==kResultOk;
        bool silent=true;
        float level=static_cast<float>(gain.load(std::memory_order_relaxed));
        int32 volume_index=0,volume_offset=0; ParamValue volume_value=0;
        bool has_volume=volume_changes && volume_changes->getPoint(0,volume_offset,volume_value)==kResultOk;
        for(int32 sample=0;sample<data.numSamples;++sample) {
            while(has_volume && volume_offset<=sample) {
                if(std::isfinite(volume_value)) { level=static_cast<float>(std::clamp(volume_value,0.0,1.0)); gain.store(level,std::memory_order_relaxed); }
                has_volume=++volume_index<volume_changes->getPointCount() && volume_changes->getPoint(volume_index,volume_offset,volume_value)==kResultOk;
            }
            while(has_input && incoming.sampleOffset<=sample) {
                if(incoming.type==Event::kNoteOnEvent) engine.note({incoming.noteOn.pitch,
                    int(incoming.noteOn.velocity*127),int(incoming.noteOn.channel)+16,true});
                else if(incoming.type==Event::kNoteOffEvent) engine.note({incoming.noteOff.pitch,0,int(incoming.noteOff.channel)+16,false});
                has_input=++next_input<input_count && data.inputEvents->getEvent(next_input,incoming)==kResultOk;
            }
            engine.tick(beat+sample*step,sample,emit);
            float output=engine.render()*level;
            silent=silent && output==0;
            for(int32 bus=0;bus<data.numOutputs;++bus) for(int32 channel=0;channel<data.outputs[bus].numChannels;++channel) {
                if(data.symbolicSampleSize==kSample32) {
                    if(data.outputs[bus].channelBuffers32 && data.outputs[bus].channelBuffers32[channel]) data.outputs[bus].channelBuffers32[channel][sample]=output;
                } else if(data.outputs[bus].channelBuffers64 && data.outputs[bus].channelBuffers64[channel]) data.outputs[bus].channelBuffers64[channel][sample]=output;
            }
        }
        for(int32 bus=0;bus<data.numOutputs;++bus) data.outputs[bus].silenceFlags=silent?((uint64(1)<<data.outputs[bus].numChannels)-1):0;
        engine.end();
        if(playing) free_beat=beat+data.numSamples*step;
        telemetry_bpm.store(bpm,std::memory_order_relaxed);
        telemetry_beat.store(beat,std::memory_order_relaxed); telemetry_playing.store(playing,std::memory_order_relaxed);
        return kResultOk;
    }
};
View::View(Plugin* plugin):owner(plugin) { owner->addRef(); rect={0,0,1000,1000}; }
View::~View() { if(systemWindow) removed(); owner->release(); }
tresult PLUGIN_API View::isPlatformTypeSupported(FIDString type) { return type && std::strcmp(type,kPlatformTypeHWND)==0?kResultTrue:kResultFalse; }
tresult PLUGIN_API View::attached(void* parent,FIDString type) {
    if(!parent || isPlatformTypeSupported(type)!=kResultTrue) return kResultFalse;
    if(!owner->bridge.attach(static_cast<HWND>(parent),owner->callbacks(),owner->editorState())) return kResultFalse;
    owner->bridge.api->resize(rect.getWidth(),rect.getHeight()); return CPluginView::attached(parent,type);
}
tresult PLUGIN_API View::removed() { if(owner->bridge.api) owner->bridge.api->detach(); return CPluginView::removed(); }
tresult PLUGIN_API View::onSize(ViewRect* size) {
    if(!size) return kInvalidArgument;
    if(owner->bridge.api) owner->bridge.api->resize(size->getWidth(),size->getHeight()); return CPluginView::onSize(size);
}
}

bool InitModule() { return true; }
bool DeinitModule() { return true; }
BEGIN_FACTORY_DEF("NoChordNoLife","https://github.com/LemSonami/NoChordNoLife","")
DEF_CLASS2(INLINE_UID_FROM_FUID(PLUGIN_ID),PClassInfo::kManyInstances,kVstAudioEffectClass,
    "NoChordNoLife",0,"Instrument|MIDI","0.3.1",kVstVersionString,Plugin::create)
END_FACTORY
