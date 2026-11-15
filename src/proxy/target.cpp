#include "proxy/target.h"

namespace mxr {

static bool splitUrl(const std::string &url, std::string &origin, std::string &base)
{
    size_t sp1=url.find("://");
    size_t n=url.size();
    if(sp1==std::string::npos){
        return false;
    }
    int sp2=sp1+3;
    if(sp2>=n||url[sp2]=='/')return false;
    while(sp2<n&&url[sp2]!='/'){
        sp2++;
    }
    if(sp2>=n){
        origin=url;
    }else{
        origin=url.substr(0,sp2);
        base=url.substr(sp2,n-sp2);
        if(base.size()==1){
            base="";
        }else if(base[base.size()-1]=='/'){
            base.pop_back();
        }
       
    }

    return true;
}

std::optional<Target> buildTarget(const Upstream &u, const std::string &strippedPath)
{
    std::string origin,base,path;
    if(!splitUrl(u.url,origin,base)){
        return std::nullopt;
    }
    std::optional<Target>res;
    path=base+strippedPath;
    Target t;
    t.origin=origin;
    t.path=path;
    res.emplace(t);
    return res;
}

} // namespace mxr
