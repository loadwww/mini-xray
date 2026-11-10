#include"filewriter.h"
#include<utility>

namespace mxr{
    FileWriter::FileWriter(const std::string& path){
        file_.open(path,std::ios::app);
    }
    FileWriter::~FileWriter(){
        // ofstream自动关文件
    }
    FileWriter:: FileWriter(FileWriter&& other) noexcept
        : file_(std::move(other.file_))
    {
        // 移动
    }          
    FileWriter& FileWriter::operator=(FileWriter&& other) noexcept{
        if(this!=&other){
            if(file_.is_open()){
                file_.close();
            }
            file_=std::move(other.file_);
        }
        return *this;
    }
    
    bool FileWriter::write(std::string_view text){
        if(!file_.is_open()){
            return false;
        }
        
        file_.write(text.data(),static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(file_);
    }
    void FileWriter::flush(){
        if (file_.is_open()) {
            file_.flush();
        }
    }
    bool FileWriter::isOpen(){
        return file_.is_open();
    }
};//namespace mxr
