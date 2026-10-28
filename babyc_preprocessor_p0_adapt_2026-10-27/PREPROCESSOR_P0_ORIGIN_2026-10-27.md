# P0 origin is truth - 2026-10-27

Apply on new zero (6-part zip SHA-256
`c2d0f9007b423ea6a399c7773d4f3e34eade76d6cf9beab4587ab0277d95b61b`).
Complete files, not diffs. Linear is not promoted.

## What changed

`LineTab` is `int`, not `unsigned short`. SQLite amalgamation is ~271k
lines; 65535 saturation is gone.

`LexGetRawToken` sets `Parser->Line` from `TokenLineTab[TokenIndex]` when
the table is present. Copied spans (`LexCopyTokens` function bodies /
macros) keep `TokenLineTab == NULL` and still count EOL.

`PrintSourceTextErrorLine` no longer walks back up to 8 empty lines.
The reported line is the token origin.

`(short int)` casts of `Parser->Line` in expression restore, `ir_mem`,
`ir_vm` are gone. Column assignment in `diagnostic.c` is `int`.

## Files

- `babyc/src/interpreter.h` - `TokenLineTab` / `LexLineTab` are `int *`
- `babyc/src/lex.c` - store unsaturating lines; token-driven `Parser->Line`
- `babyc/src/parse.c` - `OldLineTab` / `NewLineTab` are `int *`
- `babyc/src/expression.c` - `ExpressionRestoreLastTokenLine` uses `int`
- `babyc/src/platform.c` - drop empty-line walk-back
- `babyc/src/diagnostic.c` - `CharacterPos` assignment is `int`
- `babyc/src/ir_mem.c` - no `(short int)` on `Parser->Line`
- `babyc/src/ir_vm.c` - no `(short int)` on `Parser->Line`

## Not in this overlay

- No expansion behaviour (P1).
- No `TypeSkipEmptyObjectMacro` deletion.
- No Linear default.
- Serialized token header (kind + column byte) is unchanged. Linear
  packing of `SourceLine` into 16 bits in `ir_lin_emit.c` is untouched.

## Tests

- `make -C src babyc`
- `src/tests/001_assignment.c` exit 0
- `src/tests/lexer_architecture/*.c` (18 files) all exit 0
- numbered matrix `tree --accel 0`: PASSED=1222 FAILED=2
  (`047_grep.c`, `061_local_vars.c`) - same two failures on unmodified
  baseline binary
- `src/third_party/sqlite/sqlite3.c` (3.54.0) exit 0
- `/tmp/sqlite3_windows.c` (3.54.1) exit 0
- Linear smoke: `BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0
  BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1` on
  `001_assignment.c` exit 0

## Still forbidden

Do not re-lex expansion as source text. Do not add per-idiom parser
exceptions. Do not promote Linear.
