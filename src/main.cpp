#include<iostream>
#include<filesystem>
#include<vector>

using std::cout;
using std::endl;
using std::string;
namespace fs = std::filesystem;
int main()
{
    // vector<string> files;
    // files.push_back("test.jpg");
    // files.push_back("hello.cpp");
    // files.push_back("note.txt");
    // for(auto file:files)
    //     cout<<file<<endl;

    string path = "C:\\Users\\XYoung\\Desktop\\FileManager\\test";
    for(auto& file: fs::directory_iterator(path))
        cout<<file.path().extension()<<endl;
    return 0;
}
