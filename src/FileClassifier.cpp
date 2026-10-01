#include "FileClassifier.h"

#include <algorithm>
#include <cctype>
#include <set>

std::string FileClassifier::normalizedExtension(const fs::path& file)
{
    auto extension = file.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

std::string FileClassifier::categoryFor(const fs::path& file)
{
    static const std::set<std::string> images = { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp" };
    static const std::set<std::string> code = { ".c", ".cpp", ".cc", ".cxx", ".h", ".hpp",
        ".py", ".js", ".ts", ".java" };
    static const std::set<std::string> text = { ".txt", ".md", ".markdown" };
    const auto extension = normalizedExtension(file);
    if (images.count(extension))
    {
        return "Images";
    }
    if (code.count(extension))
    {
        return "Code";
    }
    if (text.count(extension))
    {
        return "Text";
    }
    return "Other";
}

std::map<std::string, std::vector<fs::path>>
FileClassifier::classify(const std::vector<fs::path>& files) const
{
    std::map<std::string, std::vector<fs::path>> result;
    for (const auto& file : files)
    {
        const auto category = categoryFor(file);
        if (category != "Other")
        {
            result[category].push_back(file);
        }
    }
    return result;
}
