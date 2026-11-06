# Genuine public platform providers

Cinder identified PE1's working non-game platform inputs; these are reused
read-only, with isolated Cedar object/archive output. They are source/link
evidence, not Cedar runtime qualification.

Public PS5 Payload SDK revision b83202be73e930050e00de0f1b4d9ec46c0391bf,
platform/src/memstream.c and platform/include/ps5platform/libc.h,
Copyright2026 Mihawk, GPL-3.0-or-later. SDK LICENSE was read and is retained at the
source root. build-platform-compat.ps1 compiles this source unchanged; it neither
copies private game glue nor invents native FILE internals.

The provider uses libc's FILE on a pipe and a reader thread, publishing memory
at wrapped fflush/fclose. ALL THREE options are mandatory:
`--wrap=fclose`, `--wrap=fflush`, `--defsym=open_memstream=ps5_open_memstream`.
Linking only a nominal function is insufficient. The source supplies the actual
publication/cleanup code; this does not establish every POSIX seeking/error edge
case or its behavior on Cedar's target firmware. It is not executed in this task.

Real LLVM compiler-rt CPU detection is a separate Apache-2.0 WITH LLVM-exception
provider (docs/CPU-RUNTIME.md), not a fixed CPU-model constant. Real unwind
boundaries come from the GPL PSXS5 public ps5-pie-unwind.ld and its base script,
not dummy symbols. The session-link receipt captures the actual selected
providers, sections, arguments and hashes. All shared dependencies remain intact.

Future public distribution must preserve selected source, notices and applicable
license obligations. Nothing here authorizes proprietary game data distribution.
