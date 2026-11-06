# Original bounded PKF views

`include/cedar/pkf.hpp` and `src/pkf.cpp` implement allocation-free memory views,
using independent MIT-licensed Shenmue-Mods/ShenmueDK format evidence at exact
revision `1dae443814b3755281d207a13f25571c0e51a5d1`. MIT was read before source;
preserved notice: `third_party/licenses/shenmuedk-MIT.txt`, Copyright (c) 2019
Phil. No conflicting file-level license was present in reviewed files. No
restricted LemonHaze materials, recovered game logic or proprietary assets
were incorporated.

Primary source evidence:

- [PKF header](http<LOCAL_REFERENCE>/pkf.h): 16-byte PAKF header, content boundary, opaque unknown word and texture count.
- [PKF reader](http<LOCAL_REFERENCE>/pkf.cpp): optional initial DUMY, counted TEXN sequence, trailing IPAC at contentSize.
- [Node definition](http<LOCAL_REFERENCE>/node.h) and [reader/writer](http<LOCAL_REFERENCE>/node.cpp): eight-byte node header; size includes that header.
- [DUMY definition](http<LOCAL_REFERENCE>/dumy.cpp): DUMY signature and raw body.
- [MT5 dispatch](http<LOCAL_REFERENCE>/mt5.cpp): explicitly maps 0x4E584554 to TEXN; the PKF reader itself does not check each TEXN signature.
- [TEXN reader](http<LOCAL_REFERENCE>/texn.cpp) and [TextureID](http<LOCAL_REFERENCE>/texture_id.h): eight raw ID bytes followed by PVR-side body bytes.

## Contract and supported limits

- Caller supplies complete, immutable **plain** PAKF bytes. Gzip is neither
  inferred nor decoded here; the existing verified-gzip decoder can run first.
- Header/content boundary, optional first DUMY and declared TEXN nodes are
  validated before a view becomes usable. Unknown node signatures inside the
  counted texture sequence fail explicitly. All size checks precede arithmetic.
- TEXN must contain its header and eight-byte ID. Remaining bytes are exposed
  raw, including possibly empty bodies; successful PKF parsing does not prove
  PVR structural validity, texture decode or renderer readiness.
- One million declared textures is an implementation work limit. Linear cursor
  iteration avoids repeated index scans. Indexed lookup is intentionally O(n).
- Uncounted bytes within contentSize remain an explicitly opaque content
  remainder, not guessed padding or additional parsed nodes. DUMY body is raw.
- If bytes follow contentSize, they must begin with IPAC and pass the existing
  narrow `IpacIndex` parser; errors preserve its distinct status. No scanning
  for a magic string or guessed alternate offset occurs. Absence is explicit.
- The PKF unknown header word and TextureID are uninterpreted. No filename,
  hash, compression, pixel format, alignment or authentication rules are guessed.
- Caller storage must outlive the view, all cursors and returned spans. Failed
  parsing discards an earlier view. No writes, extraction, allocation or file
  access occur; no game bytes were inspected or copied during this batch.

No compilation, runtime/parser execution or tests were performed by this worker.
Lead owns build integration and compilation. This is an asset-container boundary,
not a claim of a native game boot or playable port.
