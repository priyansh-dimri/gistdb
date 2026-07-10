# 0002 - A Scoped Temp File Helper for Tests

**Date:** July 10, 2026

## RAII for cleanup, not a manual teardown

`ScopedTempFile` creates its file in the constructor and removes it in the destructor. I could have just had each test create a filename and delete it in TearDown, but that only cleans up on the happy path. If an assertion fails partway through a test, execution stops right there and the file leaks.

## mkstemp over hand-rolling a unique name

I used `mkstemp` instead of generating a filename myself (for example: appending a counter or timestamp) and then opening it. Building the name and creating the file as two separate steps leaves a classic **time of check to time of use** (TOCTOU) race where two concurrent test processes can choose the same filename before either creates it. Whereas, `mkstemp` creates the file atomically as part of picking the name, so that race doesn't exist.

## Non-copyable and non-movable

Copy is deleted because two copies would both think they own the same path and both try to delete it. I could have made it movable instead of banning that too, but nothing in the test suite needs to hand ownership of a temp file off between scopes so I left move deleted too.
