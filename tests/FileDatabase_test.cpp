#include "FileDatabase.h"
#include "TestSupport.h"

int main()
{
    return runTest([]
    {
        TestDirectory fixture("database");
        const auto root = fs::canonical(fixture.path);
        const auto unicode = fixture.write(fs::u8path(u8"笔记'_% .TXT"), "hello");
        const auto other = fixture.write("other.zip", "archive");
        const auto first = FileDatabase::readRecord(unicode, root);
        check(first.sizeBytes == 5 && first.category == "Text" && first.extension == ".txt",
            "Read actual metadata and normalize extension");
        const auto dbPath = fixture.path / fs::u8path(u8"索引.db");
        {
            FileDatabase database(dbPath);
            database.replaceSnapshot(root, { first, FileDatabase::readRecord(other, root) });
            check(database.query().size() == 2, "Persist both recognized and unknown files");
            check(database.query({ "Text", ".txt", "'_%" }).size() == 1,
                "Query Unicode names with literal SQL metacharacters");
            check(database.query({ "", "", "' OR 1=1 --" }).empty(),
                "Query input must be bound, never interpreted as SQL");
            check(!database.query().front().indexedAt.empty(), "Persist index time");
            bool rejected = false;
            try { database.replaceSnapshot(root, { first, first }); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && database.query().size() == 2,
                "A failed refresh must roll back the whole snapshot");
            const auto secondRoot = root / "second";
            fixture.write("second/main.cpp", "code");
            database.replaceSnapshot(secondRoot,
                { FileDatabase::readRecord(secondRoot / "main.cpp", secondRoot) });
            database.replaceSnapshot(root, { first });
            check(database.query().size() == 2, "Refresh must retain other roots");
        }
        {
            FileDatabase reopened(dbPath, true);
            check(reopened.query().size() == 2, "Metadata survives closing and reopening");
            bool rejected = false;
            try { reopened.replaceSnapshot(root, {}); }
            catch (const std::exception&) { rejected = true; }
            check(rejected, "Read-only query connection must not write");
        }
        const auto absentDb = fixture.path / "absent.db";
        bool rejected = false;
        try { FileDatabase missing(absentDb, true); }
        catch (const std::exception&) { rejected = true; }
        check(rejected && !fs::exists(absentDb), "Query must not create a missing database");
        fixture.write("invalid.db", "not SQLite");
        rejected = false;
        try { FileDatabase invalid(fixture.path / "invalid.db"); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "Invalid database must report an error");
    });
}
