# Bounded dialogue expression parser and pure evaluator

`parse_dialogue_expression` parses the source's postfix expression beginning at
an explicit caller offset immediately after F0. It emits nodes into caller
storage and uses a separate caller-owned `size_t` postfix stack. No allocation,
resource discovery, IO or generated story data is involved.

`evaluate_dialogue_expression` consumes a successful immutable view and a
caller-owned value array. Its optional read-only callback receives a typed
`DialogueOperandRequest` and returns `{resolved, value}`. The returned evaluation
has a separate error, resolved flag and numeric value. An error-free unresolved
result is not false; its numeric field is not meaningful.

## Exact provenance and license

The new header/source are GPL-3.0-only, source-informed by authored
brynnb/new-yokosuka revision `b0df7f2720a7eecabce2e610fdedec35bf1243e3`:

- [`NativeDialogueSelector.js`](http<LOCAL_REFERENCE>/NativeDialogueSelector.js),
  blob `fb3992b58afea057b7fb68b529ff813ea21edb21`, SHA256
  `720599cd90c1786b0afc65614f4f4a3e974a9c0a15c063cb90ab628049f44c51`:
  `OPERATIONS`, `operand`, `parseNativeDialogueExpression`.
- [`NativeDialoguePredicate.js`](http<LOCAL_REFERENCE>/NativeDialoguePredicate.js),
  blob `19589a8920b51cc964ce0a565e4cf61245f2e869`, SHA256
  `db2c6f5cf384237a1c2d1734f54262b21ef6ab5cf5f8b0715d8fd74df4440655`:
  `operatorValue`, `evaluateNativeDialoguePredicate` and operand helpers.

The root license was read before source; neither file has a conflicting
file-level notice. Retain `third_party/licenses/new-yokosuka-GPL3.txt` and
`DIALOGUE-EXPRESSION-EVIDENCE.md`. Imported generated tables, external SEGA C,
restricted sources and game payloads were not opened or incorporated.

## Parsing profile

Tokens outside group `token & 0xE0 == 0x80` consume exactly two bytes. Ordinary
indices/literals use `((token & 0x0F)<<8)|nextByte`, including the source's
intentional omission of bit 4. Groups 00/20/40 select banks 2/3/4. Bank4 index
FFF is the special literal -1. Group60 is an unsigned12 literal. Exact token A0
uses the next byte as runtime mode; other A-group tokens are unsupported.
Groups C0/E0 represent actor identity/spatial-result indices.

One-byte operators 81–8C correspond to assign, increment, decrement, isZero,
AND, OR, equal, notEqual, greaterThan, greaterThanOrEqual, lessThan and
lessThanOrEqual. Operators 2–4 are unary; all other operators are binary.
Token80 terminates and selects the top root. Like the reference, multiple roots
are accepted; `remaining_roots()` reports total roots still on the stack,
including the selected root. Discarded roots are not evaluated. `end_offset()`
is the byte after token80. Unknown operators, truncated operands, underflow,
empty termination and missing termination fail without an invented skip.

The strict native limits are 65,536 nodes, 131,072 tokens (including terminator)
and expression-tree depth 1,024. Caller capacities can impose smaller bounds.
The parser checks both node and stack capacities before their writes. It retains
every parsed node including discarded roots; a view borrows node storage only,
not the byte input or postfix-stack storage. Nodes carry child indices and
subtree/depth metadata; clients must not modify them after successful parsing.

## Host values and supported evaluation

Literal leaves resolve directly without callbacks. For other leaves the
provider receives kind, native value type and index/runtime mode. State-bank
requests preserve bank types 2–4; actor requests carry type5; spatial requests
carry type6. Actor context and any map/state binding belong to the supplied
callback context, with no generated actor table or name inferred here.

Runtime mode0 means UTC year minus1900, 1 month1–12, 2 day of month, 3 weekday
Sunday0, 4 hour, 5 minute; mode6 is yen. Other modes are caller runtime
components. The callback supplies those values from real host state. This
module reads no system clock and performs no calendar/world conversion.
Spatial requests require an actual caller-provided final result or remain
unresolved; the generated spatial table/radius/map mapping is not implemented.

Missing callback or `{resolved=false}` remains unresolved. Known values must
be finite double numbers; NaN/Inf is an explicit error rather than JS coercion
or false comparison. Unresolved numeric payloads are ignored. Provider calls
are read-only, in left-subtree/right-subtree order, and are not skipped merely
because a later boolean operation could determine its result. Providers must
not modify nodes, values, the view or other provider state used by this call.

Only pure operations4–12 are evaluated. isZero/comparisons yield exact 0/1 from
known finite operands. AND resolves0 if either known operand is zero; OR
resolves1 if either known operand is nonzero. Otherwise either missing operand
keeps the result unresolved. This preserves the source's determinate truth
rules without defaulting absent input to true/false.

Assignment/increment/decrement1–3 parse faithfully but a reachable occurrence
returns `unsupported_operation` before any provider call or value write. No
mutation/arithmetic semantics are guessed. Every reachable node is preflighted
before provider calls; selected-root evaluation is iterative, without recursive
stack growth. A discarded unsupported root does not block the selected root.

## Output, aliases and lifetime

Caller pointers must reference real appropriately aligned storage. Full
declared node/stack spans, input bytes and output-view object must be pairwise
disjoint. Multiplication/address overflow and overlap are checked before parser
scratch writes. Parse failure may clobber scratch nodes/stack but does not
publish or alter the output view. The caller must check the result before using
that view; an earlier valid view is not a newly parsed result. Reusing borrowed
node storage invalidates its earlier views even when a new parse fails.

Evaluation requires value capacity at least `node_count()`, including discarded
roots, although only selected-subtree slots are written. The full declared
value span must be disjoint from node storage and the view object. The caller
also keeps values disjoint from opaque callback/context storage, whose extent
cannot be inferred. Nodes/view and callback data stay valid and immutable
throughout evaluation. Value scratch can change on a later provider-value
error; no partially valid evaluation is returned. The error's `node_index`
locates a node-specific failure, otherwise it holds the sentinel. Parser errors
similarly identify a byte offset. Arbitrary pointer allocation validity cannot
be proved by these APIs.

No existing body/machine, build file or story state is modified. Yoko owns batch
integration and compilation. No runtime, tests, game data, event execution or
playability claims are part of this delivery.
