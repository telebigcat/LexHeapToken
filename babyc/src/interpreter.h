/* picoc main header file - this has all the main data structures and
 * function prototypes. If you're just calling picoc you should look at the
 * external interface instead, in picoc.h */

#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "platform.h"

/* Canonical semantic diagnostic text. Keep user-visible root strings in
 * diagnostic.c; execution engines select these shared messages instead of
 * embedding independent copies. */
extern const char DiagnosticMsgNullPointerDereference[];
extern const char DiagnosticMsgUninitializedVariable[];
extern const char DiagnosticMsgUninitializedVariableNamed[];
extern const char DiagnosticMsgDivisionByZero[];
extern const char DiagnosticMsgModuloByZero[];
extern const char DiagnosticMsgArrayIndexOutOfBounds[];
extern const char DiagnosticMsgArrayIndexOutOfBoundsSized[];
extern const char DiagnosticMsgOutOfMemory[];
extern const char DiagnosticMsgLexerSourceTooLarge[];
extern const char DiagnosticMsgNullFunctionPointer[];
extern const char DiagnosticMsgFunctionUndefined[];
extern const char DiagnosticMsgUnresolvedObjectSize[];
extern const char DiagnosticMsgMemcpyOverlap[];
int PicocMemoryRangesOverlap(const void *A, const void *B, size_t Bytes);
void BabycHeapTrackAllocation(void *Ptr, size_t Size);

/* Engine-neutral source location for diagnostic delivery (error path only). */
typedef struct DiagnosticLocation {
    const char *FileName;
    const char *SourceText;
    int Line;
    int Column;
} DiagnosticLocation;


/* handy definitions */
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef NULL
#define NULL 0
#endif

#ifndef min
#define min(x,y) (((x)<(y))?(x):(y))
#endif

#define MEM_ALIGN(x) (((x) + sizeof(ALIGN_TYPE) - 1) & ~(sizeof(ALIGN_TYPE)-1))

#define GETS_BUF_MAX 256

/* for debugging */
#define PRINT_SOURCE_POS ({ PrintSourceTextErrorLine(Parser->pc->CStdOut, Parser->FileName, Parser->SourceText, Parser->Line, Parser->CharacterPos); PlatformPrintf(Parser->pc->CStdOut, "\n"); })
#define PRINT_TYPE(typ) PlatformPrintf(Parser->pc->CStdOut, "%t\n", typ);

/* small processors use a simplified FILE * for stdio, otherwise use the system FILE * */
#ifdef BUILTIN_MINI_STDLIB
typedef struct OutputStream IOFILE;
#else
typedef FILE IOFILE;
#endif

/* coercion of numeric types to other numeric types */
#ifndef NO_FP
#define IS_FP(v) ((v)->Typ->Base == TypeFP)
#define FP_VAL(v) ((v)->Val->FP)
#else
#define IS_FP(v) 0
#define FP_VAL(v) 0
#endif

#define IS_POINTER_COERCIBLE(v, ap) ((ap) ? ((v)->Typ->Base == TypePointer) : 0)
#define POINTER_COERCE(v) ((int)(v)->Val->Pointer)

#define IS_INTEGER_NUMERIC_TYPE(t) ((t)->Base >= TypeInt && (t)->Base <= TypeUnsignedLongLong)
#define IS_INTEGER_NUMERIC(v) IS_INTEGER_NUMERIC_TYPE((v)->Typ)
#define IS_NUMERIC_COERCIBLE(v) (IS_INTEGER_NUMERIC(v) || IS_FP(v))
#define IS_NUMERIC_COERCIBLE_PLUS_POINTERS(v,ap) (IS_NUMERIC_COERCIBLE(v) || IS_POINTER_COERCIBLE(v,ap))


struct Table;
struct Picoc_Struct;
struct ValueType;

typedef struct Picoc_Struct Picoc;

void DiagnosticReportV(Picoc *pc, const DiagnosticLocation *Location,
                       const char *Message, va_list Args);
/* Raw-write diagnostic renderer for OS fault/signal contexts; no stdio, heap,
 * or locks. Implemented in diagnostic.c so crash diagnostics share one owner. */
void DiagnosticReportCrashSignalSafe(const char *FileName, const char *SourceText,
                                     int Line, int Column, const char *Kind,
                                     const char *FunctionName);
void DiagnosticReport(Picoc *pc, const DiagnosticLocation *Location,
                      const char *Message, ...);
void DiagnosticReportMacroRedefinition(Picoc *pc,
                                       const DiagnosticLocation *Current,
                                       const DiagnosticLocation *Previous,
                                       const char *MacroName);
void DiagnosticReportAssign(Picoc *pc, const DiagnosticLocation *Location,
                            const char *Format, struct ValueType *Type1,
                            struct ValueType *Type2, int Num1, int Num2,
                            const char *FuncName, int ParamNo);
void ProgramFailAt(Picoc *pc, const DiagnosticLocation *Location,
                   const char *Message, ...);

