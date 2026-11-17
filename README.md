## Introduction

A key-value database written in C++, with durability provided by a
write-ahead log (WAL).

The WAL is an append-only file where every operation (put, remove) is
recorded *before* it is applied to the in-memory data. If the process
crashes, the database is rebuilt at startup from the last snapshot plus
the operations recorded in the WAL.

## Long-term Goal
Build a high-performance and reliable database server capable of supporting 
heavy concurrent workloads while maintaining data consistency and low latency. 
Planned features include a client-server architecture with user authentication 
and authorization, as well as support for storing and managing data formats beyond
plain text key-value pairs, including JSON, XML, and other structured document 
types.
## Current features
- In-memory store with `put`, `get`, `remove`
- Binary snapshot file with a checkpoint LSN
- Binary WAL with per-record CRC-32 and replay on startup
- Durable WAL appends: each record is fsynced before `put`/`remove` returns
- Atomic snapshots: written to `dbfile.db.tmp`, fsynced, then atomically
  renamed over `dbfile.db` (the old snapshot stays valid until the swap)
- Automatic checkpoint: when the WAL reaches a configurable size (default 4 MB), 
  a snapshot is written atomically and the WAL is then truncated to its header.
- WAL segmentation with automatic segment rotation
- Persistent WAL writer: the active WAL segment remains open between appends 
  and is fsynced after each record
- Concurrency (thread-safe access) with shared and unique lock

## Roadmap
- Bit-flip corruption test
- Key-value server with HTTP API
- Authentication and per-user access control
- Possibility to add more data-type saving (json, images...)
- Maybe make lock key-level instead of database-level

## On-disk format

All integers are little-endian (fixed width). Strings are
[length: uint32][bytes], without terminator.

### Snapshot (`dbfile.db`)

magic "KVDB" | version u32 (2) | checkpoint_lsn u64 | count u64 | pairs...

pair: owner_id u64 | key (string) | value (string)

The snapshot is written to `dbfile.db.tmp`, fsynced and atomically
renamed over `dbfile.db`.

### WAL (`walfiles/wal_NNNNNN.log`)

The WAL is split into segments named `wal_000000.log`, `wal_000001.log`, ...
(zero-padded id, never reused). A new segment is created when the active
one would exceed the maximum segment size.

Segment header (16 bytes):
magic "KVWL" | version u32 (2) | first_lsn u64

Followed by records:
record: len u32 | crc32 u32 | payload

payload (len bytes, covered by the CRC-32):
lsn u64 | owner_id u64 | op u8 | key (string) | value (string, PUT only)

op: 0x01 = PUT, 0x02 = REMOVE (no value)