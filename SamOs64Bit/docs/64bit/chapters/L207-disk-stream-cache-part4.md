# Lecture 207 - disk stream cache source part 4

Upstream: PeachOS64BitCourse 5d6658a.

(Upstream's commit message is mislabeled "Lecture 107" -
SamOs treats it as L207 per the actual project arc and the
preceding L206. Preserved verbatim in the docs.)

## What landed

- `diskstreamer_new` becomes a one-liner that calls
  `diskstreamer_new_from_disk` after resolving the disk by id.
  Both constructors share the same pos / sector_size / disk
  init path now.
- `disk_read_block` switches `disk->driver->functions.read` to
  `idisk->driver->functions.read`. SamOs already used `idisk`
  consistently since L186, so no change here.

## BIOS test status

Source + link. Build links.
