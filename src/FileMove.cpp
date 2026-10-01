#include "FileMove.h"
#include "FileSafety.h"

#include <stdexcept>
#include <system_error>

namespace
{
    void renameWithoutReplacement(const fs::path& source, const fs::path& destination)
    {
#ifdef _WIN32
        // No REPLACE_EXISTING flag: fail if the destination appears concurrently.
        if (!MoveFileExW(source.c_str(), destination.c_str(), 0))
        {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
        }
#else
        // Exclusive creation prevents overwrites, including after the initial check.
        // Both paths are on the same volume. Failure to unlink retains both names.
        fs::create_hard_link(source, destination);
        fs::remove(source);
#endif
    }
}

std::vector<MoveResult> FileMove::move(
    const std::map<std::string, std::vector<fs::path>>& files,
    const fs::path& target, bool dryRun) const
{
    std::vector<MoveResult> results;
    for (const auto& category : files)
    {
        const auto folder = target / category.first;
        for (const auto& file : category.second)
        {
            MoveResult result { file, folder / file.filename(), MoveStatus::Failed, "" };
            try
            {
                if (category.first != "Images" && category.first != "Code" && category.first != "Text")
                {
                    throw std::runtime_error("Invalid category");
                }
                if (!fs::is_directory(target) || isLink(file)
                    || !fs::is_regular_file(fs::symlink_status(file))
                    || fs::canonical(file.parent_path()) != fs::canonical(target))
                {
                    throw std::runtime_error("Source must be a regular file directly inside the root");
                }
                const auto folderStatus = fs::symlink_status(folder);
                if (isLink(folder) || (fs::exists(folderStatus) && !fs::is_directory(folderStatus)))
                {
                    throw std::runtime_error("Category path must be a real directory");
                }
                if (fs::exists(fs::symlink_status(result.destination)))
                {
                    result.status = MoveStatus::Skipped;
                    result.message = "Destination already exists; source retained";
                }
                else if (dryRun)
                {
                    result.status = MoveStatus::Planned;
                }
                else
                {
                    fs::create_directory(folder);
                    if (isLink(folder) || !fs::is_directory(folder))
                    {
                        throw std::runtime_error("Category path changed during operation");
                    }
                    renameWithoutReplacement(file, result.destination);
                    result.status = MoveStatus::Moved;
                }
            }
            catch (const std::exception& error)
            {
                result.message = error.what();
            }
            results.push_back(result);
        }
    }
    return results;
}
