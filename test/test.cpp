//
// Created by arisu on 10/11/2026.
//

#include <cassert>
#include <iostream>
#include <stdexcept>

#include "../database/database.h"

namespace {
    const std::filesystem::path DB_DIR = "../dbfile";
    const std::filesystem::path WAL_PATH = DB_DIR / "wal.log";

    void printState(Database& db) {
        for (auto k : {"a", "b", "c"})
            std::cout << k << (db.has(k) ? " present\n" : " absent\n");
    }
}

// Crash after a checkpoint: "c" and remove("a") live only in the WAL.
void testA() {
    std::cout << "=== testA ===\n";
    std::filesystem::remove_all(DB_DIR);   // clean state

    {
        Database db;
        db.put("a", "1");
        db.put("b", "2");
        db.saveToDisk();      // checkpoint_lsn = 2

        db.put("c", "3");     // WAL only (lsn 3)
        db.remove("a");       // WAL only (lsn 4)
        // no saveToDisk: simulated crash, RAM is lost here
    }

    {
        Database db;          // snapshot + replay
        printState(db);
        assert(!db.has("a"));   // REMOVE replayed
        assert(db.has("b"));    // from snapshot
        assert(db.has("c"));    // from WAL
    }
}

// Crash in the middle of the last record: remove("a") is lost.
// Depends on testA having just run, so call it right after testA().
void testB() {
    std::cout << "=== testB ===\n";

    const auto size = std::filesystem::file_size(WAL_PATH);
    std::filesystem::resize_file(WAL_PATH, size - 3);   // truncate last record

    Database db;              // snapshot + replay (stops at broken record)
    printState(db);
    assert(db.has("a"));      // remove(lsn 4) truncated, not applied
    assert(db.has("b"));
    assert(db.has("c"));      // lsn 3 intact
}

