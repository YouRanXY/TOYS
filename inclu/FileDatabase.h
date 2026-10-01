#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;
namespace fs = std::filesystem;

struct FileRecord
{
    fs::path path;
    fs::path root;
    std::string name;
    std::string extension;
    std::string category;
    std::int64_t sizeBytes = 0;
    std::int64_t modifiedTicks = 0;
    std::string indexedAt;
};

struct QueryFilter
{
    std::string category;
    std::string extension;
    std::string name;
};

class FileDatabase
{
public:
    explicit FileDatabase(const fs::path& path, bool readOnly = false);
    ~FileDatabase();
    FileDatabase(const FileDatabase&) = delete;
    FileDatabase& operator=(const FileDatabase&) = delete;

    static FileRecord readRecord(const fs::path& path, const fs::path& root);
    void replaceSnapshot(const fs::path& root, const std::vector<FileRecord>& records);
    std::vector<FileRecord> query(const QueryFilter& filter = {}) const;

private:
    sqlite3* database = nullptr;
    void execute(const char* sql) const;
};
