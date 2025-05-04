#!/usr/bin/env python3
"""e2e/screen_match.py - match screen rows read back by screen_text.py
against the serial log, allowing for cells something drew over.

    screen_match.py <screen.txt> <serial.txt> <row regex> [min known]

BLANK.ELF's selftest windows and the mouse cursor can cover part of
the boot text before a screendump; screen_text.py reads those cells
as '?'. A row (that matches the regex) matches a serial line when,
after its trailing blanks and '?' are cut, every cell that is not '?'
is the serial line's character at that column, and at least
<min known> (default 12) cells are known. A framebuffer drawn with
the wrong pitch or size scrambles whole rows, which still fail.

Prints "match: <row>" or "nomatch: <row>" for every row tested; rows
with fewer known cells than the minimum are left out.
"""
import re
import sys


def main():
    screen, serial, pattern = sys.argv[1], sys.argv[2], sys.argv[3]
    min_known = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    with open(serial, encoding="latin-1") as f:
        lines = [l.rstrip("\r\n") for l in f]
    rx = re.compile(pattern)
    with open(screen, encoding="latin-1") as f:
        for row in f:
            row = row.rstrip("\n")
            if not rx.search(row):
                continue
            cut = row.rstrip(" ?")
            known = sum(1 for c in cut if c != "?")
            if known < min_known or known * 2 < len(cut):
                continue
            ok = any(
                len(l) >= len(cut)
                and all(c == "?" or c == l[i] for i, c in enumerate(cut))
                for l in lines
            )
            print(("match: " if ok else "nomatch: ") + row)


if __name__ == "__main__":
    main()
