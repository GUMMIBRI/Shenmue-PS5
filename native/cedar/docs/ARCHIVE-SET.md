# Explicit-priority raw archive set

`ArchiveSet` composes already-open `TacReader` objects in an application-supplied
order. It performs no directory scan, archive discovery, file-name hashing,
Steam/patch precedence inference, decompression, game loading or extraction.
It contains original composition logic; its existing TAD reader dependency
retains the separate documented MIT-licensed layout provenance.

## Borrowed configuration

Pass a nonnull pointer array containing 1–32 distinct nonnull opened readers to
`bind`. Position zero has highest priority. A bind attempt clears any previous
binding before checking the entire new list. Failure therefore cannot silently
leave an older content list selected. A successful bind borrows both the pointer
array and its readers; it does not allocate/copy/own any reader or asset bytes.

The array and all pointers must remain immutable and alive until `unbind` or
destruction. Readers, backing TAD buffers and mounted POSIX asset objects must
also remain alive, immutable/unmoved, and mounted according to their own lifetime
contracts. Synchronize all calls against configuration/reader/storage changes.
The class cannot make a freed pointer safe. Bind validates `opened()` state;
it does not rescan/reopen every TAC or claim immutable external files.

## Selection and errors

`stat_hash(h1,h2,entry)` clears its output and asks each reader's validated
metadata API in supplied order. Only the exact nested combination
`TacReaderError::index_failure` and `TadError::not_found` advances to the next
reader. The first successful lookup wins and exposes its zero-based
`archive_index` plus the entry. An earlier I/O, malformed-index, unsupported,
not-open, TAC-size, range or other error stops lookup immediately. An inaccessible
patch therefore does not silently reveal older/lower-priority content.

If every reader reports the exact missing-key result, the set reports explicit
`ArchiveSetError::not_found`, with no selected index. A failure attributed to a
reader reports `reader_failure`, its priority index, and the complete nested
`TacReaderStatus`, including index/asset failure details and transferred bytes.
Configuration errors expose the offending list index when one exists.
`ArchiveSetStatus::no_archive` is the sentinel otherwise.

Bind success has no selected archive, and missing-key/config/preflight errors
have no fabricated native I/O failure. The nested status is meaningful for a
reader-attributed failure/success; inspect the outer error first. The selected
reader's new `stat_hash` API must validate the current physical TAC length and
entry range, matching the reader's raw-read checks. This class never substitutes
unvalidated index-only metadata for that API.

## Raw subrange access

`read_hash` rejects unbound use, nonzero reads into null buffers, and reads above
the existing 64 MiB backend cap. It resolves once using the validated metadata
operation, then asks only the selected reader to read the requested raw stored
entry subrange. That reader rechecks current TAC/range state. A read error or
newly missing key after selection fails at the selected index, without trying
any lower-priority archive. Partial failures preserve the accurately reported
caller-buffer prefix length; there is no zero-fill/fake success.

A zero-length read permits a null buffer but still requires a real matching
entry and valid offset according to the selected reader. Offsets/ranges are
validated by the reader; no adjacent-entry borrowing or output-size guessing is
introduced here. Stored bytes may be compressed. Selection/size checks do not
authenticate a TAD/TAC pair or protect against externally changed same-size files
between stat and read. The external-file stability limitations remain explicit.

## Integration

Compile `src/archive_set.cpp` against the existing TAD/TAC reader target after
the lead adds `TacReader::stat_hash(uint32_t,uint32_t,TadEntry&) const noexcept`.
No existing files or CMake target were changed by this implementation worker.
No runtime, fixture, extraction, compilation or console operation was performed.
