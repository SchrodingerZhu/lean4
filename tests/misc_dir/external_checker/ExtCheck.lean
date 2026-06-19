/-! Smoke test for `--external-checker-lib`. The reject-all mock external checker
rejects every declaration, so kernel checking of `foo` must fail with the mock's
`Kernel.Exception.other` message. -/

def foo : Nat := 1
