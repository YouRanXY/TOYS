#pragma once

#include <filesystem>
#include <map>
#include <vector>
#include <string>


namespace fs = std::filesystem;


class FileMove
{

public:

    void move(
        const std::map<std::string, std::vector<fs::path>>& files,
        const fs::path& target
    );

};