---
description: Run the Unity host test suites (test/host) on this machine
allowed-tools: Bash(cd:*), Bash(pio test:*), Bash(~/.platformio/penv/bin/pio test:*)
---

Run the host tests and report the result:

```bash
cd test && pio test -e native_tests
```

If `pio` is not on PATH, use `~/.platformio/penv/bin/pio`.

Both suites (`host/test_server` and `host/test_lib`) must run — if the output
shows only one, or zero tests, something is wrong with `test_dir`/suite naming
rather than with the code. See the `host-tests` skill.

On failure, report the failing assertions verbatim before proposing a fix. Do not
"fix" a test by loosening an assertion unless the specification itself changed.
