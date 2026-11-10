//
// Created by arisu on 10/11/2026.
//

#include "fileio.h"

//C++ does not have native function to execute the fsync() function, instead we use system functions.
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

//force SO to sync the file in mass storage
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
    int fd = ::open(filePath.c_str(), 0_RDONLY);
    if (fd < 0) throw std::runtime_error("failed to open file for fsync.");
    int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("failed to fsync file.");
#endif
}
