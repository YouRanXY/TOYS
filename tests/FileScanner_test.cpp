#include "FileScanner.h"
#include "TestSupport.h"

int main()
{
    return runTest([]
    {
        TestDirectory fixture("scanner");
        fixture.write("z.txt");
        fixture.write("a.zip");
        fixture.write("nested/child.txt");
        std::error_code linkError;
        fs::create_symlink(fs::absolute(fixture.path / "z.txt"), fixture.path / "link.txt", linkError);
        if (linkError)
        {
            std::cout << "Symbolic-link fixture unavailable: " << linkError.message() << '\n';
        }
        const auto result = FileScanner(fixture.path).scan();
        check(result.size() == 2, "Scan must only include top-level regular files, excluding links");
        check(result.front().filename() == "a.zip", "Scan order should be deterministic");
        bool rejected = false;
        try { FileScanner(fixture.path / "missing").scan(); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "Missing root should report an error");
        rejected = false;
        try { FileScanner(fixture.path / "a.zip").scan(); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "Regular file root should report an error");
    });
}
