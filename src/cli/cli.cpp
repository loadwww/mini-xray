#include"cli.h"
#include<string>
#include"common/version.h"
#include<iostream>
int mxr::run(int argc,char*argv[]){
    if (argc == 1) {
        std::cerr << "usage: mxr [--version]\n--version    打印版本号"<< std::endl;
        return 2;
    } else if (std::string(argv[1]) == "--version") {
        std::cout << "mxr "<<versionString()<< std::endl;
        return 0;
    } else {
        std::cerr << "unknown command: " << argv[1] <<"\nusage: mxr [--version]\n--version    打印版本号"<< std::endl;
        return 2;      
    }
}