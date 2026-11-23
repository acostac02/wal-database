//
// Created by arisu on 16/11/2026.
//

#include "UserDatabase.h"

#include <fstream>
#include <iostream>
#include <mutex>

#include "../../../common/format.h"
#include "../../../common/file_operations/fileio.h"

#include <utility>
#include <windows.h>

namespace {
    constexpr char MAGIC[4] = {'U', 'S', 'D', 'B'};
    constexpr uint32_t VERSION = 1;
    constexpr uint64_t HEADER_SIZE = sizeof(MAGIC) + sizeof(VERSION) + sizeof(uint64_t) + sizeof(uint64_t);

    /**
     * Represents the database file header that contains:
     * - current file version;
     * - number of users inserted (count);
     * - the next available user id.
     */
    struct Header {
        uint32_t version = VERSION;
        uint64_t count = 0;
        uint64_t nextId = 1;
    };

    /**
     * Writes the user database file header.
     *
     * @param out the output file stream
     * @param h the header to write
     */
    void writeHeader(std::ofstream &out, const Header &h) {
        out.write(MAGIC, sizeof(MAGIC));
        format::writeValue(out, h.version);
        format::writeValue(out, h.count);
        format::writeValue(out, h.nextId);
    }

    /**
     * Read the user database file header.
     *
     * Check the header magic and version, if one is not correct, throw an exception.
     *
     * @param in the input file stream
     * @return the generated header struct
     * @throws std::runtime_error if a field value is incorrect
     */
    Header readHeader(std::ifstream &in) {
        char magic[4];
        in.read(magic, sizeof(magic));

        if (!in || memcmp(magic, MAGIC, 4) != 0) {
            throw std::runtime_error("Invalid user database file, incorrect header magic.");
        }

        Header h;
        h.version = format::readValue<uint32_t>(in);

        if (h.version != VERSION) {
            throw std::runtime_error("Invalid user database version.");
        }

        h.count = format::readValue<uint64_t>(in);
        h.nextId = format::readValue<uint64_t>(in);
        return h;
    }
}

/**
 * User database object constructor.
 *
 * If not exist, first create user database directory and file, then open the created file
 * and write the header with the MAGIC and the current version.
 * After the creation, the file is flushed into os buffer and the synchronization forced
 * by using the syncFile() function.
 * The in-memory map is filled by loadFromDisk() function call.
 */
UserDatabase::UserDatabase(std::filesystem::path usersDbPath)
    : path_(std::move(usersDbPath)) {
    std::filesystem::create_directories(path_.parent_path());

    //if file exists but only have a corrupt header
    if (std::filesystem::exists(path_) && std::filesystem::file_size(path_) < HEADER_SIZE) {
        std::filesystem::remove(path_);
    }

    if (!std::filesystem::exists(path_)) {
        {
            std::ofstream out(path_, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                throw std::runtime_error("Failed to create user database.");
            }
            Header h{};
            writeHeader(out, h);
            out.flush();
            if (!out) {
                throw std::runtime_error("Failed to insert header in user database file.");
            }

            out.close();
            if (out.fail()) {
                throw std::runtime_error("Failed to close user database file.");
            }

            syncFile(path_);
        }
    }
    loadFromDisk();
}

/**
 * Load all user from persistent storage (user db file) into memory.
 *
 * Reads and validates the database header, then loads every user record into the in-memory indexes.
 * Duplicate or invalid records are skipped.
 *
 * @throws std::runtime_error If the file cannot be opened or the database
 *         is corrupted.
 */
