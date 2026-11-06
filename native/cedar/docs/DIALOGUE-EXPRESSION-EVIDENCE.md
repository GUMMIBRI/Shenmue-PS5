# F0 dialogue expression evidence

Finite authored-source review at brynnb/new-yokosuka revision
`b0df7f2720a7eecabce2e610fdedec35bf1243e3`. Root GPL version 3 license was
previously read and is retained as
`third_party/licenses/new-yokosuka-GPL3.txt`. The two files below have no separate
file-level notice or conflicting license. A source-informed native component
must remain GPL-3.0-only with this provenance. No imported generated resource,
external C/reference material, game data or runtime was followed.

## Exact sources and functions

- [`play/dialogue/NativeDialogueSelector.js`](http<LOCAL_REFERENCE>/NativeDialogueSelector.js):
  `OPERATIONS`, `signExtendNativeDialogue`, `operand`,
  `parseNativeDialogueExpression`. Git blob
  `fb3992b58afea057b7fb68b529ff813ea21edb21`. UTF-8 source SHA256
  `720599cd90c1786b0afc65614f4f4a3e974a9c0a15c063cb90ab628049f44c51`.
- [`play/dialogue/NativeDialoguePredicate.js`](http<LOCAL_REFERENCE>/NativeDialoguePredicate.js):
  `calendarComponent`, `stateBankValue`, `spatialValue`, `operatorValue`,
  `evaluateNativeDialoguePredicate`, `nativeDialoguePredicateMatches`.
  Git blob `19589a8920b51cc964ce0a565e4cf61245f2e869`. UTF-8 source SHA256
  `db2c6f5cf384237a1c2d1734f54262b21ef6ab5cf5f8b0715d8fd74df4440655`.

Only these two authored source files were opened for this audit. The selector's
generated actor import and predicate's generated spatial-table import were not
opened. Resource loading, actor selection and corpus lookup are excluded.

## Concrete postfix grammar

Parsing begins immediately after caller-identified F0, not at the F0 byte.
Every token whose `token & 0xE0` differs from 0x80 is a two-byte operand.
Its ordinary value is `((token & 0x0F) << 8) | nextByte`; token bit 4 is not
included in that numeric field. The exact source grouping is retained rather
than inventing a signed or 13-bit value.

| Token group | Source operand |
|---|---|
| 00 | State-bank/native value type 2, unsigned12 index |
| 20 | State-bank/native value type 3, unsigned12 index |
| 40 | State-bank/native value type 4, unsigned12 index; index FFF is literal -1 |
| 60 | Literal unsigned12, native value type 1 |
| A0 | Runtime selector with following-byte mode; only token A0 is accepted |
| C0 | Actor-runtime identity, unsigned12 index |
| E0 | Spatial-result lookup, unsigned12 index |

Group 80 encodes one-byte operators using `token & 0x1F`:

| Opcode/token | Arity | Source operation | Evaluator proof |
|---|---|---|---|
| 0 / 80 | terminator | Return top stack element | Requires nonempty stack |
| 1 / 81 | 2 | assign | Unresolved; no assignment implementation |
| 2 / 82 | 1 | increment | Unresolved; no increment implementation |
| 3 / 83 | 1 | decrement | Unresolved; no decrement implementation |
| 4 / 84 | 1 | isZero | Numeric equality to zero yields 1/0 |
| 5 / 85 | 2 | booleanAnd | Numeric zero/nonzero truth rules |
| 6 / 86 | 2 | booleanOr | Numeric zero/nonzero truth rules |
| 7 / 87 | 2 | equal | Numeric equality yields 1/0 |
| 8 / 88 | 2 | notEqual | Numeric inequality yields 1/0 |
| 9 / 89 | 2 | greaterThan | Numeric comparison yields 1/0 |
| 10 / 8A | 2 | greaterThanOrEqual | Numeric comparison yields 1/0 |
| 11 / 8B | 2 | lessThan | Numeric comparison yields 1/0 |
| 12 / 8C | 2 | lessThanOrEqual | Numeric comparison yields 1/0 |

Other operator codes are unsupported. Unary and binary stack underflow,
truncated two-byte operands and missing terminator are explicit parse failures.
The reference terminator accepts multiple roots and returns only the top one;
a faithful parser can expose remaining stack count rather than silently change
that rule. It does not evaluate discarded roots. No F0 length prefix exists in
these functions: the ending cursor is the byte following the terminator.

An allocation-free native parser can use caller-owned AST-node and stack-index
arrays. Nodes are emitted in postfix order, so references point to earlier
nodes; this avoids recursive parse allocation and permits bounded evaluation.
Required node/stack limits and alias checks must be explicit. Parse failure
must not publish a valid tree/cursor. No existing body/machine file needs to
change until that API is agreed.

## Host-only evaluation contract and remaining boundaries

Literals need no provider. State-bank types 2,3,4 call `readStateBank(bank,index)`;
missing reader, null or undefined is unresolved. Runtime selectors 0–5 use UTC
calendar fields: year minus 1900, month 1–12, day of month, weekday Sunday=0,
hour, minute. Mode 6 reads yen. Remaining modes call `readRuntimeComponent(mode)`.
Actor values call `readActorRuntimeValue(index,currentActorCode)`. An isolated
native implementation can accept explicit typed numeric host responses for all
these operands; it must document that calendar modes/yen are already supplied
in those exact source units, without reading a system clock or guessing world
state. It must never substitute zero for absent input.

The reference spatial evaluation requires a generated one-based record table,
exact map identity, player X/Z and a strict squared-distance/radius test. That
table's coordinate conversion and contents are outside this review. A native
host-only evaluator must either return unsupported/unavailable for spatial
operands or request the final explicit numeric spatial result from a real
caller provider; the provider mapping is an unresolved integration obligation.
It must not fabricate locations, radii, actor IDs or a map conversion.

Operators 1–3 are named by the parser but not implemented by this evaluator.
They must remain explicitly unsupported/unresolved, not mutate host data or be
reinterpreted as arithmetic. Comparisons and isZero require resolved operands.
AND yields resolved zero if either resolved operand is zero; OR yields resolved
one if either resolved operand is nonzero. Otherwise unresolved inputs stay
unresolved. This is a source-proven truth result, not defaulting missing state
to false/true. Both children are evaluated by the source before these rules;
providers should therefore be read-only rather than side-effect callbacks.

The JavaScript helper converts resolved values with `Number(value)` and does
not consistently reject NaN/infinity. A native typed-number API should reject
nonfinite provider values explicitly instead of recreating JS coercion or
accidentally resolving a predicate through NaN comparisons. That stricter
supported profile must be documented. Finite double values match the source's
number/comparison domain without inventing integer wrapping.

## Result

A concrete small postfix parser and host-data-only evaluator for literal,
bank/runtime/actor/provider-spatial leaves and pure operations 4–12 is supported
by these two authored files. Assignment/increment/decrement, spatial record
binding, world-state providers and native story effects remain unproven or
external. This receipt approves no execution, resource admission or generated
story data. No decoder was implemented in this audit and no compilation,
tests, assets or runtime were used.
