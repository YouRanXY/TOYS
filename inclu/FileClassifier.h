#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class FileClassifier
{
public:
    static std::string normalizedExtension(const fs::path& file);
    static std::string categoryFor(const fs::path& file);
    std::map<std::string, std::vector<fs::path>>
    classify(const std::vector<fs::path>& files) const;
};
