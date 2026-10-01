#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class MoveStatus { Planned, Moved, Skipped, Failed };

struct MoveResult
{
    fs::path source;
    fs::path destination;
    MoveStatus status;
    std::string message;
};

class FileMove
{
public:
    std::vector<MoveResult> move(
        const std::map<std::string, std::vector<fs::path>>& files,
        const fs::path& target, bool dryRun = false) const;
};
