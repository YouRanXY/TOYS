#include "FileScanner.h"
#include "FileSafety.h"

#include <algorithm>
#include <stdexcept>

FileScanner::FileScanner(const fs::path& path) : directory(path)
{
}

std::vector<fs::path> FileScanner::scan() const
{
    if (!fs::is_directory(directory))
    {
        throw std::runtime_error("Not a directory: " + directory.u8string());
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (!isLink(entry.path()) && fs::is_regular_file(entry.symlink_status()))
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
