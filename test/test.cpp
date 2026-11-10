//
// Created by arisu on 10/11/2026.
//

#include <cassert>
#include <iostream>
#include <stdexcept>

#include "../database/database.h"

void testA() {
    std::filesystem::remove_all("../dbfile");

    {
        Database db;

        db.put("a", "1");
        db.put("b", "2");
        db.saveToDisk();                 // a, b in snapshot (checkpoint_lsn = 2)

        db.put("c", "3"); // only in nel (lsn 3)
        db.remove("a");           // only in WAL (lsn 4)
        // no saveToDisk: simulated crash, db destroyed from here.
    }

    {
        Database db;                       // snapshot + replay
        assert(db.get("b") == "2");
        assert(db.get("c") == "3"); //retrieved from wal
        // "a" should not be present: get("a") throws out_of_range
        bool threw = false;
        try { db.get("a"); } catch (const std::out_of_range&) { threw = true; }
        assert(threw);
    }
}

void testB() {
    auto walPath = std::filesystem::path("../dbfile/wal.log");
    std::filesystem::resize_file(walPath, std::filesystem::file_size(walPath)-3); //truncate

    Database db;
    for (auto k : {"a", "b", "c"})
        std::cout << k << (db.has(k) ? " present\n" : " absent\n"); //should not
}
