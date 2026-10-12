# defiant-test

The milestone harness for Defiant. It exports a tenant through
`gdefcore_subject_snapshot`, imports that archive through
`gdefcore_subject_import`, and runs the six gates that close milestone 1:
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
value of `GHOTI_DEFTEST_PLANT` and must turn its gate red.

## API

`gdeftest_lint` greps for `unescaped-output`, `unfiltered-read`,
`string-query`, `unsigned-form`, and `direct-storage`. Items 6 and 7 are
not in this lint. `gdeftest_response_has_internal_id` reports a decimal
integer standing on its own in a page or an error string. A numeric
character reference is not one.

The snapshot tests call the public import and the public render. They do
not add a signature.

## Status

Milestone 1. The format token is `defiant-snapshot/1`.
