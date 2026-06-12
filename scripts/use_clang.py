"""
scripts/use_clang.py — PlatformIO extra_scripts hook for [env:native].

PlatformIO's native platform (builder/main.py) always calls env.Tool("g++")
which unconditionally replaces CXX and CC.  This script runs AFTER that call
and therefore CAN override CXX back to Clang on Linux (where the PATH lookup
finds 'clang++-18' or 'clang++' first).  On Windows, if MinGW g++ is before
Clang in PATH, the lookup finds g++ and ASan/UBSan are silently omitted — the
test still passes, sanitizers just run in CI.

On Linux CI the job sets:  CC: clang-18  CXX: clang++-18  (in ci.yml)
Those are what this script reads from the environment.

IMPORTANT: We never override LINK independently from CXX.  Mixing g++ objects
(COFF) with the clang++ linker produces a fatal COMDAT-section mismatch on
Windows.  Always keep compile + link in sync.
"""

import os
Import("env")  # noqa: F821  — SCons injects this

requested_cxx = os.environ.get("CXX", "")
requested_cc  = os.environ.get("CC",  "")

if requested_cxx:
    # Do NOT set LINK here. If CXX gets overridden back to g++ by PlatformIO's
    # env.Tool("g++"), a separate LINK=clang++ would mix COFF objects with the
    # LLVM linker and produce error LNK1143. Keep compile + link in sync.
    env.Replace(CC=requested_cc or "clang", CXX=requested_cxx)
    # Verify the binary is actually reachable before adding sanitizer flags
    actual_cxx = env.WhereIs(requested_cxx) or env.WhereIs(requested_cxx + ".exe") or ""
    if actual_cxx and "clang" in actual_cxx.lower():
        # clang is confirmed: enable sanitizers + LLVM coverage instrumentation
        env.Append(CCFLAGS=["-fsanitize=address,undefined",
                             "-fprofile-instr-generate",
                             "-fcoverage-mapping"])
        env.Append(LINKFLAGS=["-fsanitize=address,undefined",
                               "-fprofile-instr-generate"])
        print(f"[use_clang.py] Clang confirmed ({actual_cxx}): ASan+UBSan+coverage ON")
    else:
        print(f"[use_clang.py] Requested CXX={requested_cxx!r} not found or not clang; "
              "no sanitizers added")
else:
    # On Windows the MinGW runtime DLLs (libgcc_s_seh-1, libstdc++-6,
    # libwinpthread-1) are in the MinGW bin directory, which is on the shell
    # PATH but NOT on the Windows-native PATH seen by Python subprocess.
    # Statically link the runtime so the test runner exe is self-contained.
    import sys
    if sys.platform == "win32":
        env.Append(LINKFLAGS=[
            "-static-libgcc",
            "-static-libstdc++",
            "-Wl,-Bstatic,--whole-archive",
            "-lwinpthread",
            "-Wl,--no-whole-archive,-Bdynamic",
        ])
        print("[use_clang.py] CXX env var not set; g++ on Windows — static runtime linked")
    else:
        print("[use_clang.py] CXX env var not set; using platform default compiler")
