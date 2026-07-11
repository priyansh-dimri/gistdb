# 0008 - DataChunk and the Operator Interface

**Date:** July 11, 2026

## DataChunk points at columns, it doesn't own them

`DataChunk` holds non-owning pointers to columns instead of copying their data in. The whole zero-copy idea only works if this is true everywhere by default, later on the hash join is going to be the one deliberate **exception** that copies.

## Operator is just GetNext, nothing about children

`Operator` is a single pure virtual method and nothing else, no generic "child" slot. Different operators need different numbers of children, filter has one, hash join has two, so I left that as each concrete operator's own member rather than forcing a shared shape onto the base interface that wouldn't actually fit every operator.
