#include "FileMove.h"
#include "TestSupport.h"

int main()
{
    return runTest([]
    {
        TestDirectory fixture("mover");
        const auto source = fixture.write("same.txt", "source");
        const auto destination = fixture.write("Text/same.txt", "destination");
        FileMove mover;
        mover.move({ { "Text", { source } } }, fixture.path);
        check(readFile(source) == "source", "Collision should retain source");
        check(readFile(destination) == "destination", "Collision should retain destination");
        const auto good = fixture.write("new.txt");
        mover.move({ { "Text", { good } } }, fixture.path);
        check(!fs::exists(good) && fs::exists(fixture.path / "Text/new.txt"),
            "Move into an existing category directory");
        const auto preview = fixture.write("preview.cpp");
        const auto plan = mover.move({ { "Code", { preview } } }, fixture.path, true);
        check(plan.front().status == MoveStatus::Planned, "Preview returns a plan");
        check(fs::exists(preview) && !fs::exists(fixture.path / "Code"),
            "Preview must not create directories or move files");
        fixture.write("Images", "category blocker");
        const auto image = fixture.write("photo.jpg");
        const auto blocked = mover.move({ { "Images", { image } } }, fixture.path);
        check(blocked.front().status == MoveStatus::Failed && fs::exists(image),
            "Blocked category must preserve source");
        const auto missing = fixture.path / "gone.txt";
        const auto next = fixture.write("next.txt");
        const auto mixed = mover.move({ { "Text", { missing, next } } }, fixture.path);
        check(mixed[0].status == MoveStatus::Failed && mixed[1].status == MoveStatus::Moved,
            "One missing source must not stop the remaining moves");
        const auto invalid = mover.move({ { "../outside", { image } } }, fixture.path);
        check(invalid.front().status == MoveStatus::Failed && fs::exists(image),
            "Reject categories escaping the root");
        fixture.write("Text/directory.txt/child");
        const auto directoryCollision = fixture.write("directory.txt");
        const auto collision = mover.move({ { "Text", { directoryCollision } } }, fixture.path);
        check(collision.front().status == MoveStatus::Skipped && fs::exists(directoryCollision),
            "Destination directory counts as a collision");
        const auto linkedSource = fixture.write("linked.txt", "retained");
        std::error_code linkError;
        fs::create_symlink(fs::absolute(fixture.path / "absent-target"),
            fixture.path / "Text/linked.txt", linkError);
        if (!linkError)
        {
            const auto linkCollision = mover.move({ { "Text", { linkedSource } } }, fixture.path);
            check(linkCollision.front().status == MoveStatus::Skipped && fs::exists(linkedSource)
                && fs::is_symlink(fs::symlink_status(fixture.path / "Text/linked.txt")),
                "Dangling destination symlink must retain source and link");
        }
        else
        {
            std::cout << "Symbolic-link fixture unavailable: " << linkError.message() << '\n';
        }
    });
}
