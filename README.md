# defiant-test

The milestone harness for Defiant. It takes a tenant's backup pair through
`gdefcore_subject_snapshot` (the data set) and `gdefcore_subject_keyset` (the
key set, from the tenant's key store), restores it through
`gdefcore_subject_restore`, and runs the six gates that close milestone 1:
cross-tenant isolation, an access-filter bypass, erasure that survives
restore, a memory-to-SQLite round trip, the unsafe-list lint for items 1–5,
and a scanner for an internal id in a rendered page.

A line is clean when it carries the marker `unsafe`. The scanner reads
bytes the caller already has. There is no HTTP response here.

```c
#include <ghoti.io/defiant-test/defiant-test.h>

int line = 0;
int item = 0;
if (gdeftest_lint(text, text_len, &line, &item) != GDEFTEST_OK) {
  /* line is 1-based, item is 1 through 5 */
}
```

## Building

The library finds `defiant-core`, `defiant-storage`, and both storage
backends through pkg-config. A missing dependency is a hard error.

```bash
make test PREFIX=/path/to/prefix
```

`make test` also runs `tools/check-gates.sh`. Each plant is an environment
value of `GHOTI_DEFTEST_PLANT` and must turn its gate red. Two gates plant
a defect in a scratch copy of source instead: `tools/check-archive.sh` (the
hostile-archive tests, against copies of defiant-core and defiant-storage) and
`tools/check-lint.sh` (the unsafe-list lint over every Defiant library's
source).

## API

`gdeftest_lint` greps for the tokens of unsafe-list items 1 to 5, and a
line that carries the word `unsafe` is clean:

- `unescaped-output`, unsafe-list item 1
- `unfiltered-read`, unsafe-list item 2
- `string-query`, unsafe-list item 3
- `unsigned-form`, unsafe-list item 4
- `direct-storage`, unsafe-list item 5

Items 6 and 7 are not in this lint. `gdeftest_response_has_internal_id` reports a decimal
integer standing on its own in a page or an error string. A numeric
character reference is not one.

The snapshot tests call the public import and the public render. They do
not add a signature.

## Status

Milestone 1. The format token is `defiant-snapshot/1`.