/* lexical tokens */
enum LexToken
{
    /* 0x00 */ TokenNone,
    /* 0x01 */ TokenComma,
    /* 0x02 */ TokenAssign, TokenAddAssign, TokenSubtractAssign, TokenMultiplyAssign, TokenDivideAssign, TokenModulusAssign,
    /* 0x08 */ TokenShiftLeftAssign, TokenShiftRightAssign, TokenArithmeticAndAssign, TokenArithmeticOrAssign, TokenArithmeticExorAssign,
    /* 0x0d */ TokenQuestionMark, TokenColon,
    /* 0x0f */ TokenLogicalOr,
    /* 0x10 */ TokenLogicalAnd,
    /* 0x11 */ TokenArithmeticOr,
    /* 0x12 */ TokenArithmeticExor,
    /* 0x13 */ TokenAmpersand,
    /* 0x14 */ TokenEqual, TokenNotEqual,
    /* 0x16 */ TokenLessThan, TokenGreaterThan, TokenLessEqual, TokenGreaterEqual,
    /* 0x1a */ TokenShiftLeft, TokenShiftRight,
    /* 0x1c */ TokenPlus, TokenMinus,
    /* 0x1e */ TokenAsterisk, TokenSlash, TokenModulus,
    /* 0x21 */ TokenIncrement, TokenDecrement, TokenUnaryNot, TokenUnaryExor, TokenSizeof, TokenCast,
    /* 0x27 */ TokenLeftSquareBracket, TokenRightSquareBracket, TokenDot, TokenArrow,
    /* 0x2b */ TokenOpenBracket, TokenCloseBracket,
    /* 0x2d */ TokenIdentifier, TokenIntegerConstant, TokenFPConstant, TokenStringConstant, TokenCharacterConstant,
    /* 0x32 */ TokenSemicolon, TokenEllipsis,
    /* 0x34 */ TokenLeftBrace, TokenRightBrace,
    /* 0x36 */ TokenIntType, TokenCharType, TokenFloatType, TokenDoubleType, TokenVoidType, TokenEnumType,
    /* 0x3c */ TokenLongType, TokenSignedType, TokenShortType, TokenStaticType, TokenAutoType, TokenRegisterType, TokenExternType, TokenConstType, TokenStructType, TokenUnionType, TokenUnsignedType, TokenTypedef,
    /* 0x46 */ TokenContinue, TokenDo, TokenElse, TokenFor, TokenGoto, TokenIf, TokenWhile, TokenBreak, TokenSwitch, TokenCase, TokenDefault, TokenReturn,
    /* 0x52 */ TokenHashDefine, TokenHashInclude, TokenHashIf, TokenHashIfdef, TokenHashIfndef, TokenHashElse, TokenHashEndif,
    /* 0x59 */ TokenNew, TokenDelete,
    /* 0x5b */ TokenOpenMacroBracket,
    /* 0x5c */ TokenVolatileType,
    TokenOffsetof,
    /* 0x5d */ TokenEOF, TokenEndOfLine, TokenEndOfFunction
};

/* used in dynamic memory allocation */
struct AllocNode
{
    unsigned int Size;
    struct AllocNode *NextFree;
};

/* whether we're running or skipping code */
enum RunMode
{
    RunModeRun,                 /* we're running code as we parse it */
    RunModeSkip,                /* skipping code, not running */
    RunModeReturn,              /* returning from a function */
    RunModeCaseSearch,          /* searching for a case label */
    RunModeBreak,               /* breaking out of a switch/while/do */
    RunModeContinue,            /* as above but repeat the loop */
    RunModeGoto                 /* searching for a goto label */
};

/* parser state - has all this detail so we can parse nested files */
struct ParseState
{
    Picoc *pc;                  /* the picoc instance this parser is a part of */
    const unsigned char *Pos;   /* the character position in the source text */
    char *FileName;             /* what file we're executing (registered string) */
    short int Line;             /* line number we're executing */
    short int CharacterPos;     /* character/column in the line we're executing */
    enum RunMode Mode;          /* whether to skip or run code */
    int SearchLabel;            /* what case label we're searching for */
    const char *SearchGotoLabel;/* what goto label we're searching for */
    const char *SourceText;     /* the entire source text */
    short int HashIfLevel;      /* how many "if"s we're nested down */
    short int HashIfEvaluateToLevel;    /* if we're not evaluating an if branch, what the last evaluated level was */
    char DebugMode;
    const unsigned short *TokenLineTab;
    int TokenLineCount;
    int TokenIndex;
    const unsigned char *TokenBase;
    int ScopeID;                /* for keeping track of local variables (free them after they go out of scope) */
    int DeclIsConst;            /* set by TypeParseFront: current decl is const */
    int DeclIsVolatile;         /* set by TypeParseFront: current decl is volatile */
    unsigned int DeclVlaDimMask;
    unsigned char DeclArrayDimDepth;
    int UnevaluatedExpr;
    int SizeofOperatorDepth;
    const unsigned char *LastTokenStartPos;
    const unsigned char *ExpressionStopPos;
};

/* values */
enum BaseType
{
    TypeVoid,                   /* no type */
    TypeInt,                    /* integer */
    TypeShort,                  /* short integer */
    TypeChar,                   /* a single character (signed) */
    TypeLong,                   /* long integer */
    TypeLongLong,               /* long long integer */
    TypeUnsignedInt,            /* unsigned integer */
    TypeUnsignedShort,          /* unsigned short integer */
    TypeUnsignedChar,           /* unsigned 8-bit number */ /* must be before unsigned long */
    TypeUnsignedLong,           /* unsigned long integer */
    TypeUnsignedLongLong,       /* unsigned long long integer */
#ifndef NO_FP
    TypeFP,                     /* floating point */
#endif
    TypeFunction,               /* a function */
    TypeMacro,                  /* a macro */
    TypePointer,                /* a pointer */
    TypeArray,                  /* an array of a sub-type */
    TypeStruct,                 /* aggregate type */
    TypeUnion,                  /* merged type */
    TypeEnum,                   /* enumerated integer type */
    TypeGotoLabel,              /* a label we can "goto" */
    Type_Type                   /* a type for storing types */
};

/* Floating-point storage format is explicit metadata, not inferred from
 * Base or from the host C type used to carry a temporary value. */
enum FPStorageFormat
{
    FPStorageNone = 0,
    FPStorageBinary32,
    FPStorageBinary64
};

