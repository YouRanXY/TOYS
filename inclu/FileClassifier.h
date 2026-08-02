#pragma once

#include <filesystem>
#include <vector>
#include <map>
#include <string>


namespace fs = std::filesystem;


class FileClassifier
{

public:

    std::map<std::string, std::vector<fs::path>>
    classify(std::vector<fs::path> files);

};