# 0006 - Schema Section and a Shared Byte I/O Module

**Date:** July 11, 2026

## Schema stays declarative, physical facts stay elsewhere

`SchemaSection` only stores table id, name, and columns, nothing about row groups or row counts. That data belongs to `TableObject`, attached later as row groups actually get scanned or finalized. A table's schema is decided once at `CREATE TABLE` and never changes.

## next_table_id lives inside SchemaSection itself

`AddTable` hands out `next_table_id_` and increments it, all inside `SchemaSection`, so the counter and the schema it's counting are the same persisted object. I could have kept the counter somewhere else and passed it in, but then two things that always need to move and persist together would be split across two places for no reason.

## Rejecting a bad TypeId byte instead of casting blindly

`ReadTypeId` checks that the raw byte it read is a valid `TypeId` value before casting, and throws if not. It would be simpler to just `static_cast` whatever byte shows up, but a corrupted or truncated schema section would then quietly turn into a column with a non-deterministic type instead of a clear error at load time.

## Pulling the byte reader/writer out into its own module for modularity

The Footer already had its own hand-rolled `WriteU32`/`ByteReader`-style helpers, and `SchemaSection` needed the exact same kind of thing. Rather than copying that code a second time, I pulled it out into `gistdb::serialization::byte_io` and had both use it.
