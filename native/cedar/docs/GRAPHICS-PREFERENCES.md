# App-owned graphics preferences

`load_graphics_preferences` and `store_graphics_preferences` connect the validated
52-byte CGFX record to the existing transactional native SaveStore. The only slot
is `cedar-graphics-v1.cgfx` within an explicitly mounted, provisioned app-owned
directory. Neither function mounts, creates a directory, selects a game-save path,
imports PC settings, or changes live rendering.

Load failures leave full preserve-engine defaults. Missing files remain explicit
storage errors with native errno. Truncation, checksum/version/value failures are
record errors; oversized files fail the bounded storage read. Failed input is
never automatically replaced. Capability/ownership declarations are not saved:
they must be re-established against the actual current producer/device.

Store encodes and validates before any I/O. Existing files are refused unless the
caller explicitly supplies allow_replace. The underlying atomic commit, cleanup,
commit-uncertain and durability-uncertain reports are retained without rollback
or a false successful-save claim. This is preferences persistence, not original
Shenmue save compatibility. No runtime/file operations were exercised for QA.
