# open_memstream compatibility boundary — blocked

The captured session-link dependency is real: selected public RADV util/debug
members import `open_memstream`, and strict linkage reports it unresolved.
This investigation did not implement a NULL-return success placeholder or
invent/copy any foreign `FILE` layout. No compatibility header/source was added:
there is no established native callback provider on which to build one safely.

## Actual public header contract

The existing SDK
`<LOCAL_REFERENCE>/stdio.h`
declares all of the following:

- `open_memstream(char **, size_t *)` as a POSIX interface.
- `funopen` with cookie, integer-length read/write callbacks, `fpos_t` seek
  callback and integer close callback.
- `fopencookie` with the header's `cookie_io_functions_t` read/write/seek/close
  callback types.

These are actual header declarations, not proof that the selected console C
runtime exports compatible functions. This distinction is the blocker.

## Bounded provider inspection

Read-only inspection used the existing LLVM `llvm-readelf --dyn-syms` for the
actual dynamic stub and `llvm-nm --print-file-name --defined-only` for the exact
static archives below. It searched the exact function names rather than relying
on library existence or similarly named underscored functions.

| Actual input | Findings |
| --- | --- |
| Selected SDK `target/lib/libSceLibcInternal.so` | Exports `malloc`, `calloc`, `realloc`, `free`, `fclose`, `fflush`, `fseek`, `fseeko`, `ftello`, `setvbuf`, `fdopen`; does **not** export `funopen`, `fopencookie`, `fmemopen`, or `open_memstream` |
| Selected SDK `target/lib/libc.a` | No defined `funopen`, `fopencookie`, `fmemopen`, or `open_memstream` |
| Existing public `deps/radv-sdk/ps5-payload-sdk/target/lib/libc.a` | No defined function among that callback/memory-stream set |
| Existing public `deps/radv-sdk/ps5-payload-sdk/target/lib/libps5platform.a` | No defined function among that callback/memory-stream set |

The selected baseline and alternate public inputs retain their existing
upstream licenses/notices. No third-party implementation was copied, so no new
reuse-license claim or imported implementation notice is asserted. This was a
bounded inspection of these providers, not a claim that no compatible public
implementation exists anywhere.

## Why source implementation stopped

A growable memory buffer alone cannot satisfy POSIX `open_memstream`: it must
participate in the exact selected runtime's `FILE` operations, publish buffer
and length at flush/close boundaries, handle seeking and zero-filled gaps,
report allocation/overflow errors, and transfer final allocation ownership
correctly. The declared callback APIs would offer that integration, but an
implementation calling an unprovided `funopen`/`fopencookie` would merely replace
one unresolved dependency with another.

`fdopen`, `setvbuf` and ordinary I/O exports do not establish supported custom
stream callbacks. No temporary disk-file imitation, guessed `FILE` internals,
private PE wrapper, patched SDK, pretend successful flush or stubbed missing
symbol was substituted. Importing only FreeBSD/musl memory-stream code would
not establish ABI compatibility with this console runtime's actual stdio.

## Required next input

Select a genuinely licensed callback provider proven compatible with the actual
selected PS5 stdio runtime, or an already reviewed compatible full memory-stream
provider with exact provenance. Cinder is checking existing compatible platform
work to avoid duplicating prior fixes. Only after that evidence exists should
the Cedar shim implement and review the real buffer/seek/publication/error
contract. A broader C-runtime replacement is a separate structural integration
decision, not authorized by a header declaration alone.

No compilation, new linkage, native stdio execution, fixture, game/runtime,
thread launch or console action was performed for this compatibility inquiry.
The existing failed session-link receipts remain unchanged.
