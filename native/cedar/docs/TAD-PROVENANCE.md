# TAD index parser provenance and supported scope

Implemented `include/cedar/tad.hpp` and `src/tad.cpp` as original allocation-free,
bounded C++17 memory-view code, using format layout from the independently
MIT-licensed **Shenmue-Mods/ShenmueDK** project. License was read before source.
Exact reviewed revision: `1dae443814b3755281d207a13f25571c0e51a5d1`.

Primary sources:

- [MIT license, Copyright (c) 2019 Phil](http<LOCAL_REFERENCE>/LICENSE)
- [TAD header/entry definitions](http<LOCAL_REFERENCE>/tad.h)
- [TAD reading/writing layout](http<LOCAL_REFERENCE>/tad.cpp)

The complete MIT notice is preserved in
`third_party/licenses/shenmuedk-MIT.txt`. No conflicting file-level license was
present in the reviewed TAD files. No other third-party dependencies were copied.
This work neither reads nor incorporates the restricted LemonHaze checkout.

## Format evidence

The compatible primary definitions describe fourteen 32-bit header words
(56 bytes), a duplicate 32-bit count, and eight-word entries (32 bytes).
The source reads entry offsets and sizes directly as byte ranges in the TAC.
It does not establish a reliable universal signature: its `_isValid` returns
false. This implementation therefore does not pretend to recognize every TAD.

Owner-authorized bounded read-only inspection on November 5, 2026 examined only
the first 60 header bytes and first 32-byte entry of each of these installed files:

- `<LOCAL_REFERENCE>/Program Files (x86)/Steam/steamapps/common/SMLaunch/sm1/archives/dx11/data/audio_eng_5be2c578.tad`
- `<LOCAL_REFERENCE>/Program Files (x86)/Steam/steamapps/common/SMLaunch/sm2/archives/dx11/data/audio_eng_5be2c4e2.tad`

Only 184 bytes total were read; no game files or raw header dumps were saved or
copied into this project. Directory metadata reported index lengths 23,804 and
28,668 bytes. Observed counts 742 and 894 respectively account for those exact
lengths using the compatible source layout. Both headers use little-endian
words with file type 1, identifier words 5 and 2, and render bytes `dx11`.
Observed reserved header words and the first entry's pad words were zero.
These observations constrain the supported profile; they are not full-archive
verification or gameplay qualification.

## Implemented behavior and limits

- Explicit little-endian decoding without host struct reinterpretation.
- Non-owning immutable input; no allocation, file access or extraction.
- Exact count duplication, complete index length and entry range checks.
- One million entries maximum bounds parse/lookup work; this is an implementation
  limit, not a claim that the format has that limit.
- Only the observed DX11 identifier profile and zero reserved words are supported.
  Nonzero reserved words, trailing extensions or other profiles fail explicitly.
  Larger-than-32-bit TAC layout variants are not silently truncated.
- Hash-pair lookup returns the first matching entry; it does not resolve names
  or assert that hash pairs are unique. Unknown entry words remain uninterpreted.
- Actual TAC length must match the header through `validate_tac_size` before the
  caller uses ranges. A public subtraction-based range check is also available.
- Header checksum is exposed as opaque metadata; integrity/authenticity is not
  verified. No compression decoding, container recursion, texture decoding,
  recovered game logic or executable boot is implemented.

No runtime tests or parser execution were performed in this work batch. Lead
owns compilation and integration. MIT grants rights to the referenced software,
not redistribution rights to SEGA's game data.
