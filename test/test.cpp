#include <cassert>

#include "../database/database.h"
#include <iostream>
#include "../storage_engine/StorageEngine.h"

namespace {
    const std::filesystem::path DB_DIR = "../dbfile";
    const std::filesystem::path WAL_DIR = DB_DIR / "walfiles";
    const std::filesystem::path WAL_PATH = WAL_DIR / "wal_000000.log"; // primo segmento
    const std::filesystem::path WAL_BACKUP = DB_DIR / "wal.backup";
}

void firstRun()
{
    std::filesystem::remove_all("../dbfile");

    StorageEngine engine(
        1024ULL * 1024 * 1024,
        1024 * 1024
    );

    const auto mario =
        engine.createUser(
            "mario",
            "hash_mario"
        );

    const auto luca =
        engine.createUser(
            "luca",
            "hash_luca"
        );

    const auto anna =
        engine.createUser(
            "anna",
            "hash_anna"
        );

    engine.put(
        mario,
        "mail",
        "mario@gmail.com"
    );

    engine.put(
        mario,
        "city",
        "Milan"
    );

    engine.put(
        luca,
        "mail",
        "luca@gmail.com"
    );

    engine.put(
        luca,
        "phone",
        "12345"
    );

    engine.put(
        anna,
        "mail",
        "anna@gmail.com"
    );

    engine.put(
        anna,
        "secret",
        "topsecret"
    );

    engine.remove(
        luca,
        "phone"
    );

    engine.deleteUser(anna);

    std::cout
        << "First run completed."
        << std::endl;
}

void secondRun()
{
    StorageEngine engine(
        1024ULL * 1024 * 1024,
        1024 * 1024
    );

    auto mario =
        engine.authenticate(
            "mario",
            "hash_mario"
        );

    auto luca =
        engine.authenticate(
            "luca",
            "hash_luca"
        );

    auto anna =
        engine.authenticate(
            "anna",
            "hash_anna"
        );

    assert(mario.has_value());
    assert(luca.has_value());
    assert(!anna.has_value());

    assert(
        engine.get(
            *mario,
            "mail"
        ) == "mario@gmail.com"
    );

    assert(
        engine.get(
            *mario,
            "city"
        ) == "Milan"
    );

    assert(
        engine.get(
            *luca,
            "mail"
        ) == "luca@gmail.com"
    );

    assert(
        !engine.has(
            *luca,
            "phone"
        )
    );

    std::cout
        << "Second run OK."
        << std::endl;
}
