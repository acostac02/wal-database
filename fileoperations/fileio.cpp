//
// Created by arisu on 10/11/2026.
//

#include "fileio.h"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

///force SO to sync the file in mass storage
void syncFile(const std::filesystem::path &filePath) {
#ifdef _WIN32
    HANDLE hFile = CreateFileW( //W stands for wide (UTF-16) not write
        filePath.c_str(),
        GENERIC_WRITE,                      //FlushFileBuffer require write permissions
        FILE_SHARE_READ | FILE_SHARE_WRITE, //Other handles can read and write while the file is opened
        nullptr,
        OPEN_EXISTING,         //open only if the file exists
        FILE_ATTRIBUTE_NORMAL, //no special attributes
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to open file for the fsync, error "
                                 + std::to_string(GetLastError()));
    }
    BOOL result = FlushFileBuffers(hFile);
    CloseHandle(hFile);
    if (!result) {
        throw std::runtime_error("FlushFileBuffers failed.");
    }
#else
    int fd = ::open(filePath.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("failed to open file for fsync.");
    int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("failed to fsync file.");
#endif
}

/// Replace the db.tmp file to .db file atomically
void atomicReplace(const std::filesystem::path &tmpPath, const std::filesystem::path &finalPath) {
    syncFile(tmpPath);
#ifdef _WIN32
    BOOL success = MoveFileExW(
        tmpPath.c_str(),
        finalPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        );
    if (!success) {
        DWORD error = ::GetLastError();
        throw std::runtime_error("An error occurred during the atomic replace of the database file: error code "
            + std::to_string(error));
    }
#else
    std::filesystem::rename(tmpPath, finalPath);

    auto dir = finalPath.parent_path();
    if (dir.empty()) dir = ".";
    int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("failed to open directory for fsync.");
    int rc = ::fsync(fd);
    ::close(fd);
    if (rc != 0) throw std::runtime_error("failed to fsync directory.");
#endif
}

