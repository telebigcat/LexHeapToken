/* diagnostic.c - engine-neutral diagnostic delivery.
 *
 * Execution engines supply a source location and message; this module owns
 * the presentation. It is deliberately off the successful execution path. */
#include "picoc.h"
#include "interpreter.h"
#include <string.h>
#include <stdint.h>
#if defined(_WIN32) || defined(WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

/* Resolve an indexed-expression location centrally for every execution engine.
 * IndexDepth is one-based (grid[i] = 1, grid[i][j] = 2); zero preserves the
 * historical meaning of selecting the last closing bracket. This runs only
 * on an error path and never participates in expression evaluation. */
void DiagnosticSetIndexAccessLocation(struct ParseState *Parser,
                                      const char *BaseIdent, int IndexDepth)
{
    const char *line, *p, *end, *candidate = NULL, *target = NULL;
    int lineNo, nesting = 0, accessDepth = 0, identLen;
    if (Parser == NULL || Parser->SourceText == NULL || Parser->Line <= 0)
        return;
    line = Parser->SourceText;
    for (lineNo = 1; *line != '\0' && lineNo < Parser->Line; ++line)
        if (*line == '\n') ++lineNo;
    if (lineNo != Parser->Line) return;
    end = line;
    while (*end != '\0' && *end != '\n' && *end != '\r') ++end;
    if (BaseIdent == NULL || BaseIdent[0] == '\0') return;
    identLen = (int)strlen(BaseIdent);
    for (p = line; p + identLen <= end; ++p)
    {
        char before, after;
        if (strncmp(p, BaseIdent, (size_t)identLen) != 0) continue;
        before = p > line ? p[-1] : ' ';
        after = p[identLen];
        if ((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z')
            || (before >= '0' && before <= '9') || before == '_'
            || (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z')
            || (after >= '0' && after <= '9') || after == '_') continue;
        {
            const char *afterIdent = p + identLen;
            while (afterIdent < end && (*afterIdent == ' ' || *afterIdent == '\t')) ++afterIdent;
            if (afterIdent < end && *afterIdent == '[')
            {
                candidate = afterIdent;
                if (IndexDepth > 0) break;
            }
        }
    }
    if (candidate == NULL) return;
    if (IndexDepth <= 0)
    {
        /* Preserve the historical helper contract used by uninitialized-read
         * diagnostics: point at the last closing bracket in the access. */
        for (p = candidate; p < end; ++p)
        {
            if (*p == '[') ++nesting;
            else if (*p == ']')
            {
                if (nesting > 0) --nesting;
                if (nesting == 0) target = p;
            }
            else if (nesting == 0 && (*p == ';' || *p == ',')) break;
        }
    }
    else
    {
        for (p = candidate; p < end; ++p)
        {
            if (*p == '[')
            {
                if (nesting == 0) ++accessDepth;
                ++nesting;
                if (nesting == 1 && accessDepth == IndexDepth)
                {
                    target = p + 1;
                    while (target < end && (*target == ' ' || *target == '\t')) ++target;
                    break;
                }
            }
            else if (*p == ']')
            {
                if (nesting > 0) --nesting;
            }
            else if (nesting == 0 && (*p == ';' || *p == ',')) break;
        }
    }
    if (target != NULL && target < end)
        Parser->CharacterPos = (short int)(target - line + (IndexDepth <= 0 ? 1 : 1));
}

/* Canonical message catalog for diagnostics shared by execution engines.
 * Keep the literal roots here so source audits can identify any new engine
 * that bypasses the common diagnostic vocabulary. */
const char DiagnosticMsgNullPointerDereference[] = "NULL pointer dereference";
const char DiagnosticMsgUninitializedVariable[] = "uninitialized variable";
const char DiagnosticMsgUninitializedVariableNamed[] = "uninitialized variable '%s'";
const char DiagnosticMsgDivisionByZero[] = "division by zero";
const char DiagnosticMsgModuloByZero[] = "modulo by zero";
const char DiagnosticMsgArrayIndexOutOfBounds[] = "array index out of bounds";
const char DiagnosticMsgArrayIndexOutOfBoundsSized[] = "array index %d out of bounds (size %d)";
const char DiagnosticMsgOutOfMemory[] = "out of memory";
const char DiagnosticMsgLexerSourceTooLarge[] = "source too large for lexer";
const char DiagnosticMsgNullFunctionPointer[] = "null function pointer";
const char DiagnosticMsgFunctionUndefined[] = "function is undefined";
const char DiagnosticMsgUnresolvedObjectSize[] = "cannot resolve canonical object size for member access";
const char DiagnosticMsgMemcpyOverlap[] = "memcpy source and destination regions overlap (undefined behavior in C)";

/* Async-signal-safe renderer for OS faults. Keep this implementation here,
 * beside the normal diagnostic renderer, but use raw writes only: the ordinary
 * stdio renderer is not safe from POSIX signal handlers. */
static void DiagnosticRawWrite(const char *Text, size_t Length)
{
    while (Length > 0) {
#if defined(_WIN32) || defined(WIN32)
        int N = _write(2, Text, (unsigned)(Length > 0x7fffffffU ? 0x7fffffffU : Length));
#else
        ssize_t N = write(2, Text, Length);
#endif
        if (N <= 0) return;
        Text += (size_t)N;
        Length -= (size_t)N;
    }
}

static void DiagnosticRawString(const char *S)
{
    const char *P;
    if (S == NULL) S = "?";
    for (P = S; *P != '\0'; P++) { }
    DiagnosticRawWrite(S, (size_t)(P - S));
}

static void DiagnosticRawInt(int Value)
{
    char Digits[16];
    unsigned N = 0;
    unsigned V;
    if (Value < 0) {
        DiagnosticRawString("-");
        V = (unsigned)(-(Value + 1)) + 1U;
    } else V = (unsigned)Value;
    do { Digits[N++] = (char)('0' + V % 10U); V /= 10U; } while (V != 0 && N < sizeof(Digits));
    while (N > 0) DiagnosticRawWrite(&Digits[--N], 1);
}

void DiagnosticReportCrashSignalSafe(const char *FileName, const char *SourceText,
                                     int Line, int Column, const char *Kind,
                                     const char *FunctionName)
{
    const char *Base = FileName ? FileName : "?";
    const char *P, *LinePos, *End;
    int CurrentLine = 1, C = 0;
    for (P = Base; *P; P++) if (*P == '/' || *P == '\\') Base = P + 1;
    if (SourceText != NULL && Line > 0) {
        for (LinePos = SourceText; *LinePos && CurrentLine < Line; LinePos++)
            if (*LinePos == '\n') CurrentLine++;
        End = LinePos;
        while (*End && *End != '\n' && *End != '\r') End++;
        DiagnosticRawWrite(LinePos, (size_t)(End - LinePos));
        DiagnosticRawString("\n");
        for (P = LinePos; P < End && (C < (Column > 0 ? Column - 1 : 0) || *P == ' '); P++, C++) {
            char Ch = (*P == '\t') ? '\t' : ' ';
            DiagnosticRawWrite(&Ch, 1);
        }
        DiagnosticRawString("^\n");
    } else {
        DiagnosticRawString("^\n");
    }
    DiagnosticRawString(Base); DiagnosticRawString(":"); DiagnosticRawInt(Line);
    DiagnosticRawString(":"); DiagnosticRawInt(Column); DiagnosticRawString(" ");
    DiagnosticRawString(Kind ? Kind : "FAULT");
    DiagnosticRawString(" inside library function '");
    DiagnosticRawString(FunctionName ? FunctionName : "?");
    DiagnosticRawString("'\n");
}

/* Compare ranges by distance rather than end-address addition, which could
 * wrap uintptr_t for hostile or malformed pointer values. Zero-byte copies
 * access no bytes and therefore have no overlapping byte ranges. */
int PicocMemoryRangesOverlap(const void *A, const void *B, size_t Bytes)
{
    uintptr_t X, Y;
    if (Bytes == 0 || A == NULL || B == NULL) return 0;
    X = (uintptr_t)A;
    Y = (uintptr_t)B;
    return X <= Y ? (Y - X) < Bytes : (X - Y) < Bytes;
}

/* Error-path-only source access resolver. Runtime engines often detect an
 * uninitialized aggregate at its storage root (v) or after evaluating an
 * indexed aggregate (v[i].field). Normalize that legacy anchor here, once,
 * instead of teaching every engine to rediscover the same member column.
 * This is deliberately not used by successful execution and adds no VM
 * instruction fields or per-instruction bookkeeping. */
static int DiagnosticMemberColumn(const char *SourceText, int Line, int Column)
{
    const char *LP, *P;
    int L = 1, BestCol = 0, BestDistance = 0x7fffffff;
    if (SourceText == NULL || Line <= 0) return 0;
    for (LP = SourceText; *LP != '\0' && L < Line; LP++)
        if (*LP == '\n') L++;
    if (L != Line) return 0;

    /* Ignore strings and comments while scanning this source line. */
    {
        int InString = 0, InChar = 0, Escape = 0;
        for (P = LP; *P != '\0' && *P != '\n' && *P != '\r'; ) {
            const char *Start, *Q, *LastMember = NULL;
            int HasMember = 0, EndCol, StartCol, Distance;
            char C = *P;
            if (Escape) { Escape = 0; P++; continue; }
            if ((InString || InChar) && C == '\\') { Escape = 1; P++; continue; }
            if (!InChar && C == '"') { InString = !InString; P++; continue; }
            if (!InString && C == '\'') { InChar = !InChar; P++; continue; }
            if (InString || InChar) { P++; continue; }
            if (C == '/' && P[1] == '/') break;
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_')) {
                P++; continue;
            }
            Start = P;
            Q = P + 1;
            while ((*Q >= 'a' && *Q <= 'z') || (*Q >= 'A' && *Q <= 'Z')
                   || (*Q >= '0' && *Q <= '9') || *Q == '_') Q++;
            P = Q;
            for (;;) {
                while (*P == ' ' || *P == '\t') P++;
                if (*P == '[') {
                    int Depth = 1, InS = 0, InC = 0, Esc = 0;
                    P++;
                    while (*P && *P != '\n' && *P != '\r' && Depth > 0) {
                        if (Esc) { Esc = 0; P++; continue; }
                        if ((InS || InC) && *P == '\\') { Esc = 1; P++; continue; }
                        if (!InC && *P == '"') InS = !InS;
                        else if (!InS && *P == '\'') InC = !InC;
                        else if (!InS && !InC && *P == '[') Depth++;
                        else if (!InS && !InC && *P == ']') Depth--;
                        P++;
                    }
                    if (Depth != 0) break;
                    continue;
                }
                if (*P == '.' || (P[0] == '-' && P[1] == '>')) {
                    P += (*P == '.') ? 1 : 2;
                    while (*P == ' ' || *P == '\t') P++;
                    if (!((*P >= 'a' && *P <= 'z') || (*P >= 'A' && *P <= 'Z') || *P == '_')) break;
                    LastMember = P;
                    HasMember = 1;
                    P++;
                    while ((*P >= 'a' && *P <= 'z') || (*P >= 'A' && *P <= 'Z')
                           || (*P >= '0' && *P <= '9') || *P == '_') P++;
                    continue;
                }
                break;
            }
            if (HasMember && LastMember != NULL) {
                StartCol = (int)(Start - LP) + 1;
                EndCol = (int)(P - LP) + 1;
                if (Column < StartCol) Distance = StartCol - Column;
                else if (Column > EndCol) Distance = Column - EndCol;
                else Distance = 0;
                if (Distance < BestDistance) {
                    BestDistance = Distance;
                    BestCol = (int)(LastMember - LP) + 1;
                }
            }
            /* The outer scan resumes at the end of the access expression. */
        }
    }
    return BestCol;
}

static int DiagnosticResolvedColumn(const DiagnosticLocation *Location,
                                    const char *Message, int Fallback)
{
    int Column = Fallback;
    if (Location != NULL && Message != NULL
        && strstr(Message, DiagnosticMsgUninitializedVariable) != NULL) {
        int MemberColumn = DiagnosticMemberColumn(Location->SourceText,
                                                   Location->Line, Fallback);
        if (MemberColumn > 0) Column = MemberColumn;
    }
    return Column;
}

static void DiagnosticRenderLocationTo(IOFILE *Stream,
                                        const DiagnosticLocation *Location,
                                        const char *Message)
{
    const char *FileName;
    const char *Base;
    const char *P;
    int Column;
    if (Stream == NULL || Location == NULL)
        return;

    /* Preserve Column==0 (historical parse-error / unknown-column convention).
     * Only member-column resolution may replace a positive fallback. */
    Column = Location->Column;
    if (Column > 0)
        Column = DiagnosticResolvedColumn(Location, Message, Column);
    if (Column < 0)
        Column = 0;

    /* Even synthetic/library parsers and early-init failures must retain the
     * location metadata when source text is unavailable. Never silently drop
     * filename/line/column merely because the renderer cannot show the line. */
    if (Location->SourceText != NULL && Location->Line > 0)
    {
        PrintSourceTextErrorLine(Stream, Location->FileName, Location->SourceText,
                                 Location->Line, Column);
        return;
    }

    FileName = Location->FileName != NULL ? Location->FileName : "input";
    Base = FileName;
    for (P = FileName; *P != '\0'; P++)
        if (*P == '/' || *P == '\\') Base = P + 1;
    /* Interactive input has no retained source-text buffer in some error
     * paths. Its diagnostic location still uses the REPL's 1-based column,
     * but the terminal line begins with the prompt, which is not part of the
     * parsed source. Keep the caret aligned with the user's input rather
     * than column zero. An empty filename is the established REPL sentinel. */
    if (Location->FileName != NULL && Location->FileName[0] == '\0')
    {
        int Indent;
        for (Indent = 0;
             Indent < (int)strlen(INTERACTIVE_PROMPT_STATEMENT)
                    + (Column > 0 ? Column - 1 : 0);
             Indent++)
            PlatformPrintf(Stream, " ");
    }
    PlatformPrintf(Stream, "^\n%s:%d:%d ", Base,
                   Location->Line > 0 ? Location->Line : 0, Column);
}

void DiagnosticReportV(Picoc *pc, const DiagnosticLocation *Location,
                       const char *Message, va_list Args)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;
    const DiagnosticLocation RuntimeLocation = { "<runtime>", NULL, 0, 0 };
    if (Location == NULL)
        Location = &RuntimeLocation;

    if (Stream == NULL)
    {
        DiagnosticRenderLocationTo(stderr, Location, Message);
        vfprintf(stderr, Message, Args);
        fputc('\n', stderr);
        fflush(stderr);
        return;
    }

    DiagnosticRenderLocationTo(Stream, Location, Message);
    PlatformVPrintf(Stream, Message, Args);
    PlatformPrintf(Stream, "\n");
    fflush(Stream);
}

