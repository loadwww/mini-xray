#include"cli.h"
#include<string>
#include"common/version.h"
#include"config/config.h"
#include<iostream>
int mxr::run(int argc,char*argv[]){
    if (argc == 1) {
        std::cerr << "usage: mxr [--version]\n--version    打印版本号"<< std::endl;
        return 2;
    } else if (std::string(argv[1]) == "--version") {
        std::cout << "mxr "<<versionString()<< std::endl;
    } else if (std::string(argv[1]) == "--config") {
        if (argc != 3) {
            std::cerr << "usage: mxr --config <path>\n";
            return 2;
        }
        auto cfg = mxr::loadConfig(argv[2]);
        if (!cfg) {
            return 1;                              // 错误细节 loadConfig 已经打过
        }
        std::string cfg_str = mxr::toJsonString(*cfg);
        std::cout << cfg_str << "\n";
    } else {
        std::cerr << "unknown command: " << argv[1] <<"\nusage: mxr [--version]\n--version    打印版本号"<< std::endl;
        return 2;      
    }
    return 0;
    
}   