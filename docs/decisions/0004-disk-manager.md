# 0004 - The Disk Manager

**Date:** July 10, 2026

## Two addressing schemes sharing one file

The file has to hold two different kinds of things: data pages, addressed by page ID AND the schema + footer metadata blob, addressed using a raw byte offset that moves every session.

I could have given each its own file, but that just trades one open file handle for two, and the whole reason for one file in the first place was to avoid managing multiple handles and their lifecycles separately. So both live in the same file, `AllocatePages`,`WritePages` and `ReadPages` functions will work in page-ID space, whereas, `WriteMetadataBlob` and `ReadMetadataBlob` functions will work in raw byte-offset space, and the only place those two schemes have to reason about each other is the append position in `WriteMetadataBlob`.

## Giving the header its own reserved page

Page 0 is reserved for the `FileHeader` and never handed out by `AllocatePages`, real data starts at page 1. I considered keeping the header outside the page-numbering scheme entirely, as its own special byte range before page 0 conceptually starts. But that would mean every other page-math calculation in the system has to remember to offset by the header's size. Making it page 0 in the same numbering scheme means "page N lives at byte N × 4096" will work always, and the only thing that needs to remember page 0 is special is `AllocatePages` function only which starts counting at 1.

## Keeping the metadata blob out of allocated-but-unwritten space

One consideration in this file is determining where `WriteMetadataBlob` should append. One option was to just append at end of file, but that did not work because if something calls `AllocatePages` for a batch of pages and hasn't written them yet, the physical file might not have grown to cover them, so "end of file" could end up inside a region that has already been reserved for allocated pages.

Instead we compute the append position using `max(current file size, next_free_page_id × page_size)`, so the blob always lands after every page that's been allocated, whether or not it's been physically written yet.

## Move-only, not copyable

DiskManager owns a raw file descriptor, so copying is intentionally disabled. Allowing copies would create multiple owners of the same file descriptor, which might cause a double-close during destruction. Move is implemented properly instead(by transferring the fd and nulling out the source).
