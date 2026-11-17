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

    void createUser(
        const std::string &name,
        const std::string &password
    );

    std::optional<uint64_t> authenticate(
        const std::string &name,
        const std::string &password
    ) const;

    bool exists(const std::string &name) const;

private:

    void save();
    void load();

    std::filesystem::path path_;
    uint64_t nextId = 1;
    std::unordered_map<uint64_t, User> users_;
    std::unordered_map<std::string, uint64_t> byName_;
    mutable std::shared_mutex mutex_;
};


#endif //KWDB_USERDATABASE_H
