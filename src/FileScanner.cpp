#include"FileScanner.h"
FileScanner::FileScanner(const fs::path&path)
{
    directory=path;
}
std::vector<fs::path> FileScanner::scan()
{
    std::vector<fs::path> files;
    for(const auto&entry :fs::directory_iterator(directory))
    {
        if(fs::is_regular_file(entry))
        {
            files.push_back(entry.path());
        }        
    
        
    }
    return files;
}