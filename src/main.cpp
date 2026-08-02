#include<iostream>

#include"FileScanner.h"
#include "FileClassifier.h"
#include "FileMove.h"


int main()
{
    fs::path path ="C:\\Users\\XYoung\\Desktop\\FileManager\\test";
    FileClassifier classifier;
    FileMove mover;

    FileScanner scanner(path);
    
    auto files =scanner.scan();
    auto result = classifier.classify(files);
    mover.move(
        result,
        path
    );
    std::cout<<"Already Done!"<<std::endl;
    
    


    
    return 0;
}
