# Actual-file read-only AFS reader

`AfsReader` connects the existing GPL-licensed `AfsIndex` to
`PosixReadOnlyAssets`. It reads the table from the requested actual archive,
rather than assuming an independently supplied header belongs to that file.
There is no allocation, extraction, owned payload buffer, file write, type
inference, guessed voice/text pairing or filename generation. The new integration
carries GPL-3.0-only consistently with its AFS index dependency/provenance.

## Transactional open and caller storage

`open(assets,relative_path,table_storage,capacity)` first invalidates any previous
reader/index binding, then validates path/storage address ranges and aliases.
It copies the bounded relative path and uses only the existing strict read-only
provider's stat/read operations. That provider owns component/no-follow/path
validation; the reader does not introduce a less restrictive fallback.

The sequence is actual file stat, actual eight-byte local header read, signature/
count validation, required table calculation, actual complete header/table read
into caller storage, first-header consistency check, physical-size restat, and
`AfsIndex::parse` against that actual archive length. Only full success commits
the borrowed table/index/assets/path. Count is bounded by one million and the
actual complete table by **8,000,008 bytes**. Payloads need not fit that borrowed
header/table buffer; their extents must fit the actual archive.

Null storage with capacity zero is a supported size query. After a valid actual
header, `storage_too_small` exposes `required_table_bytes` without caller-buffer
writes. That number is header-derived storage demand, not proof that member
extents are valid: full-table reading/parsing still must succeed. Header or full
table/size changes during open return `archive_changed` without binding a view.
Every failed open/parse leaves reader/index invalid. Caller workspace may contain
a reported prefix or full invalid table after failure; discard it as a parsed
view until a complete successful open.

`bytes_transferred` means bytes actually copied into the call's caller buffer.
The preliminary local eight-byte header never falsely increments that prefix;
its actual successful/partial I/O count is available in nested
`assets_status.bytes_transferred`. A table read failure reports its exact
caller-storage prefix. A later stat/parse failure retains the already-written
table prefix count without claiming the table was accepted.

## Numeric members and current extents

`stat_member(index,output)` returns a numeric raw offset/size only after the
current physical archive size still matches the opened snapshot and the member
remains in bounds. `read_member(index,offset,buffer,length)` also bounds the
requested subrange inside that member, then performs actual read-only I/O.
Reads above the provider's 64 MiB per-call limit fail; callers may explicitly
chunk a larger member. Zero-byte reads permit null output but still validate a
real member, offset and current file extent. No zero-fill/fake success is used.

Observed physical-size changes latch `requires_reopen`; future metadata/reads
continue failing until explicit close/open, even if the size later returns to
its old value. `opened` becomes false and member_count returns zero while this
latch is set, but callers must still close/reopen before releasing the retained
table/assets borrow. Ordinary bad indices, buffer arguments, range errors or
transient provider failures do not destroy an otherwise valid reader.

All methods, including const reads, must be externally serialized because they
share this size-change latch. Retain table bytes immutable and the mounted
assets object alive/unmoved until close or the next open attempt. No worker,
descriptor, mount or file lifetime is owned by this reader.

## Explicit optional directory inspection

`directory_candidate` returns only the index profile's sector-aligned maximum
member-end candidate with its required span. In-bounds is **not** proof that an
AFS name directory exists. It does not automatically read or label names.

`read_directory_range` runs only when the caller explicitly supplies an
`AfsDirectory` span and subrange. The requested whole span must be exactly
member_count times 0x30 bytes, start outside the header/table, fit the actual
archive, and contain the requested subrange. Destination must not overlap the
directory descriptor. This returns raw bytes, not authenticated directory
metadata or normalized strings. No automatic candidate acceptance, printable
replacement, fallback member name or voice pairing occurs.

## Alias checks and limits

Before writes, destination spans are checked for address overflow and separation
from the reader, borrowed immutable table and mounted assets object. Open also
rejects table-storage overlap with the input path bytes and assets object.
Metadata outputs are cleared on ordinary failure after safe alias validation;
invalid overlapping output references are left untouched to avoid corrupting
the object/table being protected. Read failures may modify exactly the prefix
reported in the status. Caller pointers still must describe real allocated
storage; integer checks cannot establish allocation provenance.

Each provider stat/read reopens the path. Header comparison and size checks
catch some observable changes but establish no stable-file identity, lock,
cryptographic authentication or protection against same-length replacements
between operations. Existing root/provider limits for hard links, mount changes
and privileged actors remain. Successful open proves the table bytes were
actually read and structurally checked at that time, not that later payloads
are authentic or unchanged.

## Delivery boundary

Only `include/cedar/afs_reader.hpp`, `src/afs_reader.cpp` and this contract were
authored. The lead owns source review, CMake integration and batch compilation/
linkage. This worker performed no runtime/fixture test, archive/asset content
read, extraction, compilation, game launch, console action or shared-file edit.