/* data type */
struct ValueType
{
    enum BaseType Base;             /* what kind of type this is */
    int ArraySize;                  /* the size of an array type */
    int Sizeof;                     /* the storage required */
    int AlignBytes;                 /* the alignment boundary of this type */
    enum FPStorageFormat FPFormat;  /* FP semantic/storage format, independent of Base */
    const char *Identifier;         /* the name of a struct or union */
    struct ValueType *FromType;     /* the type we're derived from (or NULL) */
    struct ValueType *DerivedTypeList;  /* first in a list of types derived from this one */
    struct ValueType *Next;         /* next item in the derived type list */
    struct Table *Members;          /* members of a struct or union */
    int OnHeap;                     /* true if allocated on the heap */
    int StaticQualifier;            /* true if it's a static */
};

/* Forward declaration used by intrinsic function signatures. */
struct Value;

/* function definition */
struct FuncDef
{
    struct ValueType *ReturnType;   /* the return value type */
    int NumParams;                  /* the number of parameters */
    int VarArgs;                    /* has a variable number of arguments after the explicitly specified ones */
    struct ValueType **ParamType;   /* array of parameter types */
    char **ParamName;               /* array of parameter names */
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int); /* library intrinsic or NULL */
    struct ParseState Body;         /* lexical tokens of the function body if not intrinsic */
    void *BodyIr;                   /* IrStmt* compiled body, optional */
};

/* macro definition */
struct MacroDef
{
    int NumParams;                  /* the number of parameters */
    char **ParamName;               /* array of parameter names */
    struct ParseState Body;         /* lexical tokens of the function body if not intrinsic */
    const char *DeclFileName;       /* original #define location for diagnostics */
    const char *DeclSourceText;     /* source containing the original definition */
    int DeclLine;                   /* 1-based source line */
    int DeclColumn;                 /* 1-based macro-name column */
};

/* values */
union AnyValue
{
    char Character;
    short ShortInteger;
    int Integer;
    long LongInteger;
    unsigned short UnsignedShortInteger;
    unsigned int UnsignedInteger;
    unsigned long UnsignedLongInteger;
    unsigned long long UnsignedLongLongInteger;
    long long LongLongInteger;
    unsigned char UnsignedCharacter;
    char *Identifier;
    char ArrayMem[2];               /* placeholder for where the data starts, doesn't point to it */
    struct ValueType *Typ;
    struct FuncDef FuncDef;
    struct MacroDef MacroDef;
#ifndef NO_FP
    double FP;
#endif
    void *Pointer;                  /* unsafe native pointers */
};

struct Value
{
    struct ValueType *Typ;          /* the type of this value */
    union AnyValue *Val;            /* aligned value storage */
    void *ValRaw;                   /* original storage when Val is an aligned scalar proxy */
    struct Value *LValueFrom;       /* if an LValue, this is a Value our LValue is contained within (or NULL) */
    struct Value *InitAliasRoot;    /* tracked object reached through a pointer alias, if known */
    char ValOnHeap;                 /* this Value is on the heap */
    char ValOnStack;                /* the AnyValue is on the stack along with this Value */
    char AnyValOnHeap;              /* the AnyValue is separately allocated from the Value on the heap */
    char IsLValue;                  /* is modifiable and is allocated somewhere we can usefully modify it */
    char IsConst;                   /* declared const — assignment forbidden after init */
    char IsVolatile;                /* declared volatile — accepted; interpreter always re-reads */
    char IsInitialized;             /* set on definition-with-init, assignment, globals/params */
    unsigned char *ElemInit;        /* per-byte init bitmap of an automatic array/struct root; NULL = not tracked */
    int ElemInitCount;              /* number of bytes (bits in ElemInit) covered */
    int ScopeID;                    /* to know when it goes out of scope */
    char OutOfScope;
    /* bitfield metadata (only meaningful for struct/union member descriptors;
     * BitWidth==0 means a normal non-bitfield member) */
    unsigned char BitOffset;        /* bit start within the storage unit */
    unsigned char BitWidth;         /* width in bits; 0 = not a bitfield */
    unsigned short DeclIndex;       /* declaration order in struct/union (members) */
    /* Reuse tail padding: VLA dimension metadata is meaningful only for array
     * and pointer Values, which can never be bit-field member descriptors. */
    unsigned short VlaDimMask;
};


/* hash table data structure */
struct TableEntry
{
    struct TableEntry *Next;        /* next item in this hash chain */
    const char *DeclFileName;       /* where the variable was declared */
    unsigned short DeclLine;
    unsigned short DeclColumn;

    union TableEntryPayload
    {
        struct ValueEntry
        {
            char *Key;              /* points to the shared string table */
            struct Value *Val;      /* the value we're storing */
        } v;                        /* used for tables of values */

        char Key[1];                /* dummy size - used for the shared string table */

        struct BreakpointEntry      /* defines a breakpoint */
        {
            const char *FileName;
            short int Line;
            short int CharacterPos;
        } b;

    } p;
};

struct Table
{
    short Size;
    short OnHeap;
    struct TableEntry **HashTable;
};

/* Shared interpreted-call ABI view.  Arguments remain typed Values owned by the
 * caller's evaluation frame; variadic consumers keep a cursor into this view.
 * This is deliberately independent of host va_list/layout and therefore uses
 * BabyC's platform-neutral Value representation. */
struct CallAbiFrame
{
    struct Value **Arguments;
    int ArgumentCount;
    int FixedArgumentCount;
    int IsVariadic;
};

