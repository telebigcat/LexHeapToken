# P0 origin: adapt to another baseline - 2026-10-27

This document is the portable form of P0. It does not depend on line
numbers of the 27 Oct zero. Apply the eight edits by matching the
**before** snippets. If a snippet is missing, skip that hunk and keep
searching: another baseline may have already done it, or the nearby
code was rewritten.

Companion overlay of complete files (same edits, this zero):
`babyc_preprocessor_p0_origin_2026-10-27.zip`
SHA-256 `72e69bcae4ba80c76303c1d65bfae364626feb5c83d44d1dc16147e7a214d2e5`

Zero this P0 was built on (6-part zip):
SHA-256 `c2d0f9007b423ea6a399c7773d4f3e34eade76d6cf9beab4587ab0277d95b61b`

Linear is not promoted. Token header (kind + 1-byte column) is unchanged.
Do not retouch `.expect` files. Do not start P1 (object-like expansion)
in the same overlay.

Unified diffs live next to this file in `patches/`.

---

## Intent (why these eight files)

`LineTab` was `unsigned short` and saturated at 65535. SQLite amalgam
is ~271k lines. Diagnostics used `Parser->Line++` on every skipped EOL,
then `PrintSourceTextErrorLine` walked back up to 8 empty lines, so the
caret could land on a comment (`** Do both phases of a commit.`).

P0 makes the token table the source of truth for `Parser->Line` on the
**file** stream. Copied spans (`LexCopyTokens` function bodies, macro
bodies) keep `TokenLineTab == NULL` and still count EOL. That split is
load-bearing: if you attach the file table to a copied buffer, loop
bodies and function replay mix two streams (seen as `045` / `061`
caret drift when tried).

After P0, `Parser->Line` is `int`. Stop stuffing it into `short`.

---

## Grep inventory (confirm the other baseline still has these)

Run these before editing. If a pattern is absent, that hunk is already
done or the name moved.

```
TokenLineTab
LexLineTab
unsigned short *LineTab
Lexer->Line > 65535
Parser->Line++
walk back to
the nearest non-empty line
Parser->Line = (short int)
Parser->CharacterPos = (short int)
unsigned short *OldLineTab
```

Expected on an unpatched tree that still has the 65535 design:

| Pattern | File | Meaning |
|---|---|---|
| `const unsigned short *TokenLineTab` | `interpreter.h` `ParseState` | widen |
| `unsigned short *LexLineTab` | `interpreter.h` `Picoc` | widen |
| `unsigned short *LineTab` | `lex.c` `LexTokenise` | widen |
| `Lexer->Line > 65535 ? 65535` | `lex.c` `LexTokenise` | stop saturating |
| `Parser->Line++` inside EOL skip | `lex.c` `LexGetRawToken` | only if no table |
| `LastTokenIndex = Parser->TokenIndex` then `LastTokenLine = Parser->Line` | `lex.c` `LexGetRawToken` | insert table read between them |
| `unsigned short *OldLineTab` | `parse.c` `ParseExpandDeclarationMacro` | widen |
| `unsigned short Line = Parser->TokenLineTab[...]` | `expression.c` `ExpressionRestoreLastTokenLine` | widen + drop short cast |
| walk-back `back < 8` empty lines | `platform.c` `PrintSourceTextErrorLine` | delete the block |
| `Parser->CharacterPos = (short int)(target - line` | `diagnostic.c` | `int` cast |
| `Parser->Line = (short int)Index->SourceLine` | `ir_mem.c` | drop cast |
| `Parser->Line = (short int)line` | `ir_vm.c` | drop cast |

Function bodies already set `TokenLineTab = NULL` after `LexCopyTokens`.
Do **not** change that. Do **not** attach `TokenLineTab` to
`LoopRunBody` copied `BodyTokens`.

---

## File 1: `src/interpreter.h`

Two type fields. Nothing else.

### 1a. `ParseState.TokenLineTab`

Anchor: field sits between `DebugMode` and `TokenLineCount`.

Before:

```c
    char DebugMode;
    const unsigned short *TokenLineTab;
    int TokenLineCount;
```

After:

```c
    char DebugMode;
    const int *TokenLineTab;
    int TokenLineCount;
```

If the other baseline already has `const int *TokenLineTab`, skip.

### 1b. `Picoc.LexLineTab`

