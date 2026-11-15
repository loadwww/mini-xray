#include "parse/sse.h"

#include <nlohmann/json.hpp>

namespace mxr {

// 把字节切成整行交给 processLine；没见到换行的半行留在 lineBuf_。
void SseAccumulator::feed(const std::string &bytes)
{
    size_t p;
    lineBuf_.append(bytes);
    while((p=lineBuf_.find('\n'))!=std::string::npos){
        std::string s=lineBuf_.substr(0,p);
        if(!s.empty()&&s.back()=='\r')s.pop_back();
        processLine(s);
        lineBuf_.erase(0,p+1);
    }
}

// 一行分类：空行分发事件、data: 累积、注释与其它行忽略。
void SseAccumulator::processLine(const std::string &line)
{
    if(line.empty()){
        if(!dataAcc_.empty())dispatch(dataAcc_);
        dataAcc_.clear();
    }else if(line.rfind(":", 0) == std::string::npos){
        if(line.rfind("data:", 0)==0){
            if(!dataAcc_.empty())dataAcc_.push_back('\n');
            std::string s=line.substr(5,line.size()-5);
            if(!s.empty()&&s[0]==' '){
                s.erase(0,1);
            }
            dataAcc_.append(s);
            
        }
    }
}

// 解析一个完整事件，累积文本 / finish_reason / usage；坏块计数跳过。
void SseAccumulator::dispatch(const std::string &data)
{
    if(data=="[DONE]"){
        done_=true;
        return;
    }
    try{
        nlohmann::json j=nlohmann::json::parse(data);
        if(j["choices"].is_array() && !j["choices"].empty()){
            if(j.contains("choices")&&!j["choices"].empty()){
                nlohmann::json choice=j["choices"][0];
                if(choice.contains("delta")){
                    if(choice["delta"]["content"].is_string()){
                        text_.append(j["choices"][0]["delta"]["content"].get<std::string>());
                    }
                }
                if(choice.contains("finish_reason")&&choice["finish_reason"].is_string()){
                    finishReason_=(j["choices"][0]["finish_reason"].get<std::string>());
                }
                if(j.contains("usage")){
                    usage_={j["usage"]["prompt_tokens"].get<long long>(),
                            j["usage"]["completion_tokens"].get<long long>(),
                            j["usage"]["total_tokens"].get<long long>()};
                    hasUsage_=true;
                }
                ++eventCount_;
            }
        }
    }catch (const nlohmann::json::exception &) { ++badDataCount_; return; }

}

// 流结束：补处理残留的半行与没等到空行的事件。
void SseAccumulator::finish()
{
    if(!lineBuf_.empty()&&lineBuf_.back()=='\r'){
        lineBuf_.pop_back();
    }
    if(!lineBuf_.empty())processLine(lineBuf_);
    lineBuf_.clear();
    if(!dataAcc_.empty()){
        dispatch(dataAcc_);
        dataAcc_.clear();
    }

}

bool SseAccumulator::done() const { return done_; }
const std::string &SseAccumulator::text() const { return text_; }
bool SseAccumulator::hasUsage() const { return hasUsage_; }
const SseUsage &SseAccumulator::usage() const { return usage_; }
const std::string &SseAccumulator::finishReason() const { return finishReason_; }
int SseAccumulator::badDataCount() const { return badDataCount_; }
int SseAccumulator::eventCount() const { return eventCount_; }

} // namespace mxr