void UserDatabase::loadFromDisk() {
    std::unique_lock lock(mutex_);

    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open user database file.");
    }
    //read and check the header
    Header h{};
    try {
        h = readHeader(in);
    } catch (std::exception &e) {
        throw std::runtime_error(std::string("Failed to load user database: ") + e.what());
    }

    users_.clear();
    byName_.clear();

    for (uint64_t i = 0; i < h.count; i++) {
        try {
            const auto userId = format::readValue<uint64_t>(in);
            const std::string userName = format::readString(in);
            const std::string passwordHash = format::readString(in);

            if (userId == 0) {
                std::cerr
                        << "Found user with reserved id 0 during the load from disk in pos: "
                        << i
                        << std::endl;
                continue;
            }
            if (bool exists = users_.contains(userId)) {
                std::cerr
                        << "Found duplicate user id: "
                        << userId
                        << " in load from disk, in count: "
                        << i << std::endl;
                continue;
            }

            if (byName_.contains(userName)) {
                std::cerr << "Duplicate username: "
                        << userName
                        << std::endl;
                continue;
            }

            users_[userId] = User{userId, userName, passwordHash};
            byName_[userName] = userId;
        } catch (const std::exception &e) {
            throw std::runtime_error(
                "Corrupted user database while reading user " +
                std::to_string(i)
            );
        }
    }
    nextId_ = h.nextId;
}

/**
 * Persist the current in-memory user database to disk.
 *
 * The database is first written to a temporary file and then atomically replaced to prevent partial writes.
 *
 * @throws std::runtime_error for file errors
 */
void UserDatabase::saveToDisk() {
    std::shared_lock lock(mutex_);

    std::filesystem::path tmpPath = path_;
    tmpPath += ".tmp";

    std::ofstream tmpFile(tmpPath, std::ios::binary | std::ios::trunc);
    if (!tmpFile.is_open()) {
        throw std::runtime_error("Failed to create tmp user database file.");
    }

    Header h;
    h.count = users_.size();
    h.nextId = nextId_;
    writeHeader(tmpFile, h);

    for (const auto &[id, user]: users_) {
        format::writeValue(tmpFile, id);
        format::writeString(tmpFile, user.userName);
        format::writeString(tmpFile, user.passwordHash);
    }

    tmpFile.flush();
    if (!tmpFile) {
        throw std::runtime_error("Failed to insert users in the user database tmp file.");
    }

    tmpFile.close();
    if (tmpFile.fail()) {
        throw std::runtime_error("Failed to close user database tmp file.");
    }

    try {
        atomicReplace(tmpPath, path_);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        throw;
    }
}

uint64_t UserDatabase::reserveUserId() {
    std::unique_lock lock(mutex_);
    return nextId_++;
}


void UserDatabase::createUser(uint64_t userId, const std::string &username, const std::string &passwordHash) {
    std::unique_lock lock(mutex_);

    users_[userId] = User{userId, username, passwordHash};
    byName_[username] = userId;
}

void UserDatabase::deleteUser(uint64_t userId) {
    std::unique_lock lock(mutex_);

    auto it = users_.find(userId);
    if (it == users_.end()) {
        return;
    }

    byName_.erase(it->second.userName);
    users_.erase(it);
}


std::optional<uint64_t> UserDatabase::authenticate(const std::string &name, const std::string &password) const {
    std::shared_lock lock(mutex_);

    auto it = byName_.find(name);
    if (it == byName_.end()) {
        return std::nullopt;
    }

    const uint64_t userId = it->second;
    const User &user = users_.at(userId);
    if (user.passwordHash == password) {
        return userId;
    }
    return std::nullopt;
}

bool UserDatabase::exists(const std::string &name) const {
    std::shared_lock lock(mutex_);
    return byName_.contains(name);
}

void UserDatabase::applyCreateUser(uint64_t userId, const std::string &username, const std::string &passwordHash) {
    std::unique_lock lock(mutex_);
    users_[userId] = User{
        userId,
        username,
        passwordHash
    };

    byName_[username] = userId;
}

void UserDatabase::applyDeleteUser(uint64_t id) {
    std::unique_lock lock(mutex_);

    auto it = users_.find(id);
    if (it == users_.end()) {
        return;
    }
    byName_.erase(it->second.userName);
    users_.erase(id);
}

namespace udb {
} // udb