Anchor: next to `LexTokeniseLineScratch` / `LexLineCount`.

Before:

```c
    void *LexTokeniseLineScratch;
    unsigned short *LexLineTab;
    int LexLineCount;
```

After:

```c
    void *LexTokeniseLineScratch;
    int *LexLineTab;
    int LexLineCount;
```

`Parser->Line` and `struct LexState.Line` are already `int` on this
zero. If another baseline still has `short int Line` on `ParseState`,
widen **that** too or P0 is a no-op for amalgamations.

Do not widen `Value.SourceLine` (`short int`) in this phase. IR/Linear
still pack it into 16 bits in places (`ir_lin_emit.c`). That is P0+
and a different overlay.

---

## File 2: `src/lex.c` (the real change)

Five coordinated sites inside `LexTokenise` and `LexGetRawToken`.
Do all of them or none: a mixed `unsigned short` table with `int *`
pointers is undefined.

### 2a. Scratch type in `LexTokenise`

Before:

```c
    int LineCount = 0;
    unsigned short *LineTab;
```

After:

```c
    int LineCount = 0;
    int *LineTab;
```

### 2b. Alloc size and overflow guard

Before:

```c
    if (LineCap > INT_MAX / (int)sizeof(unsigned short))
        LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
    LineTab = (unsigned short *)HeapAllocMem(pc, LineCap * (int)sizeof(unsigned short));
```

After:

```c
    if (LineCap > INT_MAX / (int)sizeof(int))
        LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
    LineTab = (int *)HeapAllocMem(pc, LineCap * (int)sizeof(int));
```

### 2c. Store the real line (no 65535 clamp)

Before:

```c
        LineTab[LineCount++] = (unsigned short)(Lexer->Line > 65535 ? 65535 : Lexer->Line);
```

After:

```c
        LineTab[LineCount++] = Lexer->Line;
```

If the other baseline writes `Lexer->Line` already without the clamp,
keep it; just make sure the element type is `int`.

### 2d. Persist next to the token buffer

The layout is still: token bytes, 0-3 pad bytes, `unsigned int` count,
then `LineCount` line numbers. Only the **element size** of the table
changes (`2` -> `4` on LP64). Nothing in Linear reads this sidecar;
the serialized token header is untouched.

Before:

```c
        if (LineCount > (INT_MAX - (int)sizeof(unsigned int)) / (int)sizeof(unsigned short))
            LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
        Extra = (int)sizeof(unsigned int) + LineCount * (int)sizeof(unsigned short);
```

and later:

```c
            memcpy(p + sizeof(unsigned int), LineTab, (size_t)LineCount * sizeof(unsigned short));
            pc->LexLineTab = (unsigned short *)(p + sizeof(unsigned int));
```

After: replace every `sizeof(unsigned short)` in those three lines with
`sizeof(int)`, and the cast with `(int *)`.

### 2e. `LexGetRawToken`: do not `Line++` when the table owns the line

Anchor: the `while` that skips `TokenEndOfLine` when
`!Parser->InHashIfExpression`.

Before:

```c
                Parser->Line++;
                Parser->Pos += TOKEN_DATA_OFFSET;
                if (Parser->TokenLineTab != NULL)
                    Parser->TokenIndex++;
```

After:

```c
                if (Parser->TokenLineTab == NULL)
                    Parser->Line++;
                Parser->Pos += TOKEN_DATA_OFFSET;
                if (Parser->TokenLineTab != NULL)
                    Parser->TokenIndex++;
```

Copied bodies have `TokenLineTab == NULL`, so they still increment.
File parse uses the table, so they must **not** increment or you get
+1 drift on every blank.

### 2f. `LexGetRawToken`: copy table line onto `Parser->Line`

Insert **after** `LastTokenIndex = Parser->TokenIndex` and **before**
`LastTokenLine = Parser->Line`.

Before:

```c
    Parser->LastTokenStartPos = Parser->Pos;
    Parser->LastTokenIndex = Parser->TokenIndex;
    Parser->LastTokenLine = Parser->Line;
    Parser->LastTokenColumn = *((unsigned char *)Parser->Pos + 1);
    Parser->CharacterPos = *((unsigned char *)Parser->Pos + 1);
```

After:

