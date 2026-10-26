# Lexer token scratch on host heap — 2026-10-26

Delta overlay sulla baseline `/tmp/babyc_new_baseline.zip`
(SHA-256 `8b0254019d58f77966dcd3d0e3954fcf3f061cea8c5f5daa999bbfd72abb4fc1`).

Contiene i file sorgente completi (non una patch). Overlay sulla tree
`babyc/` della baseline, poi `make -C src babyc`.

Nessun `.expect` toccato. Nessun altro file della baseline toccato.

## Problema

`babyc sqlite3.c` (amalgama ufficiale SQLite, ~9.1 MB) falliva subito con:

```
sqlite3.c:1:1 out of memory
```

Il messaggio era fuorviante: non era un fallimento di `malloc` dell'host.
Era il tetto fisso dello stack interno Picoc (`PICOC_STACK_SIZE` = 2 MB
in `src/picoc.c`).

## Causa

`LexTokenise` in `src/lex.c` preallocava il buffer token con:

```
ReserveSpace = (End - Pos) * 12 + 16
TokenSpace   = HeapAllocStack(pc, ReserveSpace)
```

`HeapAllocStack` cresce nell'arena Picoc (`HeapStackTop` verso `HeapBottom`).
Su UNIX/WIN32 quell'arena e' 2 MB allocati una volta in `HeapInit`. Per
`sqlite3.c` servono ~109 MB di scratch (`9.1e6 * 12`). La richiesta supera
l'arena, `HeapAllocStack` torna `NULL`, `LexFail` stampa `out of memory`
con caret in riga 1 colonna 1 (il lexer non ha ancora avanzato).

`HeapAllocMem` su UNIX/WIN32 e' gia' `calloc` di sistema (`USE_MALLOC_HEAP`
in `src/platform.h` e `src/platform/platform_win32.h`). Il buffer linee e il
buffer token definitivo usavano gia' l'heap; solo lo scratch intermedio
passava dallo stack da 2 MB.

## File modificati (3)

### `src/lex.c`

1. `#include <stddef.h>` per `ptrdiff_t`.
2. `LexTokenise`:
   - `SourceLen` calcolato come `ptrdiff_t`.
   - Overflow guard: se `SourceLen < 0` o `SourceLen > (INT_MAX - 16) / 12`
     emette `source too large for lexer` (bound `int` di `ReserveSpace`,
     non tetto heap).
   - `USE_MALLOC_HEAP` (UNIX/WIN32): scratch via `HeapAllocMem`. `NULL` =
     vero OOM host -> `out of memory`.
   - Embedded (no `USE_MALLOC_HEAP`): resta `HeapAllocStack`. `NULL` =
     tetto arena -> `source too large for lexer`, non `out of memory`.
   - Guardia per-token: se `MemUsed + 2 + ValueSize > ReserveSpace`
     emette `source too large for lexer` invece di scrivere oltre il buffer.
   - Cleanup successo: `HeapFreeMem` sullo scratch heap, `HeapPopStack`
     sullo scratch arena.
   - Conservato il ramo `TokenFPConstant` della baseline (`FPFormat` tag +
     `double`); non e' stato sostituito con il lexer del workspace.
3. `LexCleanupOnFailure` (percorso `LexFail` / `longjmp`):
   - stesso split `HeapFreeMem` / `HeapPopStack` per non leakare lo scratch
     heap e non chiamare `HeapPopStack` su un puntatore malloc.

Il buffer linee (`LineTab`) e il buffer token definitivo (`HeapMem`) erano
gia' su `HeapAllocMem`; non cambiati.

### `src/diagnostic.c`

Nuovo letterale, distinto da `out of memory`:

```
DiagnosticMsgLexerSourceTooLarge = "source too large for lexer"
```

`out of memory` resta riservato al fallimento reale di allocazione host.
`DiagnosticMsgUnresolvedObjectSize` della baseline e' invariato.

### `src/interpreter.h`

Dichiarazione:

```
extern const char DiagnosticMsgLexerSourceTooLarge[];
```

Nessun campo nuovo in `Picoc`: si riusa `LexTokeniseStackScratch` /
`LexTokeniseStackScratchSize` (ora heap o stack a seconda della piattaforma).

## Cosa non e' stato toccato

- `PICOC_STACK_SIZE` resta 2 MB (stack di esecuzione, non lexer).
- `HeapAllocStack` / `HeapAllocMem` invariati.
- Parser, IR, Linear, bytecode, `.expect`: invariati.
- Catalogo canonico `out of memory`: il nuovo messaggio e' contestuale
  e non entra nei letterali verificati da `tools/check_diagnostic_catalog.py`.

## Come applicare

Dalla tree della baseline (`babyc/`):

```
# Overlay i tre file da questo zip (percorsi babyc/src/...)
# poi rebuild
make -C src babyc
```

File nel zip:

- `babyc/src/lex.c`
- `babyc/src/diagnostic.c`
- `babyc/src/interpreter.h`
- `LEX_HEAP_TOKEN_SCRATCH_2026-10-26.md` (questo documento)
- `DELTA_MANIFEST.txt`
- `CHANGED_FILES_SHA256.txt`

## Verifiche su questa overlay

Eseguite su copia della baseline + questi tre file, `make -C src babyc`:

- Link `src/babyc` riuscito. Warning preesistenti della baseline
  (`ir_mem.c` array-bounds, `cstdlib/stdlib.c` use-after-free,
  `winapi/src/wfiles.c` strcasestr); nessuno in `lex.c` / `diagnostic.c`.
- `001_assignment.c`: `42 / 64 / 12, 34`, exit 0.
- `/tmp/lex_heap_smoke.c` (~324 KB comment filler): exit 0.
- `src/third_party/sqlite/sqlite3.c`: niente `out of memory` a 1:1;
  tokenizza e fallisce dopo a `sqlite3.c:15699:8 illegal character '$'`
  (l'amalgama SQLite non e' un programma BabyC).
