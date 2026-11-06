# Native bounded dialogue-body command decoder

`decode_dialogue_body` decodes one explicitly supported instruction from a
caller-selected immutable routing-byte slice. It does not execute that
instruction, select an actor, evaluate a predicate, load a message, advance
caller state or implement native event behavior. This is a script command
decoder, not a CPU emulator or a claim of original-story implementation.

## License and source receipt

`include/cedar/dialogue_body.hpp` and `src/dialogue_body.cpp` are GPL-3.0-only,
source-informed by authored brynnb/new-yokosuka
[`play/dialogue/NativeDialogueBody.js`](http<LOCAL_REFERENCE>/NativeDialogueBody.js),
revision `b0df7f2720a7eecabce2e610fdedec35bf1243e3`, blob
`5824982fe8537f7273984b1ec076c034eddaa463`, UTF-8 source SHA256
`cc41375858f6ec9a52e53cd9a3986d980f191d6865286812fe23c8796dc395c1`.
Root license was read before source; neither reviewed authored file carries a
conflicting file-level notice. Retain
`third_party/licenses/new-yokosuka-GPL3.txt`. Exact source functions and the
two-file inspection boundary are recorded in `DIALOGUE-ROUTING-EVIDENCE.md`.
No generated resource/corpus, external SEGA example or restricted engine source
was read or incorporated.

## Supported instruction forms

`?` means any low nibble unless restricted below. Classes are mode-dependent.

| Mode | Opcodes | Descriptor |
|---|---|---|
| Both | 4?, 5? | Conditional-if-zero / unconditional signed12 relative branch |
| Both | D? | Signed20 relative branch |
| Both | C1–C7 | Source-established runtime field write request |
| Outer | 0? | Two-byte dispatch no-op with raw operand |
| Outer | 1? | Progress redirect request |
| Outer | 2? | Message-group entry, not yet a presented message |
| Outer | 3? | Invoke/yield request; encoded operand 0x10 is a no-op |
| Outer | 6?, 7? | State-bank 2 write request, value 1 / 0 |
| Outer | 8? | Signed12 branch with saved following-offset continuation |
| Outer | 9? | Progress-record request |
| Outer | E1 | Eleven-byte opaque native event; raw byte and four signed16 arguments |
| Outer | E2 | Two-byte opaque native event |
| Outer | F2 | Four-byte opaque state5 event |
| Outer | F5 | Save signed24 relative continuation |
| Outer | F9 | Continuation request; no encoded/resolved target |
| Outer | FC | State6 event |
| Outer | F3, F6, F7, F8, FF | Completion descriptor |
| Outer | F1, F4, FA, FB, FD, FE | One-byte dispatch no-op |
| Group | 2? | Message index |
| Group | 3?, 6? | Opaque native command, source tick-advance marker 1 / 0 |
| Group | 9? | Progress marker |
| Group | FC | Opaque command word 0xFC00, zero tick-advance marker |
| Group | F6, F7, F8 | Pending message flag bits 4 descriptor |
| Group | F1, FD | One-byte dispatch no-op |

Two-byte class operands are big-endian low-nibble12 values. D-class uses
low-nibble20; F5 uses the following three bytes as big-endian24. Displacements
are explicitly sign-extended without implementation-defined signed casts.
Targets are relative to the byte immediately following the full instruction.
All targets, including not-taken conditional targets and saved continuations,
must point inside the supplied slice; one-past-end is not an executable target.
`following_offset` may equal slice size for a nonbranch final instruction; this
does not establish that continued execution is valid.

Runtime C1/C2 request manager offsets 0x10/0x11. C3–C6 request person offsets
0x0B–0x0E; C7 requests signed-byte person offset 0x10. Both raw byte and mapped
value are retained. The generic `operand` also retains the two-byte encoded
word; field-specific interpretation uses the separate explicit fields.
E1 arguments are big-endian signed16 at instruction offsets 3,5,7,9; its opaque
byte is offset 2. Other event payloads remain accessible through the original
slice using `offset` and `byte_length` without invented argument meanings.

F0 expressions, E0 random blocks, variable/ambiguous event forms, unsupported
runtime selectors, group-return boundaries and all other opcodes fail with
`unsupported_opcode`. No unsupported instruction has an invented skip length.
The decoder does not accumulate pending flags, apply the source's command-word
flag rules, resolve continuations, mutate state banks or dispatch events. The
caller must implement those separately from proven semantics. Tick markers are
raw interpreter descriptors, not seconds, sample durations or a playback clock.

## Bounds and output validity

Input must be valid caller-owned storage; `offset` and all returned offsets are
relative to that exact slice, not absolute record/file positions. Input remains
immutable during decoding. The output object must be disjoint from the entire
input span. Address-overflow and overlap checks precede reading an opcode.
All operand bytes and relative targets are checked before committing the local
instruction to output. Every failure leaves output unchanged; the result error
must be checked before consuming it. Stale output from an earlier successful
call is not a newly decoded instruction. No caller cursor is accepted or changed.
Arbitrary pointer allocation validity remains the caller's responsibility.

Descriptors contain only values and offsets, with no retained input pointer or
allocation. To inspect opaque payload bytes later, the caller retains the same
immutable slice. A future executor needs its own explicit step budget, loop
handling, host acknowledgements and lifecycle policy. Unknown commands must not
be treated as successful progress.

No files, assets, GPU, runtime or tests were accessed by this implementation.
Yoko owns batch integration/compilation; this receipt establishes source scope,
not successful game execution or the presence of this profile in Steam archives.
