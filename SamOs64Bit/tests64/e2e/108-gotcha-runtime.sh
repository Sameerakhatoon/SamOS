#!/usr/bin/env bash
# e2e/108-gotcha-runtime.sh - runtime regression anchors for the
# real-bug gotchas (G18 + G37). Both are documented in
# debugging-history.md; their fixes are source-anchored by
# 22-gotcha-fidelity. This test adds a runtime mark so a
# regression that silently undoes either fix also fails an
# e2e (not just the grep).
. "$(dirname "$0")/_lib.sh"
boot_and_dump 25
expect_stage_value 144 -eq 1 "G37 task_sleep math regression anchor"
expect_stage_value 145 -eq 1 "G18 window_event_push two-push regression anchor"
echo "PASS: e2e/108 gotcha runtime anchors"