/* stack frame for function calls */
struct StackFrame
{
    struct ParseState ReturnParser;         /* how we got here */
    const char *FuncName;                   /* the name of the function we're in */
    struct Value *ReturnValue;              /* copy the return value here */
    struct Value **Parameter;               /* array of parameter values */
    int NumParams;                          /* the number of parameters */
    struct CallAbiFrame CallAbi;            /* fixed + promoted variadic arguments */
    struct Table LocalTable;                /* the local variables and parameters */
    struct TableEntry *LocalHashTable[LOCAL_TABLE_SIZE];
    struct StackFrame *PreviousStackFrame;  /* the next lower stack frame */
};

/* lexer state */
enum LexMode
{
    LexModeNormal,
    LexModeHashInclude,
    LexModeHashDefine,
    LexModeHashDefineSpace,
    LexModeHashDefineSpaceIdent
};

struct LexState
{
    const char *Pos;
    const char *End;
    const char *FileName;
    int Line;
    int CharacterPos;
    const char *SourceText;
    enum LexMode Mode;
    int EmitExtraNewlines;
};

/* library function definition */
struct LibraryFunction
{
    void (*Func)(struct ParseState *Parser, struct Value *, struct Value **, int);
    const char *Prototype;
};

/* output stream-type specific state information */
union OutputStreamInfo
{
    struct StringOutputStream
    {
        struct ParseState *Parser;
        char *WritePos;
    } Str;
};

/* stream-specific method for writing characters to the console */
typedef void CharWriter(unsigned char, union OutputStreamInfo *);

/* used when writing output to a string - eg. sprintf() */
struct OutputStream
{
    CharWriter *Putch;
    union OutputStreamInfo i;
};

/* possible results of parsing a statement */
enum ParseResult { ParseResultEOF, ParseResultError, ParseResultOk };

/* a chunk of heap-allocated tokens we'll cleanup when we're done */
struct CleanupTokenNode
{
    void *Tokens;
    const char *SourceText;
    struct CleanupTokenNode *Next;
};

/* Values temporarily owned by parser error-cleanup paths. */
struct CleanupParseValueNode
{
    struct Value *Value;
    struct CleanupParseValueNode *Next;
};

/* linked list of lexical tokens used in interactive mode */
struct TokenLine
{
    struct TokenLine *Next;
    unsigned char *Tokens;
    int NumBytes;
};


/* a list of libraries we can include */
struct IncludeLibrary
{
    char *IncludeName;
    void (*SetupFunction)(Picoc *pc);
    struct LibraryFunction *FuncList;
    const char *SetupCSource;
    struct IncludeLibrary *NextLib;
};

#define FREELIST_BUCKETS 32                         /* freelists for 4, 8, 12 ... 128 byte allocs (was 8 → 32) */
#define SPLIT_MEM_THRESHOLD 32                      /* don't split memory which is close in size (raised from 16) */
#define BREAKPOINT_TABLE_SIZE 21
/* Bucket index: AllocSize >> 2 maps 4→1, 8→2, ..., 128→32. Bucket 0 unused for safety. */


/* the entire state of the picoc system */
struct ValueProxyEntry
{
    struct Value *Owner;
    struct ValueProxyEntry *Next;
};

#define VLA_BOUND_CACHE_SIZE 64
struct VlaBoundCacheEntry
{
    const unsigned char *TokenBase;
    const unsigned char *SourceText;
    const unsigned char *Pos;
    int TokenIndex;
    unsigned char IsVLA;
    unsigned char Valid;
};

struct Picoc_Struct
{
    struct MacroDef *StdargMacroValues[5]; /* stdarg intrinsic identity, per interpreter */
    struct ValueProxyEntry *ValueProxyList; /* stack-backed aligned scalar proxies */
    /* parser global data */
    struct Table GlobalTable;
    int ExprCollapseTok;            /* operator token that triggered the current ExpressionStackCollapse (TokenNone = end of expression) */
    struct CleanupTokenNode *CleanupTokenList;
    struct CleanupParseValueNode *CleanupParseValueList;
    /* Scratch allocations owned by an in-progress LexTokenise call. LexFail
     * runs outside that function's stack frame, so it needs explicit cleanup. */
    void *LexTokeniseStackScratch;
    int LexTokeniseStackScratchSize;
    void *LexTokeniseLineScratch;
    unsigned short *LexLineTab;
    int LexLineCount;
    const unsigned char *LexTokenBase;
    struct VlaBoundCacheEntry VlaBoundCache[VLA_BOUND_CACHE_SIZE];
    struct TableEntry *GlobalHashTable[GLOBAL_TABLE_SIZE];

    /* lexer global data */
    struct TokenLine *InteractiveHead;
    struct TokenLine *InteractiveTail;
    struct TokenLine *InteractiveCurrentLine;
    int LexUseStatementPrompt;
    union AnyValue LexAnyValue;
    struct Value LexValue;
    struct Table ReservedWordTable;
    struct TableEntry *ReservedWordHashTable[RESERVED_WORD_TABLE_SIZE];

    /* the table of string literal values */
    struct Table StringLiteralTable;
    struct TableEntry *StringLiteralHashTable[STRING_LITERAL_TABLE_SIZE];

    /* the stack */
    struct StackFrame *TopStackFrame;

    /* the value passed to exit() */
    int PicocExitValue;

    /* a list of libraries we can include */
    struct IncludeLibrary *IncludeLibList;

    /* Mutually exclusive guest platform API families: 0 = not selected,
     * 1 = BabyPOSIX, 2 = Windows/libWINAPI.  This is per interpreter. */
    int PlatformSubsystem;

