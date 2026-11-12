//
// Created by arisu on 10/11/2026.
//

#ifndef KWDB_FILEIO_H
#define KWDB_FILEIO_H

#include <filesystem>

void syncFile(const std::filesystem::path &filePath);

void syncDirectory(const std::filesystem::path &directoryPath);

void atomicReplace(const std::filesystem::path &filePath, const std::filesystem::path &newPath);

#endif //KWDB_FILEIO_H
