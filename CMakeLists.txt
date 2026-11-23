cmake_minimum_required(VERSION 4.0)
project(kwdb)

set(CMAKE_CXX_STANDARD 20)

add_executable(kwdb main.cpp
        storage/database/database/database.cpp
        storage/database/database/database.h
        storage/wal/wal.cpp
        storage/wal/wal.h
        common/format.h
        test/test.cpp
        test/test.h
        common/file_operations/fileio.h
        common/file_operations/fileio.cpp
        storage/database/user_db/UserDatabase.cpp
        storage/database/user_db/UserDatabase.h
        storage/storage_engine/StorageEngine.cpp
        storage/storage_engine/StorageEngine.h
        storage/storage_engine/StorageEngine.h
        server/TcpServer.cpp
        server/TcpServer.h
        server/Request.h
        server/Response.h
        server/Session.h
        server/SocketDefs.h
        server/Command.h
        server/RequestParser.h
)