    /* heap memory */
#ifdef USE_MALLOC_STACK
    unsigned char *HeapMemory;          /* stack memory since our heap is malloc()ed */
    void *HeapBottom;                   /* the bottom of the (downward-growing) heap */
    void *StackFrame;                   /* the current stack frame */
    void *HeapStackTop;                 /* the top of the stack */
#else
# ifdef SURVEYOR_HOST
    unsigned char *HeapMemory;          /* all memory - stack and heap */
    void *HeapBottom;                   /* the bottom of the (downward-growing) heap */
    void *StackFrame;                   /* the current stack frame */
    void *HeapStackTop;                 /* the top of the stack */
    void *HeapMemStart;
# else
    unsigned char HeapMemory[HEAP_SIZE];  /* all memory - stack and heap */
    void *HeapBottom;                   /* the bottom of the (downward-growing) heap */
    void *StackFrame;                   /* the current stack frame */
    void *HeapStackTop;                 /* the top of the stack */
# endif
#endif

    struct AllocNode *FreeListBucket[FREELIST_BUCKETS];      /* we keep a pool of freelist buckets to reduce fragmentation */
    struct AllocNode *FreeListBig;                           /* free memory which doesn't fit in a bucket */

    /* types */
    struct ValueType UberType;
    struct ValueType IntType;
    struct ValueType ShortType;
    struct ValueType CharType;
    struct ValueType LongType;
    struct ValueType UnsignedIntType;
    struct ValueType UnsignedShortType;
    struct ValueType UnsignedLongType;
    struct ValueType UnsignedLongLongType;
    struct ValueType LongLongType;
    struct ValueType UnsignedCharType;
    #ifndef NO_FP
    struct ValueType FPType;
    struct ValueType FloatType; /* binary32 objects, double-sized execution temporary */
    #endif
    struct ValueType VoidType;
    struct ValueType TypeType;
    struct ValueType FunctionType;
    struct ValueType MacroType;
    struct ValueType EnumType;
    struct ValueType GotoLabelType;
    struct ValueType *CharPtrType;
    struct ValueType *CharPtrPtrType;
    struct ValueType *CharArrayType;
    struct ValueType *VoidPtrType;

    /* debugger */
    struct Table BreakpointTable;
    struct TableEntry *BreakpointHashTable[BREAKPOINT_TABLE_SIZE];
    int BreakpointCount;
    int DebugManualBreak;
    int DebugStepMode;         /* if non-zero, break on the next statement (single-step) */
    char *DebugLastFileName;   /* last file parsed, used by set_breakpoint() from interactive */

    /* C library */
    int BigEndian;
    int LittleEndian;

    IOFILE *CStdOut;
    IOFILE CStdOutBase;

    /* the picoc version string */
    const char *VersionString;

    /* exit longjump buffer */
#if defined(UNIX_HOST) || defined(WIN32)
    jmp_buf PicocExitBuf;
#endif
#ifdef SURVEYOR_HOST
    int PicocExitBuf[41];
#endif
    /* 1 after PicocPlatformSetExitPoint arms setjmp; 0 after longjmp returns
     * or before any setjmp. PlatformExit longjmps only when set — otherwise
     * exit(1). Avoids SEGV on unarmed/corrupt JumpBuf; keeps stdint include
     * recovery via longjmp when the CLI has armed the exit point. */
    int PicocExitPointSet;

    /* string table */
    struct Table StringTable;
    struct TableEntry *StringHashTable[STRING_TABLE_SIZE];
    char *StrEmpty;

    int NextScopeID;            /* contatore monotono, inizializzato a 1 da PicocInitialise via memset+1 */
    /* VariableGet hot-path cache: multiple slots per frame (invalidated on define/scope/frame change).
     * Covers loop bodies that touch several locals (s, i, n, ...) without table walk. */
#define VAR_CACHE_SLOTS 16
    struct {
        const char *Ident;
        struct Value *Value;
        struct StackFrame *Frame;
    } VarCache[VAR_CACHE_SLOTS];
    unsigned char VarCacheNext; /* round-robin insert index */


    FILE* StdoutValue;   /* stdout per-istanza - sostituisce la statica in stdio.c */
    FILE* StderrValue;   /* stderr per-istanza */
    FILE* StdinValue;    /* stdin  per-istanza */
};

/* table.c */
void TableInit(Picoc *pc);
char *TableStrRegister(Picoc *pc, const char *Str);
char *TableStrRegister2(Picoc *pc, const char *Str, int Len);
void TableInitTable(struct Table *Tbl, struct TableEntry **HashTable, int Size, int OnHeap);
int TableSet(Picoc *pc, struct Table *Tbl, char *Key, struct Value *Val, const char *DeclFileName, int DeclLine, int DeclColumn);
int TableGet(struct Table *Tbl, const char *Key, struct Value **Val, const char **DeclFileName, int *DeclLine, int *DeclColumn);
struct Value *TableDelete(Picoc *pc, struct Table *Tbl, const char *Key);
char *TableSetIdentifier(Picoc *pc, struct Table *Tbl, const char *Ident, int IdentLen);
void TableStrFree(Picoc *pc);

/* lex.c */
void LexInit(Picoc *pc);
void LexCleanup(Picoc *pc);
void *LexAnalyse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int *TokenLen);
void LexInitParser(struct ParseState *Parser, Picoc *pc, const char *SourceText, void *TokenSource, char *FileName, int RunIt, int SetDebugMode);
void LexPrintStats(void);
void LexDumpTokens(FILE *Out, const void *Tokens, int TokenLen);
enum LexToken LexGetToken(struct ParseState *Parser, struct Value **Value, int IncPos);
enum LexToken LexRawPeekToken(struct ParseState *Parser);
void LexToEndOfLine(struct ParseState *Parser);
void *LexCopyTokens(struct ParseState *StartParser, struct ParseState *EndParser);
void LexInteractiveClear(Picoc *pc, struct ParseState *Parser);
void LexInteractiveCompleted(Picoc *pc, struct ParseState *Parser);
void LexInteractiveStatementPrompt(Picoc *pc);

