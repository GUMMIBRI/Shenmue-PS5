# Bounded AFS header/table index

`AfsIndex` adapts only `parse_afs` in brynnb/New Yokosuka's authored
[extract_dialogue_inventory.py](http<LOCAL_REFERENCE>/extract_dialogue_inventory.py),
pinned b0df7f2720a7eecabce2e610fdedec35bf1243e3. Header/implementation are
GPL-3.0-only; complete license: third_party/licenses/new-yokosuka-GPL3.txt.
No assets, generated dialogue corpus or recovered external examples are included.
This implements that authored format profile, not every AFS dialect.

The input is an immutable borrowed header/table beginning AFS+NUL, a LE32 member
count (at most one million), then count pairs of LE32 archive offset/length.
The caller supplies the actual archive extent from its real read-only source;
parse requires only the complete table in memory. Member extents are validated
against the archive, not the table buffer. Nonempty members overlapping metadata
are deliberately rejected more strictly than the reference. Zero-length members
retain their actual offsets. Overlapping payload extents are not relabeled or
silently deduplicated. Parse failures invalidate the view; accessor failures leave
outputs unchanged. Object/table/output aliasing is rejected where outputs mutate.

The reference places an optional 0x30-byte/member name directory at aligned
maximum member end (0x800 sectors). `directory_candidate` reports only an in-range
candidate: room for bytes is not evidence that a real directory exists. The caller
must establish/read the actual directory. `raw_name` takes exactly count*0x30
bytes of that independently read directory and returns a borrowed exact32-byte
name field, without decoding, ASCII replacement, trimming or invented filenames.
No archive pointer footer/dialect is guessed. Empty archives have no candidate.

This is an index, not a filesystem/extractor, subtitle decoder, voice player or
engine. There is no IO or allocation, no archive-type inference by guessed names,
no SRF/STR pairing, and no downloaded/copied proprietary input. Tables, directory
and archive handle must retain their caller-owned lifetimes. Native consumers can
use proven member extents with their read-only provider, then explicitly select
the corresponding decoder. No runtime or game-data decoding was exercised.

Exact extractor identity: 17186 bytes; Git blob
7029f4b7ec34cdf4cb8d8f12bc25a582dd589649; SHA256
526deac8f4a3cb8050dd3c4bda8787c7a344976a2c8dd89de16e66683747fa7f.
