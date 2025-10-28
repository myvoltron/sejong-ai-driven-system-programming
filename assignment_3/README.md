# xsh (assignment_3) — quick tests and notes

This folder contains a small custom shell implementation (`xsh`) and a minimal smoke test script.

Quick test runner

Run from `dev/assignment_3` (ensure `./xsh` is built in that directory):

```bash
./tests/run_tests.sh
```

What it checks
- Basic `echo` output
- Redirection `>` and `>>`
- Environment variable expansion (prints `$PATH`)

Notes
- The test runner is intentionally simple: it feeds scripted commands into `xsh` via stdin.
- Prompts and colored output are included in the captured output; tests search for the expected content.
- If you want more robust integration/unit tests, I can add more cases and normalize outputs.