void DiagnosticReportAssign(Picoc *pc, const DiagnosticLocation *Location,
                            const char *Format, struct ValueType *Type1,
                            struct ValueType *Type2, int Num1, int Num2,
                            const char *FuncName, int ParamNo)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;
    const DiagnosticLocation RuntimeLocation = { "<runtime>", NULL, 0, 0 };
    if (Location == NULL)
        Location = &RuntimeLocation;
    if (Stream == NULL)
    {
        DiagnosticRenderLocationTo(stderr, Location, "");
        fprintf(stderr, "can't %s\n", (FuncName == NULL) ? "assign" : "set");
        fflush(stderr);
        return;
    }

    DiagnosticRenderLocationTo(Stream, Location, "");
    PlatformPrintf(Stream, "can't %s ", (FuncName == NULL) ? "assign" : "set");
    if (Type1 != NULL)
        PlatformPrintf(Stream, Format, Type1, Type2);
    else
        PlatformPrintf(Stream, Format, Num1, Num2);
    if (FuncName != NULL)
        PlatformPrintf(Stream, " in argument %d of call to %s()", ParamNo, (char *)FuncName);
    PlatformPrintf(Stream, "\n");
    fflush(Stream);
}

void DiagnosticReport(Picoc *pc, const DiagnosticLocation *Location,
                     const char *Message, ...)
{
    va_list Args;
    va_start(Args, Message);
    DiagnosticReportV(pc, Location, Message, Args);
    va_end(Args);
}

