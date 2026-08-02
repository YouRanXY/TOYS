#pragma once
#include<filesystem>
#include<vector>
namespace fs=std::filesystem;

class FileScanner
{
    private:
        fs::path directory;
    public:
        FileScanner(const fs::path& path);
        std::vector<fs::path> scan();
};