#!/usr/bin/env python3
# Storage test helper. The byte pattern must match
# st_pattern() in src/fs/storage_selftest.c.
#
#   pattern.py gen   <out> <size> <seed>
#   pattern.py check <file> <size> <seed>
#   pattern.py tree  <dir>      - check a tree copied out of a
#                                 scratch image after the WRITE boot
import os
import sys


def pattern(size, seed):
    return bytes(((i * 31 + seed * 17 + (i >> 8)) & 0xFF) for i in range(size))


def check(path, size, seed):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != size:
        return "size %d != %d" % (len(data), size)
    if data != pattern(size, seed):
        bad = next(i for i, (a, b) in enumerate(zip(data, pattern(size, seed))) if a != b)
        return "content differs at byte %d" % bad
    return None


def expect_text(path, text):
    with open(path, "rb") as f:
        data = f.read()
    return None if data == text.encode() else "got %r" % data[:80]


def tree(root):
    errors = []

    def run(name, fn, *args):
        # One check at a time, so a missing file is reported and
        # the remaining checks still run.
        try:
            err = fn(*args)
        except OSError as e:
            err = "%s: %s" % (e.filename, e.strerror)
        if err:
            errors.append("%s: %s" % (name, err))

    j = lambda *p: os.path.join(root, *p)
    def sparse(path):
        with open(path, "rb") as f:
            data = f.read()
        return None if data == b"\0" * 5000 + b"END" else "wrong content (len %d)" % len(data)

    def absent(path):
        return "still exists" if os.path.exists(path) else None

    def many(path):
        got = sorted(os.listdir(path))
        want = sorted("file number %02d with a long name.txt" % i for i in range(40))
        if got != want:
            return "listing differs: %d entries, first %r" % (len(got), got[:2])
        for i in range(40):
            err = expect_text(os.path.join(path, want[i]), "file %02d\n" % i)
            if err:
                return "%s: %s" % (want[i], err)
        return None

    run("big file", check, j("SUBDIR", "Nested Long Directory Name", "Big File Data.bin"), 300000, 1)
    run("hello.txt", expect_text, j("hello.txt"), "Hello from SamOS\n")
    run("MixedCase.Txt", expect_text, j("MixedCase.Txt"), "mixed case long name\n")
    run("append.log", expect_text, j("append.log"), "one\ntwo\n")
    run("TRUNC.BIN", check, j("TRUNC.BIN"), 3000, 2)
    run("TRUNC2.BIN", expect_text, j("TRUNC2.BIN"), "short\n")
    run("SPARSE.BIN", sparse, j("SPARSE.BIN"))
    run("DELETE ME.txt", absent, j("DELETE ME.txt"))
    run("EMPTYDIR", absent, j("EMPTYDIR"))
    run("MANY", many, j("SUBDIR", "MANY"))
    return errors


def main():
    cmd = sys.argv[1]
    if cmd == "gen":
        with open(sys.argv[2], "wb") as f:
            f.write(pattern(int(sys.argv[3]), int(sys.argv[4])))
    elif cmd == "check":
        err = check(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
        if err:
            print(err)
            sys.exit(1)
    elif cmd == "tree":
        errors = tree(sys.argv[2])
        for e in errors:
            print(e)
        sys.exit(1 if errors else 0)
    else:
        sys.exit("unknown command " + cmd)


if __name__ == "__main__":
    main()