/* Render a diagnostic span through the canonical renderer. Macro redefinition
 * is the first user of spans: pointing only at the first character makes the
 * duplicate hard to find in long names such as ENABLE_VIRTUAL_TERMINAL_PROCESSING.
 * This stays in diagnostic.c so callers never print diagnostics themselves. */
static void DiagnosticReportSpan(Picoc *pc, const DiagnosticLocation *Location,
                                const char *Message, size_t Width)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;
    const char *LinePos, *End, *P, *Base;
    int LineNo, Column, Offset;
    if (Stream == NULL || Location == NULL || Location->SourceText == NULL
        || Location->Line <= 0 || Location->Column <= 0 || Width == 0) {
        DiagnosticReport(pc, Location, "%s", Message);
        return;
    }
    LinePos = Location->SourceText;
    for (LineNo = 1; *LinePos != '\0' && LineNo < Location->Line; LinePos++)
        if (*LinePos == '\n') LineNo++;
    if (LineNo != Location->Line) {
        DiagnosticReport(pc, Location, "%s", Message);
        return;
    }
    End = LinePos;
    while (*End != '\0' && *End != '\n' && *End != '\r') End++;
    Column = Location->Column;
    if ((size_t)(End - LinePos) < (size_t)(Column - 1))
        Column = (int)(End - LinePos) + 1;
    for (P = LinePos; P < End; P++) PrintCh(*P, Stream);
    PrintCh('\n', Stream);
    for (P = LinePos, Offset = 0; P < End && Offset < Column - 1; P++, Offset++)
        PrintCh(*P == '\t' ? '\t' : ' ', Stream);
    PrintCh('^', Stream);
    if (Width > (size_t)(End - LinePos - (Column - 1)))
        Width = (size_t)(End - LinePos - (Column - 1));
    while (Width > 1) { PrintCh('~', Stream); Width--; }
    PrintCh('\n', Stream);
    Base = Location->FileName != NULL ? Location->FileName : "input";
    for (P = Base; *P != '\0'; P++) if (*P == '/' || *P == '\\') Base = P + 1;
    PlatformPrintf(Stream, "%s:%d:%d %s\n", Base, Location->Line,
                   Location->Column, Message);
}

