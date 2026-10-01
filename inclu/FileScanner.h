#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

class FileScanner
{
public:
    explicit FileScanner(const fs::path& path);
    std::vector<fs::path> scan() const;

private:
    fs::path directory;
};
