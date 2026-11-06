//
// Created by arisu on 06/11/2026.
//

#include "../database/database.h"
#include "../wal/WAL.h"
#include "iostream"
#include <fstream>

//Initialize database, create database file if not exist
Database::Database()
    : dbPath("../dbfile/dbfile.txt") {
    std::filesystem::create_directories(dbPath.parent_path());
    if (!std::filesystem::exists(dbPath)) {
        if (std::ofstream file(dbPath); !file.is_open()) {
            throw std::runtime_error("Cannot create database file");
        }
    }
}

//Insert a pair key-value in the unsorted_map
void Database::put(
    const std::string &key,
    const std::string &value) {
    wal.appendPut(key, value);
    data[key] = value;
}

//Get the value from the key
std::string Database::get(
    const std::string &key)
const {
    return data.at(key);
}

//Remove a key-value pair from the unsorted_map
void Database::remove(
    const std::string &key) {
    if (data.contains(key)) {
        wal.appendRemove(key);
        data.erase(key);
    }
}

//Persist pairs in the database file
void Database::saveToDisk() {
    std::ofstream file(dbPath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for dump.");
    }

    for (auto const &[key, value]: data) {
        file << key << ": " << value << "\n";
    }
}

// Loads data from the database file into the unordered_map.
// Each line must follow the format:
//
//     key: value
//
// If a line does not contain the expected separator (": "),
// a runtime exception is thrown.
void Database::loadFromDisk() {
    std::string separator = ": ";

    std::ifstream file(dbPath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for load.");
    }

    data.clear();
    std::string line;
    int ctr = 1;

    while (std::getline(file, line)) {
        auto pos = line.find(separator);

        if (pos == std::string::npos) {
            throw std::runtime_error("Cannot find separator in database at line "
                                     + std::to_string(ctr)
                                     + ": " + line + "\n");
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + separator.size());

        data[key] = value;
        ctr++;
    }
}
