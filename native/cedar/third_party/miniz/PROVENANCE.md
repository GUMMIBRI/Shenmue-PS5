# miniz inflate-only dependency

Upstream richgel999/miniz tag3.0.2, exact commit
293d4db1b7d0ffee9756d035b9ac6f7431ef8492.
Source: http<LOCAL_REFERENCE>/293d4db1b7d0ffee9756d035b9ac6f7431ef8492

LICENSE was read before source. Its MIT permission grant and notices are retained
unchanged. Downloaded only LICENSE, miniz.h, miniz_common.h, miniz_tinfl.h,
miniz_tinfl.c and required miniz_tdef.h/miniz_zip.h headers. No upstream ZIP or
compression implementation is compiled. Upstream files are unmodified.
miniz_export.h is a clearly marked original local static-build configuration.

Compile flags disable malloc, stdio/time, deflate, ZIP and zlib-wrapper APIs.
Only the allocation-free low-level inflate implementation is used. This does not
establish game-specific compression; the Cedar gzip wrapper requires RFC1952
headers and validates CRC32/ISIZE before reporting success. No game data included.
Future source packaging must retain this directory's notices.
