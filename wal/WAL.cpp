//
// Created by arisu on 06/11/2026.
//

#include "wal.h"
#include <filesystem>
#include "fstream"

//Create the WAL file if not exist.
//The log format in the file should be:
//  OPERATION KEY VALUE
WAL::WAL()
    : walPath("../walfile/walfile.txt") {
    std::filesystem::create_directories(walPath.parent_path());
    if (!std::filesystem::exists(walPath.parent_path())) {
        if (std::ofstream file(walPath); !file.is_open()) {
            throw std::runtime_error("Cannot create WAL file");
        }
    }
}

void WAL::appendPut(const std::string &key, const std::string &value) {
    std::ofstream file(walPath, std::ios::app);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open WAL file");
    }
    file << PUT_OP
            << SEPARATOR
            << key
            << SEPARATOR
            << value
            << "\n";
}

void WAL::appendRemove(const std::string &key) {
    std::ofstream file(walPath, std::ios::app);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open WAL file");
    }
    file << REMOVE_OP
            << SEPARATOR
            << key
            << "\n";
}
