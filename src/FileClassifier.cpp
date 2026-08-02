#include "FileClassifier.h"


std::map<std::string, std::vector<fs::path>>
FileClassifier::classify(std::vector<fs::path> files)
{

    std::map<std::string, std::vector<fs::path>> result;


    for(const auto& file : files)
    {

        auto ext = file.extension();


        if(ext == ".jpg" || ext == ".png")
        {
            result["Images"].push_back(file);
        }


        else if(ext == ".cpp" || ext == ".h")
        {
            result["Code"].push_back(file);
        }


        else if(ext == ".txt" || ext == ".markdown")
        {
            result["Text"].push_back(file);
        }
        

    }


    return result;
}