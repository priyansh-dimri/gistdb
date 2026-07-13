# 0013 - The Hash Join Operator

**Date:** July 13, 2026

## Copying build-side rows instead of pointing at them

Every other operator so far has pointed at data instead of copying it, but hash join's build side is an exception. Copying matched build rows into owned storage up front means the build child's chunks can be let go right after the build phase finishes, and the probe phase never has to worry about their lifetime again.

## The probe side stays zero-copy

Probe chunks are read one at a time and never held onto past the `GetNext()` call that produced them, so there was no reason to copy anything there. Only the build side has the "has to outlive its own phase" problem, so only the build side needed the exception.

## Serializing the join key into one string

Both the build and probe sides turn their key columns into a single string, values appended byte-for-byte, VARCHAR length-prefixed so two adjacent strings can't be confused for one longer one, and that string is what actually gets hashed and compared.
