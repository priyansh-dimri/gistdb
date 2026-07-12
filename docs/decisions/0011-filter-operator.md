# 0011 - The Filter Operator

**Date:** July 12, 2026

## ANDing into the selection vector instead of overwriting it

`FilterOperator` combines its predicate's result into the chunk's existing selection vector rather than replacing it outright. A chunk arriving at Filter might already have rows deselected by something upstream, and overwriting the selection vector would silently resurrect those rows the moment this filter's own predicate happens to pass them. ANDing means a row only stays selected if it survived every filter it's passed through so far, not just the most recent one.

## One chunk in, one chunk out, no draining

`FilterOperator` pulls exactly one chunk from its child per `GetNext()` call and returns it immediately. It doesn't need to see all of its input before producing anything, a row's fate depends only on that row, not on any other row in the stream, so there's no reason to hold data back the way a blocking operator like Aggregation has to.

## Null predicate results exclude the row, they don't error

If a predicate evaluates to null validity for a row, that row just gets excluded, the same as if it had failed the predicate outright. I didn't add special handling to distinguish "false" from "unknown", both mean the row doesn't make it through.
