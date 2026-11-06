//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_DATABASE_H
#define KWDB_DATABASE_H
#include <filesystem>
#include <string>
#include <unordered_map>

#include "../wal/WAL.h"


class Database {
    std::filesystem::path dbPath;
    std::unordered_map<std::string, std::string> data;
    WAL wal;

public:
    Database();

    void put(
        const std::string& key,
        const std::string& value
        );

    std::string get(
        const std::string& key)
    const;

    void remove(const std::string& key);

    void saveToDisk();

    void loadFromDisk();

};


#endif //KWDB_DATABASE_H