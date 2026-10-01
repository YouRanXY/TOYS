#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>

namespace fs = std::filesystem;

inline void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class TestDirectory
{
public:
    fs::path path;

    explicit TestDirectory(const std::string& name)
        : path(fs::current_path() / ("fixture-" + name))
    {
        check(!fs::exists(path), "Fixture already exists: " + path.u8string());
        fs::create_directory(path);
    }

    ~TestDirectory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path write(const fs::path& name, const std::string& contents = "sample")
    {
        const auto file = path / name;
        fs::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary);
        output << contents;
        check(static_cast<bool>(output), "Cannot write fixture");
        return file;
    }
};

inline std::string readFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

template <typename Function>
int runTest(Function test)
{
    try
    {
        test();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
