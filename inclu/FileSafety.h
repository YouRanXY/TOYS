#pragma once

#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Include Windows junctions and other reparse points.
inline bool isLink(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path));
#endif
}