```c
    Parser->LastTokenStartPos = Parser->Pos;
    Parser->LastTokenIndex = Parser->TokenIndex;
    if (Parser->TokenLineTab != NULL && Parser->TokenIndex >= 0
        && Parser->TokenIndex < Parser->TokenLineCount)
        Parser->Line = Parser->TokenLineTab[Parser->TokenIndex];
    Parser->LastTokenLine = Parser->Line;
    Parser->LastTokenColumn = *((unsigned char *)Parser->Pos + 1);
    Parser->CharacterPos = *((unsigned char *)Parser->Pos + 1);
```

Do not seed `Parser->Line` from `LineTab[0]` in `LexInitParser`.
That caused +1 drift historically; leave `Parser->Line = 1` there.

Update the nearby comment that says "Line stays classic (EOL
increments)" so the next reader does not revert 2e/2f.

---

## File 3: `src/parse.c`

One type. `ParseExpandDeclarationMacro` saves/restores `pc->LexLineTab`
around a nested `LexAnalyse`. Pointer type must match `Picoc.LexLineTab`.

Before:

```c
    unsigned short *OldLineTab, *NewLineTab;
```

After:

```c
    int *OldLineTab, *NewLineTab;
```

Anchor: locals of `ParseExpandDeclarationMacro`, next to
`OldTokenBase` / `NewTokenBase`.

Do not give `FuncDef.Body` or `MacroDef.Body` a `TokenLineTab`. They
must stay NULL after `LexCopyTokens` (already true on this zero).

---

## File 4: `src/expression.c`

`ExpressionRestoreLastTokenLine` reads the table. Element type and
assignment must follow `Parser->Line` being `int`.

Before:

```c
        unsigned short Line = Parser->TokenLineTab[Parser->TokenIndex - 1];
        if (Line > 0 && ExpressionSourceLineHasChar(Parser, Line, PreferCh))
            Parser->Line = (short int)Line;
```

After:

```c
        int Line = Parser->TokenLineTab[Parser->TokenIndex - 1];
        if (Line > 0 && ExpressionSourceLineHasChar(Parser, Line, PreferCh))
            Parser->Line = Line;
```

If the helper was renamed, grep `TokenLineTab[Parser->TokenIndex`.

Leave `Value.SourceLine = (short)OriginLine` alone in this overlay.

---

## File 5: `src/platform.c`

Delete the empty-line walk-back inside `PrintSourceTextErrorLine`.
It exists only to hide a stale `Parser->Line`. After 2e/2f the line
is the token origin; walking back reintroduces the SQLite false caret.

Anchor: immediately after skipping a leading `\r`, immediately before
the loop that prints the source line.

Delete this whole block (and only this block):

```c
        /* If the reported line is empty/whitespace-only (skewed Line from a
         * past lexer bug, or error landed between statements), walk back to
         * the nearest non-empty line so the caret is not orphaned. */
        {
            const char *Try = LinePos;
            int back = 0;
            while (Try > SourceText && back < 8)
            {
                const char *S;
                int empty = 1;
                for (S = Try; *S != '\n' && *S != '\r' && *S != '\0'; S++)
                {
                    if (*S != ' ' && *S != '\t')
                    { empty = 0; break; }
                }
                if (!empty)
                    break;
                /* step back over previous line ending */
                if (Try > SourceText && *(Try-1) == '\n')
                    Try--;
                if (Try > SourceText && *(Try-1) == '\r')
                    Try--;
                while (Try > SourceText && *(Try-1) != '\n')
                    Try--;
                back++;
                LinePos = Try;
            }
        }
```

Keep the `\r` skip. Keep the print loop. Keep basename printing.
If the other baseline never had this walk-back, skip the file.

---

## File 6: `src/diagnostic.c`

Cosmetic consistency: `CharacterPos` is `int`.

Before:

```c
        Parser->CharacterPos = (short int)(target - line + (IndexDepth <= 0 ? 1 : 1));
```

After:

```c
        Parser->CharacterPos = (int)(target - line + (IndexDepth <= 0 ? 1 : 1));
```

Function: `DiagnosticSetIndexAccessLocation` (or whatever still
assigns `CharacterPos` from a pointer delta with a `short int` cast).

---

## File 7: `src/ir_mem.c`

Before:

```c
            if (Index->SourceLine > 0) Parser->Line = (short int)Index->SourceLine;
```

