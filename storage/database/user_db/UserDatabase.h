//
// Created by arisu on 16/11/2026.
//

#ifndef KWDB_USERDATABASE_H
#define KWDB_USERDATABASE_H
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

struct User {
    uint64_t userId;
    std::string userName;
    std::string passwordHash;
};

class UserDatabase {
public:
    explicit UserDatabase(std::filesystem::path usersDbPath);

    uint64_t reserveUserId();

    void createUser(
        uint64_t userId,
        const std::string &username,
        const std::string &passwordHash
    );

    void deleteUser(uint64_t userId);

    std::optional<uint64_t> authenticate(
        const std::string &name,
        const std::string &password
    ) const;

    bool exists(const std::string &name) const;

    void applyCreateUser(uint64_t userId, const std::string &username, const std::string &passwordHash);
    void applyDeleteUser(uint64_t id);

    void saveToDisk();
    void loadFromDisk();

private:

    std::filesystem::path path_;
    uint64_t nextId_ = 1;
    std::unordered_map<uint64_t, User> users_;
    std::unordered_map<std::string, uint64_t> byName_;
    mutable std::shared_mutex mutex_;
};


#endif //KWDB_USERDATABASE_H