/* parse.c */
/* the following are defined in picoc.h:
 * void PicocParse(const char *FileName, const char *Source, int SourceLen, int RunIt, int CleanupNow, int CleanupSource);
 * void PicocParseInteractive(); */
void PicocParseInteractiveNoStartPrompt(Picoc *pc, int EnableDebugger);
enum ParseResult ParseStatement(struct ParseState *Parser, int CheckTrailingSemicolon);
struct Value *ParseFunctionDefinition(struct ParseState *Parser, struct ValueType *ReturnType, char *Identifier);
void PicocPrintIrStats(void);
void ParseCleanup(Picoc *pc);
void ParserCopyPos(struct ParseState *To, struct ParseState *From);
int ParserSetDiagnosticCaret(struct ParseState *Parser, const char *BaseIdent,
                             const char *MemberIdent);
void ParserCopy(struct ParseState *To, struct ParseState *From);

/* expression.c */
int ExpressionParse(struct ParseState *Parser, struct Value **Result);
int ExpressionParseEx(struct ParseState *Parser, struct Value **Result, int AllowComma);
PTR_TYPE_INT_SIGNED ExpressionParseInt(struct ParseState *Parser);
void ExpressionAssign(struct ParseState *Parser, struct Value *DestValue, struct Value *SourceValue, int Force, const char *FuncName, int ParamNo, int AllowPointerCoercion);
PTR_TYPE_INT_SIGNED ExpressionCoerceInteger(struct Value *Val);
void ExpressionMarkInitialized(struct Value *Dest);
void ExpressionMarkBufferWritten(struct Value *DestArg, void *Buf, int Len);
void ExpressionMarkPtrArgEscaped(struct Value *Arg);
/* Call library Intrinsic with SEGV/BUS protection */
void PicocCallIntrinsic(struct ParseState *Parser,
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int),
    struct Value *ReturnValue, struct Value **Param, int NumArgs,
    const char *FuncName);

/* FormatPure sprintf magic-box cache (clibrary.c) */
int FmtCacheEnabled(void);
int FmtCacheLookup(const char *Fmt, long long *Args, int N, char *Dst, int DstCap);
void FmtCacheStore(const char *Fmt, long long *Args, int N, const char *Out);
int FmtCacheCollectIntArgs(struct Value **Param, int NumArgs, long long *Out, int MaxOut);
int FmtCacheCollectIntArgsEx(struct Value **Param, int NumArgs, int FirstDataArg,
                             long long *Out, int MaxOut);

PTR_TYPE_INT ExpressionAssignInt(struct ParseState *Parser, struct Value *DestValue, PTR_TYPE_INT_SIGNED FromInt, int After);
PTR_TYPE_INT ExpressionCoerceUnsignedInteger(struct Value *Val);
struct ExpressionStack; void ExpressionPushLongLong(struct ParseState *Parser, struct ExpressionStack **StackTop, long long LLValue);
void ExpressionPushUnsignedLongLong(struct ParseState* Parser, struct ExpressionStack** StackTop, unsigned long long ULLValue);
long long ExpressionAssignLongLong(struct ParseState *Parser, struct Value *DestValue, long long FromLL, int After);
#ifndef NO_FP
double ExpressionCoerceFP(struct Value *Val);
#endif

/* type.c */
void TypeInit(Picoc *pc);
void TypeCleanup(Picoc *pc);
int TypeSize(struct ValueType *Typ, int ArraySize, int Compact);
/* C object layout is independent of execution engine and temporary slot size. */
int TypeObjectSize(const struct ValueType *Typ);
int TypeObjectElementSize(const struct ValueType *Typ);
int TypeFPStorageSize(const struct ValueType *Typ);
int TypeFPStorageAlign(const struct ValueType *Typ);
int TypeSizeValue(struct Value *Val, int Compact);
int TypeContainsUnion(struct ValueType *Typ);
int ExpressionInitLeafAddr(struct Value *Root, void *Addr, int Size);
void ExpressionInitMarkAddr(struct Value *Root, void *Addr, int Size);
int IsTypeToken(struct ParseState *Parser, enum LexToken t, struct Value *LexValue);
int TypeInitTrackable(struct ValueType *Typ);
int TypeStackSizeValue(struct Value *Val);
int TypeLastAccessibleOffset(Picoc *pc, struct Value *Val);
int TypeParseFront(struct ParseState *Parser, struct ValueType **Typ, int *IsStatic);
void TypeParseIdentPart(struct ParseState *Parser, struct ValueType *BasicTyp, struct ValueType **Typ, char **Identifier);
void TypeParse(struct ParseState *Parser, struct ValueType **Typ, char **Identifier, int *IsStatic);
struct ValueType *TypeParseBack(struct ParseState *Parser, struct ValueType *FromType);
struct ValueType *TypeAdd(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int Sizeof, int AlignBytes);
struct ValueType *TypeGetMatching(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int AllowDuplicates);
struct ValueType *TypeCreateOpaqueStruct(Picoc *pc, struct ParseState *Parser, const char *StructName, int Size);
int TypeIsForwardDeclared(struct ParseState *Parser, struct ValueType *Typ);

/* heap.c */
void HeapInit(Picoc *pc, int StackSize);
void HeapCleanup(Picoc *pc);
void *HeapAllocStack(Picoc *pc, int Size);
int HeapPopStack(Picoc *pc, void *Addr, int Size);
void HeapUnpopStack(Picoc *pc, int Size);
void HeapPushStackFrame(Picoc *pc);
int HeapPopStackFrame(Picoc *pc);
void *HeapAllocMem(Picoc *pc, int Size);
void HeapFreeMem(Picoc *pc, void *Mem);

