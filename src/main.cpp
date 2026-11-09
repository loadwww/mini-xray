#include<iostream>
#include<string>
#include"common/version.h"
using namespace std;

int main(int argc, char *argv[]){
    if (argc == 1) {
        cerr << "usage: mxr [--version]\n--version    打印版本号"<< endl;
        return 2;
    } else if (string(argv[1]) == "--version") {
        cout << "mxr "<<versionString()<< endl;
        return 0;
    } else {
        cerr << "unknown command: " << argv[1] <<"\nusage: mxr [--version]\n--version    打印版本号"<< endl;
        return 2;      
    }
    
}