#include "FileDatabase.h"
#include "FileClassifier.h"

#include <sqlite3.h>

#include <limits>
#include <stdexcept>

namespace
{
    class Statement
    {
    public:
        sqlite3_stmt* handle = nullptr;

        Statement(sqlite3* database, const char* sql)
        {
            if (sqlite3_prepare_v2(database, sql, -1, &handle, nullptr) != SQLITE_OK)
            {
                const std::string error = sqlite3_errmsg(database);
                sqlite3_finalize(handle);
                throw std::runtime_error("SQLite prepare: " + error);
            }
        }

        ~Statement()
        {
            sqlite3_finalize(handle);
        }

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        void text(int index, const std::string& value)
        {
            if (sqlite3_bind_text(handle, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            {
                throw std::runtime_error("SQLite text binding failed");
            }
        }

        void number(int index, std::int64_t value)
        {
            if (sqlite3_bind_int64(handle, index, value) != SQLITE_OK)
            {
                throw std::runtime_error("SQLite integer binding failed");
            }
        }

        void done()
        {
            if (sqlite3_step(handle) != SQLITE_DONE)
            {
                throw std::runtime_error(std::string("SQLite write: ")
                    + sqlite3_errmsg(sqlite3_db_handle(handle)));
            }
            sqlite3_reset(handle);
            sqlite3_clear_bindings(handle);
        }
    };

    std::string columnText(sqlite3_stmt* statement, int column)
    {
        const auto value = sqlite3_column_text(statement, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    std::string pathKey(const fs::path& path)
    {
        return fs::weakly_canonical(fs::absolute(path)).generic_u8string();
    }
}

FileDatabase::FileDatabase(const fs::path& path, bool readOnly)
{
    const auto name = fs::absolute(path).u8string();
    const auto flags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(name.c_str(), &database, flags, nullptr) != SQLITE_OK)
    {
        const std::string error = database ? sqlite3_errmsg(database) : "allocation failed";
        sqlite3_close(database);
        database = nullptr;
        throw std::runtime_error("Cannot open database: " + error);
    }
    try
    {
        sqlite3_busy_timeout(database, 3000);
        if (readOnly)
        {
            Statement validate(database, "SELECT root, path, name, extension, category, size_bytes, "
                "modified_ticks, indexed_at FROM file_records LIMIT 0");
        }
        else
        {
            if (sqlite3_db_readonly(database, "main") != 0)
            {
                throw std::runtime_error("Database is read-only");
            }
            execute("CREATE TABLE IF NOT EXISTS file_records ("
                "root TEXT NOT NULL, path TEXT NOT NULL, name TEXT NOT NULL, "
                "extension TEXT NOT NULL, category TEXT NOT NULL, "
                "size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0), "
                "modified_ticks INTEGER NOT NULL, "
                "indexed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')), "
                "PRIMARY KEY(root, path))");
            execute("BEGIN IMMEDIATE");
            execute("ROLLBACK");
        }
    }
    catch (...)
    {
        sqlite3_close(database);
        database = nullptr;
        throw;
    }
}

FileDatabase::~FileDatabase()
{
    sqlite3_close(database);
}

void FileDatabase::execute(const char* sql) const
{
    char* message = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &message) != SQLITE_OK)
    {
        const std::string error = message ? message : sqlite3_errmsg(database);
        sqlite3_free(message);
        throw std::runtime_error("SQLite: " + error);
    }
}

FileRecord FileDatabase::readRecord(const fs::path& path, const fs::path& root)
{
    FileRecord record;
    record.path = fs::weakly_canonical(fs::absolute(path));
    record.root = fs::canonical(root);
    record.name = path.filename().u8string();
    record.extension = FileClassifier::normalizedExtension(path);
    record.category = FileClassifier::categoryFor(path);
    const auto size = fs::file_size(path);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()))
    {
        throw std::runtime_error("File size exceeds SQLite integer range");
    }
    record.sizeBytes = static_cast<std::int64_t>(size);
    record.modifiedTicks = static_cast<std::int64_t>(fs::last_write_time(path).time_since_epoch().count());
    return record;
}

void FileDatabase::replaceSnapshot(const fs::path& root, const std::vector<FileRecord>& records)
{
    const auto rootKey = pathKey(root);
    execute("BEGIN IMMEDIATE");
    try
    {
        Statement remove(database, "DELETE FROM file_records WHERE root = ?1");
        remove.text(1, rootKey);
        remove.done();
        Statement insert(database, "INSERT INTO file_records "
            "(root, path, name, extension, category, size_bytes, modified_ticks) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
        for (const auto& record : records)
        {
            if (pathKey(record.root) != rootKey)
            {
                throw std::runtime_error("Record belongs to a different scan root");
            }
            insert.text(1, rootKey);
            insert.text(2, pathKey(record.path));
            insert.text(3, record.name);
            insert.text(4, record.extension);
            insert.text(5, record.category);
            insert.number(6, record.sizeBytes);
            insert.number(7, record.modifiedTicks);
            insert.done();
        }
        execute("COMMIT");
    }
    catch (...)
    {
        // Preserve the original exception even if rollback itself fails.
        sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::vector<FileRecord> FileDatabase::query(const QueryFilter& filter) const
{
    Statement statement(database, "SELECT path, root, name, extension, category, size_bytes, "
        "modified_ticks, indexed_at FROM file_records "
        "WHERE (?1 = '' OR category = ?1) AND (?2 = '' OR extension = ?2) "
        "AND (?3 = '' OR instr(name, ?3) > 0) ORDER BY root, path");
    statement.text(1, filter.category);
    statement.text(2, filter.extension);
    statement.text(3, filter.name);
    std::vector<FileRecord> records;
    int status;
    while ((status = sqlite3_step(statement.handle)) == SQLITE_ROW)
    {
        FileRecord record;
        record.path = fs::u8path(columnText(statement.handle, 0));
        record.root = fs::u8path(columnText(statement.handle, 1));
        record.name = columnText(statement.handle, 2);
        record.extension = columnText(statement.handle, 3);
        record.category = columnText(statement.handle, 4);
        record.sizeBytes = sqlite3_column_int64(statement.handle, 5);
        record.modifiedTicks = sqlite3_column_int64(statement.handle, 6);
        record.indexedAt = columnText(statement.handle, 7);
        records.push_back(record);
    }
    if (status != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("SQLite query: ") + sqlite3_errmsg(database));
    }
    return records;
}
