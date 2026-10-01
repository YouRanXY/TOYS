#pragma once

#include "FileDatabase.h"
#include "FileMove.h"

struct OrganizationPlan
{
    fs::path root;
    fs::path database;
    std::vector<FileRecord> records;
    std::vector<MoveResult> moves;
};

struct OrganizationResult
{
    std::vector<FileRecord> records;
    std::vector<MoveResult> moves;
    bool indexSaved = false;
    std::string error;
};

class FileWorkflow
{
public:
    static OrganizationPlan preview(const fs::path& root, const fs::path& database);
    static OrganizationResult apply(const OrganizationPlan& plan);
    static std::vector<FileRecord> index(const fs::path& root, const fs::path& database);
    static std::vector<FileRecord> query(const fs::path& database, QueryFilter filter = {});

private:
    static std::vector<fs::path> scanFiles(const fs::path& root, const fs::path& database);
};
