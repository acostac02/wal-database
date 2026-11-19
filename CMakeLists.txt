cmake_minimum_required(VERSION 4.0)
project(kwdb)

set(CMAKE_CXX_STANDARD 20)

add_executable(kwdb main.cpp
        database/database.cpp
        database/database.h
        wal/wal.cpp
        wal/wal.h
        format.h
        test/test.cpp
        test/test.h
        file_operations/fileio.h
        file_operations/fileio.cpp
        user_db/UserDatabase.cpp
        user_db/UserDatabase.h
        storage_engine/StorageEngine.cpp
        storage_engine/StorageEngine.h
        storage_engine/StorageEngine.h)
