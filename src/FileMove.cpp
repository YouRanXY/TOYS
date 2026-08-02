#include "FileMove.h"



void FileMove::move(
    const std::map<std::string, std::vector<fs::path>>& files,
    const fs::path& target
)
{

    for(auto& category : files)
    {

        // 创建分类文件夹
        fs::path folder = target / category.first;


        if(!fs::exists(folder))
        {
            fs::create_directory(folder);
        }



        // 移动每一个文件
        for(auto& file : category.second)
        {

            fs::path destination = folder / file.filename();


            fs::rename(
                file,
                destination
            );

        }

    }

}