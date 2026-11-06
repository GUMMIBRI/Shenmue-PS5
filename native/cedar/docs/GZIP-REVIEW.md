# Gzip source review — November 5, 2026

Reviewed `include/cedar/gzip_decode.hpp`, `src/gzip_decode.cpp`, the payload header
parser, and pinned miniz `miniz_tinfl.h`/`miniz_tinfl.c` at
`293d4db1b7d0ffee9756d035b9ac6f7431ef8492` (3.0.2), against primary
[RFC 1952](http<LOCAL_REFERENCE>/rfc1952.html).

No actionable wrapper defect was established by this source-only review. No
code changes, decoder execution, test runs or game-data reads were performed.

- Input/output counters remain within supplied buffers, relying on the pinned
  low-level API's returned consumed/produced sizes. That implementation explicitly
  returns lookahead bytes before reporting completion (`miniz_tinfl.c:595–602`),
  so the wrapper can locate the actual trailer rather than a guessed final eight
  bytes. Missing trailers fail before either trailer word is read.
- Every concatenated member resets state and output history, checks its own
  header and trailer, and advances through the exact consumed byte count.
  Trailing nonmember bytes fail. Empty decoded members can use the non-null
  zero-capacity sentinel; the inflater can reach end-of-block without emitting
  output, while any actual output request returns an insufficient-space status.
- Optional header CRC checks the preceding header bytes; member CRC32 checks
  only that member's output. ISIZE comparison is exact within the imposed 64MiB
  output cap, which is below wraparound. Failed checks never increment the
  verified-member count. Produced bytes remain explicitly untrusted on failure.
- Input, output and workspace overlap checks use address differences without
  addition overflow. Static assertions constrain state size/alignment. Caller
  owns valid storage/lifetimes and exclusive workspace access. Input/output caps,
  header-scan bound and the 1,024-member cap bound resource use; no heap is used.

This does not independently prove all miniz internal malformed-stream behavior,
target linkage, console behavior, CRC interoperability or actual asset decoding.
No authenticity guarantee follows from CRC. Runtime qualification remains absent.

## Next implementation proposal, not yet implemented

An original bounded `PkfView` is supported by independently MIT-licensed
ShenmueDK revision `1dae443814b3755281d207a13f25571c0e51a5d1`:
[PKF reader](http<LOCAL_REFERENCE>/pkf.cpp),
[node header](http<LOCAL_REFERENCE>/node.h),
[node size semantics](http<LOCAL_REFERENCE>/node.cpp),
[eight-byte texture ID](http<LOCAL_REFERENCE>/texture_id.h),
[TEXN reader](http<LOCAL_REFERENCE>/texn.cpp).

After the existing gzip decoder succeeds, inspect the plain PAKF header, validate
its content boundary, traverse optional DUMY and declared TEXN nodes using
bounded node lengths, expose texture-ID/PVR byte views, and pass an optional
trailing IPAC view at `contentSize` to the existing `IpacIndex`. The same path
can accept already-plain PAKF bytes. This needs no filesystem extraction or
recovered game logic and would connect actual texture payloads toward the
native renderer. The compatible definitions establish an eight-byte texture ID
and node sizes inclusive of their eight-byte header. Lead owns the next
assignment/file map; no PKF code was added during this review.
