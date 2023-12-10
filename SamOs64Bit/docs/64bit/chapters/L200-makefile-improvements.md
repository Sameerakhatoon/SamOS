# Lecture 200 - makefile improvements

Upstream: PeachOS64BitCourse 9d8497a.

## What landed

- `.PHONY: all clean user_programs user_programs_clean` so the
  recursive-make invocations aren't short-circuited by stray
  filesystem entries.
- User-program targets switch from `cd ./programs/X && $(MAKE)`
  to `$(MAKE) -C ./programs/X` so make-flags and the jobserver
  propagate cleanly.
- `user_programs_clean` mirrors the user_programs target.
- Build order tightened: stdlib first, then blank / shell /
  simple.

SamOs does not include the upstream `containerlib` and
`guilib` directories, so those entries stay out of FILES.

## BIOS test status

Source + link. Build links.
