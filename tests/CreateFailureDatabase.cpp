#include "FileDatabase.h"

#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

// Test-only preparation for a deterministic post-move snapshot failure.
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        return 1;
    }
    try
    {
        const auto path = fs::u8path(argv[1]);
        FileDatabase initialize(path);
        sqlite3* database = nullptr;
        if (sqlite3_open(path.u8string().c_str(), &database) != SQLITE_OK)
        {
            sqlite3_close(database);
            throw std::runtime_error("Cannot prepare failure fixture");
        }
        const auto status = sqlite3_exec(database,
            "CREATE TRIGGER fail_moved_insert BEFORE INSERT ON file_records "
            "WHEN instr(NEW.path, '/Text/') > 0 "
            "BEGIN SELECT RAISE(ABORT, 'injected index failure'); END;",
            nullptr, nullptr, nullptr);
        sqlite3_close(database);
        if (status != SQLITE_OK)
        {
            throw std::runtime_error("Cannot install fixture trigger");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
