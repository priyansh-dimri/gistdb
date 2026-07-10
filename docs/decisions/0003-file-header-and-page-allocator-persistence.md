# 0003 - File Header and Persisting the Page Allocator

**Date:** July 10, 2026

## A fixed byte-0 header pointing at the metadata blob

The database is one file and the schema plus footer metadata gets rewritten and appended every session, so there needs to be something at a known, fixed location saying where the current copy of that metadata actually lives. I put a small fixed-size `FileHeader` at byte 0 holding a `meta_offset`. It is the one thing in the file whose location is never changed at all.

## Adding a magic string

Beyond the offset itself, I added an 8-byte magic value ('GISTDB01') at the front. The first time someone points GistDB at a file that isn't a GistDB file, or a truncated/corrupted one, I want `Deserialize` to fail immediately with a clear error instead of reading whatever garbage bytes happen to be there and quietly misinterpreting them as a real offset.

## Persisting the page allocator's counter in the same header

The page allocator is a monotonic bump counter with no free list, nothing ever gets reused. But a counter like that only works if it survives a restart, otherwise the first insert after reopening the database would start handing out page IDs that are already in use. Rather than reconstructing a `next_free_page_id` at startup by scanning every row group's page ranges and finding the max, I persist that in the header and update it alongside `meta_offset` whenever new pages get allocated.