/* variable.c */
void VariableInit(Picoc *pc);
void VariableCleanup(Picoc *pc);
void VariableFree(Picoc *pc, struct Value *Val);
void VariableTableCleanup(Picoc *pc, struct Table *HashTable);
void *VariableAlloc(Picoc *pc, struct ParseState *Parser, int Size, int OnHeap);
void VariableStackPop(struct ParseState *Parser, struct Value *Var);
struct Value *VariableAllocValueAndData(Picoc *pc, struct ParseState *Parser, int DataSize, int IsLValue, struct Value *LValueFrom, int OnHeap);
struct Value *VariableAllocValueAndCopy(Picoc *pc, struct ParseState *Parser, struct Value *FromValue, int OnHeap);
struct Value *VariableAllocValueFromType(Picoc *pc, struct ParseState *Parser, struct ValueType *Typ, int IsLValue, struct Value *LValueFrom, int OnHeap);
struct Value *VariableAllocValueFromExistingData(struct ParseState *Parser, struct ValueType *Typ, union AnyValue *FromValue, int IsLValue, struct Value *LValueFrom);
void VariableValueRefresh(struct Value *Val);
void VariableValueCommit(struct Value *Val);
struct Value *VariableAllocValueShared(struct ParseState *Parser, struct Value *FromValue);
struct Value *VariableDefine(Picoc *pc, struct ParseState *Parser, char *Ident, struct Value *InitValue, struct ValueType *Typ, int MakeWritable);
void VariableBindParams(Picoc *pc, struct ParseState *Parser,
    char **ParamNames, struct Value **ParamValues, int NumParams);
struct Value *VariableDefineButIgnoreIdentical(struct ParseState *Parser, char *Ident, struct ValueType *Typ, int IsStatic, int *FirstVisit);
int VariableDefined(Picoc *pc, const char *Ident);
int VariableDefinedAndOutOfScope(Picoc *pc, const char *Ident);
void VariableRealloc(struct ParseState *Parser, struct Value *FromValue, int NewSize);
void VariableCacheInvalidate(Picoc *pc);
void VariableGet(Picoc *pc, struct ParseState *Parser, const char *Ident, struct Value **LVal);
void IrExecuteFunctionBody(struct ParseState *Parser, struct Value *FuncValue, struct Value **ParamArray, int ArgCount);
void VariableDefinePlatformVar(Picoc *pc, struct ParseState *Parser, char *Ident, struct ValueType *Typ, union AnyValue *FromValue, int IsWritable);
void VariableStackFrameAdd(struct ParseState *Parser, const char *FuncName, int NumParams);
void VariableStackFramePop(struct ParseState *Parser);
struct Value *VariableStringLiteralGet(Picoc *pc, char *Ident);
void VariableStringLiteralDefine(Picoc *pc, char *Ident, struct Value *Val);
int VariableAddressIsStringLiteral(Picoc *pc, const void *Addr);
/* Pedagogic heap tracker (cstdlib/stdlib.c) */
int BabycHeapIsFreed(const void *Addr);
int BabycHeapLookupLive(const void *Addr, void **BaseOut, size_t *SizeOut);
void BabycHeapCheckWrite(struct ParseState *Parser, void *Addr, int NBytes);
void *VariableDereferencePointer(struct ParseState *Parser, struct Value *PointerValue, struct Value **DerefVal, int *DerefOffset, struct ValueType **DerefType, int *DerefIsLValue);
int VariableScopeBegin(struct ParseState * Parser, int* PrevScopeID);
void VariableScopeEnd(struct ParseState * Parser, int ScopeID, int PrevScopeID);

/* clibrary.c */
void BasicIOInit(Picoc *pc);
void LibraryInit(Picoc *pc);
void LibraryAdd(Picoc *pc, struct Table *GlobalTable, const char *LibraryName, struct LibraryFunction *FuncList);
void CLibraryInit(Picoc *pc);
void PrintCh(char OutCh, IOFILE *Stream);
void PrintSimpleInt(long Num, IOFILE *Stream);
void PrintInt(long Num, int FieldWidth, int ZeroPad, int LeftJustify, IOFILE *Stream);
void PrintStr(const char *Str, IOFILE *Stream);
void PrintFP(double Num, IOFILE *Stream);
void PrintType(struct ValueType *Typ, IOFILE *Stream);
void PrintValue(struct Value *Val, IOFILE *Stream);
void LibPrintf(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs);

/* platform.c */
/* the following are defined in picoc.h:
 * void PicocCallMain(int argc, char **argv);
 * int PicocPlatformSetExitPoint();
 * void PicocInitialise(int StackSize);
 * void PicocCleanup();
 * void PicocPlatformScanFile(const char *FileName);
 * extern int PicocExitValue; */
