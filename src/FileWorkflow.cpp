#include "FileWorkflow.h"
#include "FileClassifier.h"
#include "FileScanner.h"
#include "FileSafety.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    bool samePath(const fs::path& left, const fs::path& right)
    {
        std::error_code error;
        if (fs::equivalent(left, right, error) && !error)
        {
            return true;
        }
        const auto first = fs::weakly_canonical(fs::absolute(left));
        const auto second = fs::weakly_canonical(fs::absolute(right));
#ifdef _WIN32
        return _wcsicmp(first.c_str(), second.c_str()) == 0;
#else
        return first == second;
#endif
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
}

std::vector<fs::path> FileWorkflow::scanFiles(const fs::path& root, const fs::path& database)
{
    auto files = FileScanner(root).scan();
    const auto name = database.u8string();
    const std::vector<fs::path> excluded = { database, fs::u8path(name + "-journal"),
        fs::u8path(name + "-wal"), fs::u8path(name + "-shm") };
    files.erase(std::remove_if(files.begin(), files.end(), [&](const fs::path& file)
    {
        return std::any_of(excluded.begin(), excluded.end(),
            [&](const fs::path& path) { return samePath(file, path); });
    }), files.end());
    return files;
}

OrganizationPlan FileWorkflow::preview(const fs::path& root, const fs::path& database)
{
    OrganizationPlan plan;
    plan.root = fs::canonical(root);
    plan.database = fs::absolute(database);
    const auto files = scanFiles(plan.root, plan.database);
    plan.records = readRecords(files, plan.root);
    plan.moves = FileMove().move(FileClassifier().classify(files), plan.root, true);
    return plan;
}

OrganizationResult FileWorkflow::apply(const OrganizationPlan& plan)
{
    std::vector<fs::path> files;
    for (const auto& record : plan.records)
    {
        if (isLink(record.path) || !fs::is_regular_file(fs::symlink_status(record.path)))
        {
            throw std::runtime_error("Previewed files changed. Please preview again before organizing.");
        }
        const auto current = FileDatabase::readRecord(record.path, plan.root);
        if (current.sizeBytes != record.sizeBytes || current.modifiedTicks != record.modifiedTicks
            || current.path != record.path)
        {
            throw std::runtime_error("Previewed files changed. Please preview again before organizing.");
        }
    }
    for (const auto& move : plan.moves)
    {
        if (move.status == MoveStatus::Planned)
        {
            files.push_back(move.source);
        }
    }

    FileDatabase database(plan.database);
    database.replaceSnapshot(plan.root, plan.records);
    OrganizationResult result;
    result.records = plan.records;
    result.moves = FileMove().move(FileClassifier().classify(files), plan.root);
    for (const auto& move : plan.moves)
    {
        if (move.status != MoveStatus::Planned)
        {
            result.moves.push_back(move);
        }
    }
    for (const auto& move : result.moves)
    {
        if (move.status == MoveStatus::Moved)
        {
            for (auto& record : result.records)
            {
                if (record.path == move.source)
                {
                    record.path = move.destination;
                }
            }
        }
    }
    try
    {
        std::vector<fs::path> actual;
        for (const auto& record : result.records)
        {
            actual.push_back(record.path);
        }
        result.records = readRecords(actual, plan.root);
        database.replaceSnapshot(plan.root, result.records);
        result.indexSaved = true;
    }
    catch (const std::exception& error)
    {
        result.error = std::string("Files may have moved, but the index was not refreshed: ")
            + error.what() + ". Original paths may remain indexed. Scan category directories separately.";
    }
    return result;
}

std::vector<FileRecord> FileWorkflow::index(const fs::path& root, const fs::path& database)
{
    const auto canonicalRoot = fs::canonical(root);
    const auto records = readRecords(scanFiles(canonicalRoot, database), canonicalRoot);
    FileDatabase storage(database);
    storage.replaceSnapshot(canonicalRoot, records);
    return records;
}

std::vector<FileRecord> FileWorkflow::query(const fs::path& database, QueryFilter filter)
{
    if (!filter.extension.empty())
    {
        const auto extension = filter.extension.front() == '.' ? filter.extension : "." + filter.extension;
        filter.extension = FileClassifier::normalizedExtension(fs::u8path("file" + extension));
    }
    return FileDatabase(database, true).query(filter);
}
