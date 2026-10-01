#include "FileWorkflow.h"
#include "TestSupport.h"

#include <sqlite3.h>

int main()
{
    return runTest([]
    {
        TestDirectory fixture("workflow");
        fixture.write(fs::u8path(u8"笔记.TXT"), "text");
        fixture.write("main.cpp", "code");
        fixture.write("unknown.bin", "unknown");
        fixture.write("nested/ignored.txt");
        const auto database = fixture.path / "active.txt";
        const auto plan = FileWorkflow::preview(fixture.path, database);
        check(plan.records.size() == 3 && plan.moves.size() == 2, "Preview one level only");
        check(!fs::exists(database) && !fs::exists(fixture.path / "Text"),
            "Preview must neither index nor move files");
        fixture.write("new.jpg", "added after preview");
        const auto result = FileWorkflow::apply(plan);
        check(result.indexSaved && result.error.empty(), "Apply saves the actual batch");
        check(fs::exists(fixture.path / fs::u8path(u8"Text/笔记.TXT"))
            && fs::exists(fixture.path / "Code/main.cpp"), "Move previewed Unicode and code files");
        check(fs::exists(fixture.path / "new.jpg"), "Do not move unpreviewed additions");
        check(fs::exists(fixture.path / "unknown.bin"), "Unknown files remain");
        check(FileWorkflow::query(database, { "Text", "TXT", u8"笔记" }).size() == 1,
            "GUI filters normalize extensions and support Unicode");
        const auto next = FileWorkflow::preview(fixture.path, database);
        check(next.records.size() == 2, "Exclude active database with recognized extension");

        fixture.write("stale.cpp", "before");
        const auto stalePlan = FileWorkflow::preview(fixture.path, database);
        fixture.write("stale.cpp", "changed file size");
        bool rejected = false;
        try { FileWorkflow::apply(stalePlan); }
        catch (const std::exception&) { rejected = true; }
        check(rejected && fs::exists(fixture.path / "new.jpg")
            && fs::exists(fixture.path / "stale.cpp"), "Stale preview aborts the entire batch");

        const auto badPlan = FileWorkflow::preview(fixture.path, fixture.path / "absent/index.db");
        rejected = false;
        try { FileWorkflow::apply(badPlan); }
        catch (const std::exception&) { rejected = true; }
        check(rejected && fs::exists(fixture.path / "new.jpg"), "Database failure prevents moves");
        const auto indexed = FileWorkflow::index(fixture.path, database);
        check(indexed.size() == 3 && fs::exists(fixture.path / "stale.cpp"),
            "Index refresh excludes database without moving");

        fixture.write("collision.txt", "source");
        fixture.write("Text/collision.txt", "destination");
        const auto collision = FileWorkflow::apply(FileWorkflow::preview(fixture.path, database));
        check(collision.indexSaved && readFile(fixture.path / "collision.txt") == "source"
            && readFile(fixture.path / "Text/collision.txt") == "destination",
            "Report collisions and preserve both files");
        bool skipped = false;
        for (const auto& move : collision.moves)
        {
            skipped = skipped || move.status == MoveStatus::Skipped;
        }
        check(skipped, "GUI receives per-file skipped outcomes");

        fixture.write("actions/skip.txt", "source");
        fixture.write("actions/Text/skip.txt", "target");
        fixture.write("actions/move.cpp", "code");
        const auto actionsRoot = fixture.path / "actions";
        const auto actionsDb = fixture.path / "actions.db";
        const auto actions = FileWorkflow::preview(actionsRoot, actionsDb);
        fs::remove(actionsRoot / "Text/skip.txt");
        const auto appliedActions = FileWorkflow::apply(actions);
        check(appliedActions.indexSaved && fs::exists(actionsRoot / "skip.txt")
            && !fs::exists(actionsRoot / "Text/skip.txt")
            && fs::exists(actionsRoot / "Code/move.cpp"),
            "Apply preserves previewed skips even when a conflict disappears");

        fixture.write("links/a.txt", "text");
        fixture.write("links/b.cpp", "code");
        fixture.write("replacement.txt", "text");
        const auto linksRoot = fixture.path / "links";
        const auto linksDb = fixture.path / "links.db";
        fs::last_write_time(fixture.path / "replacement.txt", fs::last_write_time(linksRoot / "a.txt"));
        const auto linksPlan = FileWorkflow::preview(linksRoot, linksDb);
        fs::remove(linksRoot / "a.txt");
        std::error_code linkError;
        fs::create_symlink(fixture.path / "replacement.txt", linksRoot / "a.txt", linkError);
        if (!linkError)
        {
            rejected = false;
            try { FileWorkflow::apply(linksPlan); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && fs::exists(linksRoot / "b.cpp") && !fs::exists(linksDb),
                "A link replacement aborts all moves before database writes");
        }
        else
        {
            std::cout << "SKIP: link replacement test: " << linkError.message() << '\n';
        }

        fixture.write("partial/fail.txt", "preserve");
        const auto partialRoot = fixture.path / "partial";
        const auto partialDb = fixture.path / "partial.db";
        { FileDatabase initialize(partialDb); }
        sqlite3* handle = nullptr;
        check(sqlite3_open(partialDb.u8string().c_str(), &handle) == SQLITE_OK, "Open fixture database");
        const int status = sqlite3_exec(handle,
            "CREATE TRIGGER fail_index BEFORE INSERT ON file_records "
            "WHEN instr(NEW.path, '/Text/') > 0 "
            "BEGIN SELECT RAISE(ABORT, 'fixture failure'); END;", nullptr, nullptr, nullptr);
        sqlite3_close(handle);
        check(status == SQLITE_OK, "Prepare deterministic indexing failure");
        const auto partial = FileWorkflow::apply(FileWorkflow::preview(partialRoot, partialDb));
        check(!partial.indexSaved && !partial.error.empty()
            && partial.moves.front().status == MoveStatus::Moved
            && fs::exists(partialRoot / "Text/fail.txt"),
            "Keep moved outcomes and data when final indexing fails");
    });
}
