#include<iostream>
#include<string>
using namespace std;

int main(int argc, char *argv[]){
    if (argc == 1) {
        cerr << "usage: minixray [--version]\n--version    打印版本号"<< endl;
        return 1;
    } else if (string(argv[1]) == "--version") {
        cout << "minixray 0.1.0" << endl;
        return 0;
    } else {
        cout << "unknown command: " << argv[1] <<"\nusage: minixray [--version]\n--version    打印版本号"<< endl;
        return 2;      // 建议加，未知命令也算失败
    }
    
}