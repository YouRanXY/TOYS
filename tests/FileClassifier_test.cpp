#include "FileClassifier.h"
#include "TestSupport.h"

int main()
{
    return runTest([]
    {
        FileClassifier classifier;
        const auto result = classifier.classify({ "photo.JPG", "main.CPP", "note.MD",
            "plain.txt", "archive.zip", "LICENSE" });
        check(result.count("Images") == 1, "Uppercase JPG should classify as Images");
        check(result.at("Images").size() == 1, "Images count");
        check(result.at("Code").size() == 1, "Uppercase CPP should classify as Code");
        check(result.at("Text").size() == 2, "MD and txt should classify as Text");
        check(result.size() == 3, "Unknown extensions must not be moved");
    });
}
