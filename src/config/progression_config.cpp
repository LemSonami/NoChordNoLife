#include "progression_config.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ncnl{
namespace {

const std::array<const char*,5> WEIGHT_KEYS={{
    "voice_leading_efficiency",
    "common_tone_retention",
    "modal_consistency",
    "root_motion",
    "tonal_attraction",
}};

ProgressionWeights active_weights=default_progression_weights();

ProgressionWeights normalized(ProgressionWeights weights) {
    double sum=0.0;
    for (double& value:weights.values) {
        if (!std::isfinite(value)) {
            value=0.0;
        }
        value=std::max(0.0,value);
        sum+=value;
    }
    if (sum<=1e-12) {
        return default_progression_weights();
    }
    for (double& value:weights.values) {
        value/=sum;
    }
    return weights;
}

bool read_number(const std::string& json,const std::string& key,double& result) {
    std::string marker='"'+key+'"';
    std::size_t position=json.find(marker);
    if (position==std::string::npos) {
        return false;
    }
    position=json.find(':',position+marker.size());
    if (position==std::string::npos) {
        return false;
    }
    std::istringstream input(json.substr(position+1));
    input>>result;
    return !input.fail() && std::isfinite(result) && result>=0.0;
}

}

ProgressionWeights default_progression_weights() {
    return {{{0.45,0.15,0.15,0.10,0.15}}};
}

const ProgressionWeights& progression_weights() {
    return active_weights;
}

void set_progression_weights(const ProgressionWeights& weights) {
    active_weights=normalized(weights);
}

void adjust_progression_weight(std::size_t index,double value) {
    if (index>=active_weights.values.size()) {
        return;
    }
    value=std::max(0.0,std::min(1.0,value));
    double old_value=active_weights.values[index];
    double old_other_sum=1.0-old_value;
    double new_other_sum=1.0-value;

    if (old_other_sum>1e-12) {
        double ratio=new_other_sum/old_other_sum;
        for (std::size_t current=0;current<active_weights.values.size();++current) {
            if (current!=index) {
                active_weights.values[current]*=ratio;
            }
        }
    }
    else {
        double share=new_other_sum/4.0;
        for (std::size_t current=0;current<active_weights.values.size();++current) {
            if (current!=index) {
                active_weights.values[current]=share;
            }
        }
    }
    active_weights.values[index]=value;
    active_weights=normalized(active_weights);
}

bool load_progression_config(const std::string& path) {
#ifdef _WIN32
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
    if (!length) { return false; }
    std::wstring wide(length,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&wide[0],length);
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) { return false; }
    DWORD size=GetFileSize(file,nullptr),count=0;
    if (size==INVALID_FILE_SIZE || size>1048576) { CloseHandle(file); return false; }
    std::string json(size,'\0'); BOOL read=ReadFile(file,size?&json[0]:nullptr,size,&count,nullptr); CloseHandle(file);
    if (!read || count!=size) { return false; }
#else
    std::ifstream input(path.c_str(),std::ios::binary);
    if (!input) {
        return false;
    }
    std::string json(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
#endif
    ProgressionWeights loaded{};
    for (std::size_t index=0;index<WEIGHT_KEYS.size();++index) {
        if (!read_number(json,WEIGHT_KEYS[index],loaded.values[index])) {
            return false;
        }
    }
    set_progression_weights(loaded);
    return true;
}

bool save_progression_config(const std::string& path) {
#ifdef _WIN32
    std::ostringstream output;
#else
    std::ofstream output(path.c_str(),std::ios::binary|std::ios::trunc);
    if (!output) {
        return false;
    }
#endif
    output<<"{\n  \"chord_progression_weights\": {\n";
    output<<std::fixed<<std::setprecision(6);
    for (std::size_t index=0;index<WEIGHT_KEYS.size();++index) {
        output<<"    \""<<WEIGHT_KEYS[index]<<"\": "
              <<active_weights.values[index]
              <<(index+1==WEIGHT_KEYS.size() ? "\n" : ",\n");
    }
    output<<"  }\n}\n";
#ifdef _WIN32
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
    if (!length) { return false; }
    std::wstring wide(length,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&wide[0],length);
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) { return false; }
    auto bytes=output.str(); DWORD count=0; BOOL written=WriteFile(file,bytes.data(),bytes.size(),&count,nullptr); CloseHandle(file);
    return written && count==bytes.size();
#else
    return output.good();
#endif
}

}
