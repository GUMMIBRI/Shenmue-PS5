# Gzip decoding scope

Original envelope/checksum wrapper follows
[RFC1952](http<LOCAL_REFERENCE>/rfc1952.html). Raw DEFLATE is supplied by
MIT miniz3.0.2 at293d4db1b7d0ffee9756d035b9ac6f7431ef8492, with unchanged
source and complete notices under third_party/miniz. Not recovered game code.

decode_gzip uses caller-owned aligned workspace/output and no heap allocation.
It accepts method8 members, optional fields, and concatenated members; checks
optional headerCRC, dataCRC32 and ISIZE; rejects trailing nonmembers. Compressed
input and total output are each capped64MiB, optional headers64KiB, members1024.
Input/output/workspace overlap is rejected. Failure may change output; counts
distinguish produced bytes from verified members. CRC detects corruption, not
authenticity. No filesystem extraction, names or inferred game compression.

This is a bounded implementation, not a universal unbounded gzip service. It does
not accept zlib streams, guessed raw DEFLATE or arbitrary proprietary envelopes.
Source review is in GZIP-REVIEW.md. Static compilation passed; no decoder execution,
fixtures, owned-asset decompression, sound/gameplay or console action occurred.