/* Report both ends of a macro collision through the canonical renderer.
 * The previous-definition note is emitted before the fatal caller exits, and
 * uses the original definition's stored source provenance when available. */
void DiagnosticReportMacroRedefinition(Picoc *pc,
                                       const DiagnosticLocation *Current,
                                       const DiagnosticLocation *Previous,
                                       const char *MacroName)
{
    char Message[512];
    snprintf(Message, sizeof(Message), "'%s' is already defined", MacroName);
    DiagnosticReportSpan(pc, Current, Message, strlen(MacroName));
    if (Previous != NULL && Previous->FileName != NULL && Previous->Line > 0) {
        snprintf(Message, sizeof(Message), "note: previous definition of '%s' is here", MacroName);
        DiagnosticReportSpan(pc, Previous, Message, strlen(MacroName));
    } else {
        DiagnosticReport(pc, NULL,
                         "note: previous definition of '%s' has no source location", MacroName);
    }
}

int ParserSetDiagnosticCaret(struct ParseState *Parser, const char *BaseIdent,
                             const char *MemberIdent)
{
    const char *Src, *LinePos, *CPos, *Base = NULL, *Dot = NULL;
    int LineCount, BaseLen, MemberLen;
    if (Parser == NULL || Parser->SourceText == NULL)
        return 0;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1;
         *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n') LineCount++;
    if (*LinePos == '\r') LinePos++;
    BaseLen = (BaseIdent != NULL) ? (int)strlen(BaseIdent) : 0;
    MemberLen = (MemberIdent != NULL) ? (int)strlen(MemberIdent) : 0;

    /* Find the last whole-word root identifier on this source line. */
    if (BaseLen > 0) {
        for (CPos = LinePos; *CPos && *CPos != '\n' && *CPos != '\r'; CPos++) {
            char before, after;
            if (strncmp(CPos, BaseIdent, (size_t)BaseLen) != 0) continue;
            before = CPos > LinePos ? CPos[-1] : ' ';
            after = CPos[BaseLen];
            if ((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z')
                || before == '_' || (before >= '0' && before <= '9')) continue;
            if ((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z')
                || after == '_' || (after >= '0' && after <= '9')) continue;
            Base = CPos;
        }
    }
    if (Base != NULL) {
        const char *End = Base;
        while (*End && *End != '\n' && *End != '\r' && *End != ';' && *End != ',') End++;
        /* A named member is preferred only when it belongs to this access. */
        if (MemberLen > 0) {
            for (CPos = Base + BaseLen; CPos + MemberLen <= End; CPos++) {
                if (strncmp(CPos, MemberIdent, (size_t)MemberLen) == 0) {
                    char before = CPos > LinePos ? CPos[-1] : ' ';
                    char after = CPos[MemberLen];
                    if (before == '.' && !((after >= 'a' && after <= 'z')
                        || (after >= 'A' && after <= 'Z') || after == '_'
                        || (after >= '0' && after <= '9'))) {
                        Parser->CharacterPos = (int)(CPos - LinePos) + 1;
                        return 1;
                    }
                }
            }
        }
        /* Otherwise anchor the last .member leaf after the root, allowing
         * intervening indexes and nested member access. */
        for (CPos = Base + BaseLen; CPos < End; CPos++)
            if (*CPos == '.' && CPos[1] != '\0' && CPos[1] != '.'
                && ((CPos[1] >= 'a' && CPos[1] <= 'z')
                    || (CPos[1] >= 'A' && CPos[1] <= 'Z') || CPos[1] == '_'))
                Dot = CPos + 1;
        if (Dot != NULL) {
            Parser->CharacterPos = (int)(Dot - LinePos) + 1;
            return 1;
        }
    }
    if (BaseIdent != NULL)
        ParserSetCaretOnIdent(Parser, BaseIdent);
    return 0;
}