#if defined(__GNUC__) || defined(__clang__)
void ProgramFail(struct ParseState *Parser, const char *Message, ...) __attribute__((noreturn));
void ProgramFailNoParser(Picoc *pc, const char *Message, ...) __attribute__((noreturn));
#else
void ProgramFail(struct ParseState *Parser, const char *Message, ...);
void ProgramFailNoParser(Picoc *pc, const char *Message, ...);
#endif
void AssignFail(struct ParseState *Parser, const char *Format, struct ValueType *Type1, struct ValueType *Type2, int Num1, int Num2, const char *FuncName, int ParamNo);
void LexFail(Picoc *pc, struct LexState *Lexer, const char *Message, ...);
void LexCleanupOnFailure(Picoc *pc);
void PlatformInit(Picoc *pc);
void PlatformCleanup(Picoc *pc);
char *PlatformGetLine(char *Buf, int MaxLen, const char *Prompt);
int PlatformGetCharacter();
void PlatformPutc(unsigned char OutCh, union OutputStreamInfo *);
void PlatformPrintf(IOFILE *Stream, const char *Format, ...);
void PlatformVPrintf(IOFILE *Stream, const char *Format, va_list Args);
void PrintSourceTextErrorLine(IOFILE *Stream, const char *FileName, const char *SourceText, int Line, int CharacterPos);
void ParserSetCaretOnLine(struct ParseState *Parser, int PreferCh);
void ParserSetCaretOnStmtEnd(struct ParseState *Parser);
void ParserSetCaretOnIdent(struct ParseState *Parser, const char *Ident);
void ParserSetCaretOnIndexAccess(struct ParseState *Parser, const char *BaseIdent);
void DiagnosticSetIndexAccessLocation(struct ParseState *Parser, const char *BaseIdent, int IndexDepth);
void ParserSetCaretOnAssignTarget(struct ParseState *Parser);
#if defined(__GNUC__) || defined(__clang__)
void PlatformExit(Picoc *pc, int ExitVal) __attribute__((noreturn));
#else
void PlatformExit(Picoc *pc, int ExitVal);
#endif
char *PlatformMakeTempName(Picoc *pc, char *TempNameBuffer);
void PlatformLibraryInit(Picoc *pc);

/* include.c */
void IncludeInit(Picoc *pc);
void IncludeCleanup(Picoc *pc);
void IncludeRegister(Picoc *pc, const char *IncludeName, void (*SetupFunction)(Picoc *pc), struct LibraryFunction *FuncList, const char *SetupCSource);
void IncludeFile(Picoc *pc, char *Filename, struct ParseState *Parser);
void PicocResolveInteractivePlatformFunction(Picoc *pc, const char *name, struct ParseState *Parser);
/* the following is defined in picoc.h:
 * void PicocIncludeAllSystemHeaders(); */

/* debug.c */
void DebugInit(Picoc *pc);
void DebugCleanup(Picoc *pc);
void DebugCheckStatement(struct ParseState *Parser);
void DebugSetBreakpoint(struct ParseState *Parser);
void DebugSetBreakpointAtLine(Picoc *pc, const char *FileName, int Line);
int  DebugClearBreakpoint(struct ParseState *Parser);
void DebugClearAllBreakpoints(Picoc *pc);
void DebugListBreakpoints(Picoc *pc);
void DebugStep(Picoc *pc);

/* stdio.c */
extern const char StdioDefs[];
extern struct LibraryFunction StdioFunctions[];
void StdioSetupFunc(Picoc *pc);

/* math.c */
extern struct LibraryFunction MathFunctions[];
void MathSetupFunc(Picoc *pc);

/* string.c */
extern struct LibraryFunction StringFunctions[];
void StringSetupFunc(Picoc *pc);

/* stdlib.c */
extern struct LibraryFunction StdlibFunctions[];
void StdlibSetupFunc(Picoc *pc);

/* time.c */
extern const char StdTimeDefs[];
extern struct LibraryFunction StdTimeFunctions[];
void StdTimeSetupFunc(Picoc *pc);

/* errno.c */
void StdErrnoSetupFunc(Picoc *pc);

/* ctype.c */
extern struct LibraryFunction StdCtypeFunctions[];

/* stdbool.c */
extern const char StdboolDefs[];
void StdboolSetupFunc(Picoc *pc);

/* stdint.c */
extern const char StdintDefs[];
void StdintSetupFunc(Picoc *pc);

/* unistd.c */
extern const char UnistdDefs[];
extern struct LibraryFunction UnistdFunctions[];
void UnistdSetupFunc(Picoc *pc);

#endif /* INTERPRETER_H */

#ifdef WIN32
/* win.c */
extern const char WinDefs[];
extern struct LibraryFunction WinFunctions[];
void WinSetupFunc(Picoc* pc);
#endif

#ifdef PERSOPORT
void PrintHelp( char * filename, struct LibraryFunction DefFunctions[], const char * Defs ) ;

/* dirent.c */
extern char DirentDefs[];
void InitDirentDefs(void) ;
extern struct LibraryFunction DirentFunctions[];
void DirentSetupFunc(Picoc *pc);

/* mini_h.c */
extern const char MiniDefs[];
extern struct LibraryFunction MiniFunctions[];
void MiniSetupFunc(Picoc *pc);

/* tools.c */
extern const char ToolsDefs[];
extern struct LibraryFunction ToolsFunctions[];
void ToolsSetupFunc(Picoc *pc);

/* popen.c */
extern const char PopenDefs[];
extern struct LibraryFunction PopenFunctions[];
void PopenSetupFunc(Picoc *pc);

/* types.c */
extern const char TypesDefs[];
extern struct LibraryFunction TypesFunctions[];
void TypesSetupFunc(Picoc *pc);

/* stat.c */
extern const char StatDefs[];
extern struct LibraryFunction StatFunctions[];
void StatSetupFunc(Picoc *pc);

/* various.c */
extern const char VariousDefs[];
extern struct LibraryFunction VariousFunctions[];
void VariousSetupFunc(Picoc *pc);

/* regex.c */
extern const char RegexDefs[];
extern struct LibraryFunction RegexFunctions[];
void RegexSetupFunc(Picoc *pc);

/* socket.c */
extern const char SocketDefs[];
extern struct LibraryFunction SocketFunctions[];
void SocketSetupFunc(Picoc *pc);

#endif