After:

```c
            if (Index->SourceLine > 0) Parser->Line = Index->SourceLine;
```

Nearby comment talks about preserving the offending dimension's source
location. Implicit conversion `short` -> `int` is fine; the cast to
`short int` was the bug for lines > 32767.

---

## File 8: `src/ir_vm.c`

Before:

```c
                    Parser->Line = (short int)line;
```

After:

```c
                    Parser->Line = line;
```

`line` is already `int` (from `indices[i]->SourceLine` / `member->SourceLine`).
Same bounds issue as file 7.

If the other baseline has more `(short int)Parser->Line` or
`Parser->Line = (short int)...` assignments, drop those casts too.
Do not hunt-rewrite `Value.SourceLine = (short)...`.

---

## What you must not do while adapting

1. Do not point `LoopRunBody` / `ParseCopyTokenSpan` parsers at the
   file `TokenLineTab`. Copied token bytes are a different stream;
   `TokenIndex` 0 is not file token 0. Tried, broke `045_scoped_declarations`.
2. Do not give `LexCopyTokens` bodies a sliced `LineTab`. P0 leaves
   them NULL on purpose (`parse.c` already does this after copy).
3. Do not change the 2-byte token header. Linear A/B is then not a
   required gate for this overlay.
4. Do not delete `TypeSkipEmptyObjectMacro` or empty-prefix skips.
   That is P1.
5. Do not promote Linear.

---

## Compile check after each file

After `interpreter.h` + `lex.c` + `parse.c` the project must compile.
The rest are type/cast/diag only.

```
make -C src babyc
```

GCC `-Wall -O2`, existing `CFLAGS`. Expect the usual `ir_mem.c`
array-bounds and `stdlib.c` realloc warnings; they are pre-existing.

---

## Gate (same as this zero)

Always:

- `./src/babyc src/tests/001_assignment.c` exit 0
- all `src/tests/lexer_architecture/*.c` exit 0 (18 files here)
- numbered matrix: `python3 tools/run_numbered_matrix.py --path tree --accel 0`
- `./src/babyc src/third_party/sqlite/sqlite3.c`
- Windows amalgam 3.54.1 if you have it

On **this** zero the numbered matrix is PASSED=1222 FAILED=2
(`047_grep.c`, `061_local_vars.c`). Those two already fail on the
unmodified baseline binary. If your other baseline currently passes
them, they must still pass after P0. If they already fail, they must
not get worse (same diagnostic text is enough; caret column 0 on 047
is pre-existing).

Linear smoke (optional; token header unchanged):

```
BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 \
BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1 \
./src/babyc src/tests/001_assignment.c
```

---

## Conflict cookbook

| Other baseline looks like | What to do |
|---|---|
| `TokenLineTab` already `int *` | Skip 1a/1b/2a/2b/2d/3/4 type changes. Still do 2c if the 65535 clamp remains. Still do 2e/2f if `Parser->Line++` is unconditional. |
| No `TokenLineTab` at all | You cannot apply P0 as-is. Origin work has to land a sidecar table first; this document does not invent that. |
| Walk-back already gone | Skip file 5. |
| `Parser->Line` still `short int` | Widen `ParseState.Line` to `int` in the same overlay as 1a, or amalgam lines wrap. |
| Extra `(short int)` stores into `Parser->Line` | Drop the cast. Same rule as files 7/8. |
| `LexGetRawToken` already reads `TokenLineTab[TokenIndex]` | Skip 2f. Verify 2e is also present. |
| Function-body parse still has a non-NULL `TokenLineTab` | Set it NULL after `LexCopyTokens` (already in this zero). Do not copy a slice of the file table. |

---

## Files in this zip

```
PREPROCESSOR_P0_ADAPT_TO_OTHER_BASELINE_2026-10-27.md
PREPROCESSOR_P0_ORIGIN_2026-10-27.md
patches/interpreter.h.diff
patches/lex.c.diff
patches/parse.c.diff
patches/expression.c.diff
patches/platform.c.diff
patches/diagnostic.c.diff
patches/ir_mem.c.diff
patches/ir_vm.c.diff
```

The diffs are against zero `c2d0f900...`. Use them with `patch -p0`
only if the other tree is that zero. On a drifted tree, apply by the
snippets above, not by line number.
