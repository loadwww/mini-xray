#include "parse/chunked.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>

namespace mxr {

std::optional<std::size_t> ChunkedDecoder::parseSizeLine(std::string_view line) const
{
    size_t res=0;
    size_t n=line.size();
    if(line.empty())return std::nullopt;
    for(size_t i=0;i<n;i++){
        char c=line[i];
        size_t x=0;
        if(c==';'&&i!=0){
            return res;
        }
        if(c>='A'&&c<='F'){
            x=c-'A'+10;
        }else if(c>='a'&&c<='f'){
            x=c-'a'+10;
        }else if(c>='0'&&c<='9'){
            x=c-'0';
        }else{
            return std::nullopt;
        }
        if(res>(SIZE_MAX-x)/16){
            return std::nullopt;
        }
        res=res*16+x;

    }
    return res;
}

std::string ChunkedDecoder::feed(std::string_view in)
{
    std::string out="";
    if(!in.empty())
        buf_.append(in);
    while(true){
        switch (st_)
        {
        
        case St::Size:
        {
            size_t p=buf_.find("\r\n");
            if(p==std::string::npos){
                return out;
            }
            std::optional<std::size_t> n=parseSizeLine( buf_.substr(0,p));
            if(n==std::nullopt){
                st_=St::Failed;
                return out;
            }else{
                if(n==0)st_=St::Trailer;
                else{
                    remaining_=n.value();
                    st_=St::Data;
                }
            }
            buf_.erase(0,p+2);
            
        }
            break;
        case St::Data:
            {
            size_t take = std::min(remaining_, buf_.size());
            out.append(buf_,0,take);
            buf_.erase(0,take);
            remaining_-=take;
            if(remaining_==0){
                st_=St::DataCrLf;
            }
            if(buf_.empty())return out;
            }
            break;

        case St::DataCrLf:
            if(buf_.size()<2)return out;
            if(buf_[0]!='\r'||buf_[1]!='\n'){
                st_=St::Failed;
            }else{
                buf_.erase(0,2);
                st_=St::Size;
            }
            break;
        case St::Trailer:
        {
            size_t p=buf_.find("\r\n");
            if(p==std::string::npos){
                return out;
            }
            if(p==0){
                buf_.erase(0,2);
                st_=St::Done;
            }else{
                buf_.erase(0,p+2);
            }
        }
            break;
        default:
            return out;
            break;
        }
    }
    return {};
}

} // namespace mxr
