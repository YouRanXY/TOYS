#include "FileScanner.h"
#include "FileClassifier.h"
#include "FileMove.h"
#include "FileDatabase.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    struct Options
    {
        std::string command;
        fs::path root;
        fs::path database = "filemanager.db";
        QueryFilter filter;
        bool apply = false;
    };

    void printHelp()
    {
        std::cout
            << "FileManager - local file organizer and SQLite index\n\n"
            << "Usage:\n"
            << "  FileManager scan <directory> [--db <path>]\n"
            << "  FileManager organize <directory> [--apply] [--db <path>]\n"
            << "  FileManager query [--db <path>] [--category <Images|Code|Text|Other>]\n"
            << "                    [--extension <ext>] [--name <substring>]\n"
            << "  FileManager --help\n\n"
            << "Organize previews by default; --apply moves files and updates the index.\n"
            << "Unknown files stay in place. Existing destinations are never replaced.\n"
            << "Scanning is one level deep; query shows the last indexed batch for each root.\n";
    }

    Options parseOptions(const std::vector<std::string>& arguments)
    {
        Options options;
        options.command = arguments.front();
        if (options.command != "scan" && options.command != "organize" && options.command != "query")
        {
            throw std::runtime_error("Unknown command: " + options.command + ". Use --help.");
        }
        std::set<std::string> seen;
        bool hasRoot = false;
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const auto& argument = arguments[index];
            if (argument.rfind("--", 0) == 0)
            {
                if (!seen.insert(argument).second)
                {
                    throw std::runtime_error("Duplicate option: " + argument);
                }
                if (argument == "--apply" && options.command == "organize")
                {
                    options.apply = true;
                    continue;
                }
                const bool queryFilter = options.command == "query"
                    && (argument == "--category" || argument == "--extension" || argument == "--name");
                if (argument != "--db" && !queryFilter)
                {
                    throw std::runtime_error("Unsupported option for this command: " + argument);
                }
                if (++index >= arguments.size() || arguments[index].empty()
                    || arguments[index].rfind("--", 0) == 0)
                {
                    throw std::runtime_error("Missing value for " + argument);
                }
                const auto& value = arguments[index];
                if (argument == "--db")
                {
                    options.database = fs::u8path(value);
                }
                else if (argument == "--category")
                {
                    if (value != "Images" && value != "Code" && value != "Text" && value != "Other")
                    {
                        throw std::runtime_error("Invalid category: " + value);
                    }
                    options.filter.category = value;
                }
                else if (argument == "--extension")
                {
                    const auto extension = value.front() == '.' ? value : "." + value;
                    options.filter.extension = FileClassifier::normalizedExtension(fs::u8path("file" + extension));
                }
                else
                {
                    options.filter.name = value;
                }
            }
            else
            {
                if (options.command == "query" || hasRoot || argument.empty())
                {
                    throw std::runtime_error("Unexpected argument: " + argument);
                }
                options.root = fs::u8path(argument);
                hasRoot = true;
            }
        }
        if (options.command != "query" && !hasRoot)
        {
            throw std::runtime_error("Directory argument required. Use --help.");
        }
        return options;
    }

    bool samePath(const fs::path& first, const fs::path& second)
    {
        std::error_code error;
        if (fs::equivalent(first, second, error) && !error)
        {
            return true;
        }
        const auto left = fs::weakly_canonical(fs::absolute(first));
        const auto right = fs::weakly_canonical(fs::absolute(second));
#ifdef _WIN32
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
#else
        return left == right;
#endif
    }

    std::vector<fs::path> scanFiles(const fs::path& root, const fs::path& database)
    {
        auto files = FileScanner(root).scan();
        const auto dbName = database.u8string();
        const std::vector<fs::path> excluded = { database, fs::u8path(dbName + "-journal"),
            fs::u8path(dbName + "-wal"), fs::u8path(dbName + "-shm") };
        files.erase(std::remove_if(files.begin(), files.end(),
            [&](const fs::path& file)
            {
                return std::any_of(excluded.begin(), excluded.end(),
                    [&](const fs::path& path) { return samePath(file, path); });
            }), files.end());
        return files;
    }

    std::vector<FileRecord> readRecords(const std::vector<fs::path>& files, const fs::path& root)
    {
        std::vector<FileRecord> records;
        for (const auto& file : files)
        {
            records.push_back(FileDatabase::readRecord(file, root));
        }
        return records;
    }

    int execute(const Options& options)
    {
        if (options.command == "query")
        {
            FileDatabase database(options.database, true);
            const auto records = database.query(options.filter);
            std::cout << "Category\tBytes\tExtension\tModifiedTicks\tIndexedAt(UTC)\tPath\n";
            for (const auto& record : records)
            {
                std::cout << record.category << '\t' << record.sizeBytes << '\t'
                    << std::quoted(record.extension) << '\t' << record.modifiedTicks << '\t'
                    << record.indexedAt << '\t' << std::quoted(record.path.generic_u8string()) << '\n';
            }
            std::cout << "Records: " << records.size() << '\n';
            return 0;
        }

        if (!fs::is_directory(options.root))
        {
            throw std::runtime_error("Not a directory: " + options.root.u8string());
        }
        const auto root = fs::canonical(options.root);
        const auto files = scanFiles(root, options.database);
        if (options.command == "scan")
        {
            const auto records = readRecords(files, root);
            FileDatabase database(options.database);
            database.replaceSnapshot(root, records);
            std::cout << "Indexed: " << records.size() << " files into "
                << std::quoted(fs::absolute(options.database).generic_u8string()) << '\n';
            return 0;
        }

        const auto groups = FileClassifier().classify(files);
        std::vector<MoveResult> moves;
        if (options.apply)
        {
            // Validate and save the initial snapshot before changing any filenames.
            const auto initial = readRecords(files, root);
            FileDatabase database(options.database);
            database.replaceSnapshot(root, initial);
            moves = FileMove().move(groups, root);
            auto actualFiles = files;
            for (const auto& move : moves)
            {
                if (move.status == MoveStatus::Moved)
                {
                    std::replace(actualFiles.begin(), actualFiles.end(), move.source, move.destination);
                }
            }
            try
            {
                database.replaceSnapshot(root, readRecords(actualFiles, root));
            }
            catch (const std::exception& error)
            {
                // Moves cannot be rolled back by a database transaction.
                throw std::runtime_error(std::string("Organization finished, but index refresh failed: ")
                    + error.what() + ". Files may have moved; the index may still contain original paths. "
                    "Scan category directories separately to index their contents.");
            }
        }
        else
        {
            moves = FileMove().move(groups, root, true);
        }

        std::size_t planned = 0, moved = 0, skipped = 0, failed = 0;
        for (const auto& move : moves)
        {
            const char* status = "";
            switch (move.status)
            {
                case MoveStatus::Planned: status = "PLAN"; ++planned; break;
                case MoveStatus::Moved: status = "MOVED"; ++moved; break;
                case MoveStatus::Skipped: status = "SKIP"; ++skipped; break;
                case MoveStatus::Failed: status = "ERROR"; ++failed; break;
            }
            std::cout << status << ' ' << std::quoted(move.source.generic_u8string())
                << " -> " << std::quoted(move.destination.generic_u8string());
            if (!move.message.empty())
            {
                std::cout << " (" << move.message << ')';
            }
            std::cout << '\n';
        }
        std::cout << "Scanned: " << files.size() << ", planned: " << planned
            << ", moved: " << moved << ", skipped: " << skipped << ", failed: " << failed
            << ", unclassified: " << files.size() - moves.size() << '\n';
        if (!options.apply)
        {
            std::cout << "Preview only. Add --apply to move files and update the index.\n";
        }
        return failed != 0 || (options.apply && skipped != 0) ? 1 : 0;
    }

    int run(const std::vector<std::string>& arguments)
    {
        try
        {
            if (arguments.empty()
                || (arguments.size() == 1 && (arguments.front() == "--help" || arguments.front() == "-h")))
            {
                printHelp();
                return 0;
            }
            return execute(parseOptions(arguments));
        }
        catch (const std::exception& error)
        {
            std::cerr << "Error: " << error.what() << '\n';
            return 1;
        }
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index)
    {
        arguments.push_back(fs::path(argv[index]).u8string());
    }
    return run(arguments);
}
#else
int main(int argc, char* argv[])
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
