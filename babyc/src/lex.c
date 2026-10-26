/* picoc lexer - converts source text into a tokenised form */ 

#include "interpreter.h"

#ifdef BABYC_LEX_STATS
/* TOKEN_PATH_V2 Fase 0: counters (compile with -DBABYC_LEX_STATS) */
static unsigned long long LexStatGetToken;
static unsigned long long LexStatGetRaw;
static unsigned long long LexStatPeek;       /* IncPos==0 */
static unsigned long long LexStatConsume;   /* IncPos!=0 */
static unsigned long long LexStatSkipMode;  /* GetToken while RunModeSkip */
static unsigned long long LexStatEOL;       /* TokenEndOfLine skipped in GetRaw */

void LexPrintStats(void)
{
    extern unsigned long long ParserCopyStatGet(void);
    fprintf(stderr,
        "[lex-stats] GetToken=%llu GetRaw=%llu peek=%llu consume=%llu skipMode=%llu EOL=%llu ParserCopy=%llu\n",
        LexStatGetToken, LexStatGetRaw, LexStatPeek, LexStatConsume,
        LexStatSkipMode, LexStatEOL, ParserCopyStatGet());
}
#else
void LexPrintStats(void) {}
#endif

#include <limits.h>
#include <stddef.h>

#ifdef NO_CTYPE
#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isalnum(c) (isalpha(c) || isdigit(c))
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#endif
#define isCidstart(c) (isalpha(c) || (c)=='_' || (c)=='#')
#define isCident(c) (isalnum(c) || (c)=='_')

#define IS_HEX_ALPHA_DIGIT(c) (((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define IS_BASE_DIGIT(c,b) (((c) >= '0' && (c) < '0' + (((b)<10)?(b):10)) || (((b) > 10) ? IS_HEX_ALPHA_DIGIT(c) : FALSE))
#define GET_BASE_DIGIT(c) (((c) <= '9') ? ((c) - '0') : (((c) <= 'F') ? ((c) - 'A' + 10) : ((c) - 'a' + 10)))

#define NEXTIS(c,x,y) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else GotToken = (y); }
#define NEXTIS3(c,x,d,y,z) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else NEXTIS(d,y,z) }
#define NEXTIS4(c,x,d,y,e,z,a) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else NEXTIS3(d,y,e,z,a) }
#define NEXTIS3PLUS(c,x,d,y,e,z,a) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else if (NextChar == (d)) { if (Lexer->Pos[1] == (e)) { LEXER_INCN(Lexer, 2); GotToken = (z); } else { LEXER_INC(Lexer); GotToken = (y); } } else GotToken = (a); }
#define NEXTISEXACTLY3(c,d,y,z) { if (NextChar == (c) && Lexer->Pos[1] == (d)) { LEXER_INCN(Lexer, 2); GotToken = (y); } else GotToken = (z); }

#define LEXER_INC(l) ( (l)->Pos++, (l)->CharacterPos++ )
#define LEXER_INCN(l, n) ( (l)->Pos+=(n), (l)->CharacterPos+=(n) )
#define TOKEN_DATA_OFFSET 2

#define MAX_CHAR_VALUE 255      /* maximum value which can be represented by a "char" data type */


struct ReservedWord
{
    const char *Word;
    enum LexToken Token;
};

static struct ReservedWord ReservedWords[] =
{
    { "#define", TokenHashDefine },
    { "#else", TokenHashElse },
    { "#endif", TokenHashEndif },
    { "#if", TokenHashIf },
    { "#ifdef", TokenHashIfdef },
    { "#ifndef", TokenHashIfndef },
    { "#include", TokenHashInclude },
    { "auto", TokenAutoType },
    { "break", TokenBreak },
    { "case", TokenCase },
    { "char", TokenCharType },
    { "continue", TokenContinue },
    { "const", TokenConstType },
    { "volatile", TokenVolatileType },
    { "default", TokenDefault },
    { "delete", TokenDelete },
    { "do", TokenDo },
#ifndef NO_FP
    { "double", TokenDoubleType },
#endif
    { "else", TokenElse },
    { "enum", TokenEnumType },
    { "extern", TokenExternType },
#ifndef NO_FP
    { "float", TokenFloatType },
#endif
    { "for", TokenFor },
    { "goto", TokenGoto },
    { "if", TokenIf },
    { "int", TokenIntType },
    { "_Bool", TokenIntType },
    { "long", TokenLongType },
    { "new", TokenNew },
    { "register", TokenRegisterType },
    { "return", TokenReturn },
    { "short", TokenShortType },
    { "signed", TokenSignedType },
    { "sizeof", TokenSizeof },
    { "offsetof", TokenOffsetof },
    { "static", TokenStaticType },
    { "struct", TokenStructType },
    { "switch", TokenSwitch },
    { "typedef", TokenTypedef },
    { "union", TokenUnionType },
    { "unsigned", TokenUnsignedType },
    { "void", TokenVoidType },
    { "while", TokenWhile }
};



/* initialise the lexer */
void LexInit(Picoc *pc)
{
    int Count;
    
    TableInitTable(&pc->ReservedWordTable, &pc->ReservedWordHashTable[0], sizeof(ReservedWords) / sizeof(struct ReservedWord) * 2, TRUE);

    for (Count = 0; Count < sizeof(ReservedWords) / sizeof(struct ReservedWord); Count++)
    {
        TableSet(pc, &pc->ReservedWordTable, TableStrRegister(pc, ReservedWords[Count].Word), (struct Value *)&ReservedWords[Count], NULL, 0, 0);
    }
    
    pc->LexValue.Typ = NULL;
    pc->LexValue.Val = &pc->LexAnyValue;
    pc->LexValue.LValueFrom = FALSE;
    pc->LexValue.ValOnHeap = FALSE;
    pc->LexValue.ValOnStack = FALSE;
    pc->LexValue.AnyValOnHeap = FALSE;
    pc->LexValue.IsLValue = FALSE;
}

/* deallocate */
void LexCleanup(Picoc *pc)
{
    int Count;

    LexInteractiveClear(pc, NULL);

    for (Count = 0; Count < sizeof(ReservedWords) / sizeof(struct ReservedWord); Count++)
        TableDelete(pc, &pc->ReservedWordTable, TableStrRegister(pc, ReservedWords[Count].Word));
}

/* check if a word is a reserved word - used while scanning */
enum LexToken LexCheckReservedWord(Picoc *pc, const char *Word)
{
    struct Value *val;
    
    if (TableGet(&pc->ReservedWordTable, Word, &val, NULL, NULL, NULL))
        return ((struct ReservedWord *)val)->Token;
    else
        return TokenNone;
}

/* get a numeric literal - used while scanning */
enum LexToken LexGetNumber(Picoc* pc, struct LexState* Lexer, struct Value* Value)
{
    unsigned long long Result = 0;
    int Base = 10;
    enum LexToken ResultToken;
#ifndef NO_FP
    double FPResult;
    double FPDiv;
#endif
    int IsUnsigned = 0;
    int IsLong = 0;
    int IsLongLong = 0;

    if (*Lexer->Pos == '0')
    {
        /* a binary, octal or hex literal */
        LEXER_INC(Lexer);
        if (Lexer->Pos != Lexer->End)
        {
            if (*Lexer->Pos == 'x' || *Lexer->Pos == 'X')
            {
                Base = 16; LEXER_INC(Lexer);
            }
            else if (*Lexer->Pos == 'b' || *Lexer->Pos == 'B')
            {
                Base = 2; LEXER_INC(Lexer);
            }
            else if (*Lexer->Pos != '.')
                Base = 8;
        }
    }

    /* get the value */
    for (; Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, Base); LEXER_INC(Lexer))
        Result = Result * Base + GET_BASE_DIGIT(*Lexer->Pos);

    /* parse suffixes */
    while (Lexer->Pos != Lexer->End)
    {
        if (*Lexer->Pos == 'u' || *Lexer->Pos == 'U')
        {
            IsUnsigned = 1;
            LEXER_INC(Lexer);
        }
        else if (*Lexer->Pos == 'l' || *Lexer->Pos == 'L')
        {
            LEXER_INC(Lexer);
            if (!IsLong)
                IsLong = 1;
            else
            {
                IsLongLong = 1;
                IsLong = 0;
            }
        }
        else
            break;
    }

    /* determine the type and store the value (C99 6.4.4.1).
     * Always write through the 64-bit field so token pack/unpack
     * (memcpy of sizeof(long long)) sees a fully defined value. */
    if (IsLongLong)
    {
        if (IsUnsigned) {
            Value->Typ = &pc->UnsignedLongLongType;
            Value->Val->UnsignedLongLongInteger = Result;
        } else {
            Value->Typ = &pc->LongLongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
    }
    else if (IsLong)
    {
        if (IsUnsigned)
        {
            if (Result <= (unsigned long long)ULONG_MAX)
            {
                Value->Typ = &pc->UnsignedLongType;
                Value->Val->LongLongInteger = (long long)Result;
            }
            else
            {
                Value->Typ = &pc->UnsignedLongLongType;
                Value->Val->UnsignedLongLongInteger = Result;
            }
        }
        else
        {
            /* decimal or non-decimal with L: long → long long */
            if (Result <= (unsigned long long)LONG_MAX)
            {
                Value->Typ = &pc->LongType;
                Value->Val->LongLongInteger = (long long)Result;
            }
            else
            {
                Value->Typ = &pc->LongLongType;
                Value->Val->LongLongInteger = (long long)Result;
            }
        }
    }
    else if (IsUnsigned)
    {
        /* u suffix, no l: unsigned int → unsigned long → unsigned long long */
        if (Result <= (unsigned long long)UINT_MAX)
        {
            Value->Typ = &pc->UnsignedIntType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)ULONG_MAX)
        {
            Value->Typ = &pc->UnsignedLongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else
        {
            Value->Typ = &pc->UnsignedLongLongType;
            Value->Val->UnsignedLongLongInteger = Result;
        }
    }
    else if (Base == 10)
    {
        /* unsuffixed decimal: int → long → long long */
        if (Result <= (unsigned long long)INT_MAX)
        {
            Value->Typ = &pc->IntType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)LONG_MAX)
        {
            Value->Typ = &pc->LongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else
        {
            Value->Typ = &pc->LongLongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
    }
    else
    {
        /* unsuffixed octal/hex: int → uint → long → ulong → ll → ull */
        if (Result <= (unsigned long long)INT_MAX)
        {
            Value->Typ = &pc->IntType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)UINT_MAX)
        {
            Value->Typ = &pc->UnsignedIntType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)LONG_MAX)
        {
            Value->Typ = &pc->LongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)ULONG_MAX)
        {
            Value->Typ = &pc->UnsignedLongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else if (Result <= (unsigned long long)LLONG_MAX)
        {
            Value->Typ = &pc->LongLongType;
            Value->Val->LongLongInteger = (long long)Result;
        }
        else
        {
            Value->Typ = &pc->UnsignedLongLongType;
            Value->Val->UnsignedLongLongInteger = Result;
        }
    }

    ResultToken = TokenIntegerConstant;

    if (Lexer->Pos == Lexer->End)
        return ResultToken;

#ifndef NO_FP
    if (*Lexer->Pos != '.' && *Lexer->Pos != 'e' && *Lexer->Pos != 'E')
        return ResultToken;

    /* floating point constant - we need to convert */
    Value->Typ = &pc->FPType;
    FPResult = (double)Result;

    if (*Lexer->Pos == '.')
    {
        LEXER_INC(Lexer);
        for (FPDiv = 1.0 / Base; Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, Base); LEXER_INC(Lexer), FPDiv /= (double)Base)
        {
            FPResult += GET_BASE_DIGIT(*Lexer->Pos) * FPDiv;
        }
    }

    if (Lexer->Pos != Lexer->End && (*Lexer->Pos == 'e' || *Lexer->Pos == 'E'))
    {
        int ExponentSign = 1;

        LEXER_INC(Lexer);
        if (Lexer->Pos != Lexer->End && *Lexer->Pos == '-')
        {
            ExponentSign = -1;
            LEXER_INC(Lexer);
        }

        /* re-use Result for exponent */
        unsigned long long ExpVal = 0;
        while (Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, 10))
        {
            ExpVal = ExpVal * 10 + GET_BASE_DIGIT(*Lexer->Pos);
            LEXER_INC(Lexer);
        }

        FPResult *= pow((double)Base, (double)ExpVal * ExponentSign);
    }

    Value->Val->FP = FPResult;

    /* C floating suffix is part of the literal type, not just decoration.
     * Preserve binary32 in the token stream and round the literal once, here. */
    if (Lexer->Pos != Lexer->End && (*Lexer->Pos == 'f' || *Lexer->Pos == 'F'))
    {
        float Fp32;
        LEXER_INC(Lexer);
        Fp32 = (float)FPResult;
        Value->Typ = &pc->FloatType;
        Value->Val->FP = (double)Fp32;
    }
    else
    {
        Value->Typ = &pc->FPType;
        Value->Val->FP = FPResult;
    }

    return TokenFPConstant;
#else
    return ResultToken;
#endif
}

/* get a reserved word or identifier - used while scanning */
enum LexToken LexGetWord(Picoc *pc, struct LexState *Lexer, struct Value *Value)
{
    const char *StartPos = Lexer->Pos;
    enum LexToken Token;
    
    do {
        LEXER_INC(Lexer);
    } while (Lexer->Pos != Lexer->End && isCident((int)*Lexer->Pos));
    
    Value->Typ = NULL;
    Value->Val->Identifier = TableStrRegister2(pc, StartPos, Lexer->Pos - StartPos);
    
    Token = LexCheckReservedWord(pc, Value->Val->Identifier);
    switch (Token)
    {
        case TokenHashInclude: Lexer->Mode = LexModeHashInclude; break;
        case TokenHashDefine: Lexer->Mode = LexModeHashDefine; break;
        default: break;
    }
        
    if (Token != TokenNone)
        return Token;
    
    if (Lexer->Mode == LexModeHashDefineSpace)
        Lexer->Mode = LexModeHashDefineSpaceIdent;
    
    return TokenIdentifier;
}

/* unescape a character from an octal character constant */
unsigned char LexUnEscapeCharacterConstant(const char **From, const char *End, unsigned char FirstChar, int Base)
{
    unsigned char Total = GET_BASE_DIGIT(FirstChar);
    int CCount;
    for (CCount = 0; IS_BASE_DIGIT(**From, Base) && CCount < 2; CCount++, (*From)++)
        Total = Total * Base + GET_BASE_DIGIT(**From);
    
    return Total;
}

/* unescape a character from a string or character constant */
unsigned char LexUnEscapeCharacter(const char **From, const char *End)
{
    unsigned char ThisChar;
    
    while ( *From != End && **From == '\\' && 
            &(*From)[1] != End && (*From)[1] == '\n' )
        (*From) += 2;       /* skip escaped end of lines with LF line termination */
    
    while ( *From != End && **From == '\\' && 
            &(*From)[1] != End && &(*From)[2] != End && (*From)[1] == '\r' && (*From)[2] == '\n')
        (*From) += 3;       /* skip escaped end of lines with CR/LF line termination */
    
    if (*From == End)
        return '\\';
    
    if (**From == '\\')
    { 
        /* it's escaped */
        (*From)++;
        if (*From == End)
            return '\\';
        
        ThisChar = *(*From)++;
        switch (ThisChar)
        {
            case '\\': return '\\'; 
            case '\'': return '\'';
            case '"':  return '"';
            case 'a':  return '\a';
            case 'b':  return '\b';
            case 'f':  return '\f';
            case 'n':  return '\n';
            case 'r':  return '\r';
            case 't':  return '\t';
            case 'v':  return '\v';
            case '0': case '1': case '2': case '3': return LexUnEscapeCharacterConstant(From, End, ThisChar, 8);
            case 'x': return LexUnEscapeCharacterConstant(From, End, '0', 16);
            default:   return ThisChar;
        }
    }
    else
        return *(*From)++;
}

/* get a string constant - used while scanning */
enum LexToken LexGetStringConstant(Picoc *pc, struct LexState *Lexer, struct Value *Value, char EndChar)
{
    int Escape = FALSE;
    const char *StartPos = Lexer->Pos;
    const char *EndPos;
    char *EscBuf;
    char *EscBufPos;
    char *RegString;
    struct Value *ArrayValue;
    
    while (Lexer->Pos != Lexer->End && (*Lexer->Pos != EndChar || Escape))
    { 
        /* find the end */
        if (Escape)
        {
            /* Line continuation: backslash already consumed; skip CR/LF
             * without a second LEXER_INC (was double-advancing and eating
             * the next line's first char — often the closing quote of
             * GNU-style "...\n\" multi-line strings, e.g. coreutils cat.c). */
            if (*Lexer->Pos == '\r' && Lexer->Pos+1 != Lexer->End
                && Lexer->Pos[1] == '\n')
            {
                Lexer->Pos += 2;
                Lexer->Line++;
                Lexer->CharacterPos = 0;
                Lexer->EmitExtraNewlines++;
                Escape = FALSE;
                continue;
            }
            if (*Lexer->Pos == '\r' || *Lexer->Pos == '\n')
            {
                if (*Lexer->Pos == '\r')
                    Lexer->Pos++;
                if (Lexer->Pos != Lexer->End && *Lexer->Pos == '\n')
                    Lexer->Pos++;
                Lexer->Line++;
                Lexer->CharacterPos = 0;
                Lexer->EmitExtraNewlines++;
                Escape = FALSE;
                continue;
            }
            Escape = FALSE;
        }
        else if (*Lexer->Pos == '\\')
            Escape = TRUE;
            
        LEXER_INC(Lexer);
    }
    EndPos = Lexer->Pos;
    
    EscBuf = HeapAllocStack(pc, EndPos - StartPos);
    if (EscBuf == NULL)
        LexFail(pc, Lexer, DiagnosticMsgOutOfMemory);
    
    for (EscBufPos = EscBuf, Lexer->Pos = StartPos; Lexer->Pos != EndPos;)
        *EscBufPos++ = LexUnEscapeCharacter(&Lexer->Pos, EndPos);
    
    /* try to find an existing copy of this string literal */
    RegString = TableStrRegister2(pc, EscBuf, EscBufPos - EscBuf);
    HeapPopStack(pc, EscBuf, EndPos - StartPos);
    ArrayValue = VariableStringLiteralGet(pc, RegString);
    if (ArrayValue == NULL)
    {
        /* create and store this string literal */
        ArrayValue = VariableAllocValueAndData(pc, NULL, 0, FALSE, NULL, TRUE);
        ArrayValue->Typ = pc->CharArrayType;
        ArrayValue->Val = (union AnyValue *)RegString;
        VariableStringLiteralDefine(pc, RegString, ArrayValue);
    }

    /* create the the pointer for this char* */
    Value->Typ = pc->CharPtrType;
    Value->Val->Pointer = RegString;
    if (*Lexer->Pos == EndChar)
        LEXER_INC(Lexer);
    
    return TokenStringConstant;
}

/* get a character constant - used while scanning */
enum LexToken LexGetCharacterConstant(Picoc *pc, struct LexState *Lexer, struct Value *Value)
{
    Value->Typ = &pc->CharType;
    Value->Val->Character = LexUnEscapeCharacter(&Lexer->Pos, Lexer->End);
    if (Lexer->Pos != Lexer->End && *Lexer->Pos != '\'')
        LexFail(pc, Lexer, "expected \"'\"");
        
    LEXER_INC(Lexer);
    return TokenCharacterConstant;
}

/* skip a comment - used while scanning */
void LexSkipComment(struct LexState *Lexer, char NextChar, enum LexToken *ReturnToken)
{
    if (NextChar == '*')
    {   
        /* conventional C comment */
        while (Lexer->Pos != Lexer->End && (*(Lexer->Pos-1) != '*' || *Lexer->Pos != '/'))
        {
            if (*Lexer->Pos == '\n')
                Lexer->EmitExtraNewlines++;

            LEXER_INC(Lexer);
        }
        
        if (Lexer->Pos != Lexer->End)
            LEXER_INC(Lexer);
        
        Lexer->Mode = LexModeNormal;
    }
    else
    {   
        /* C++ style comment */
        while (Lexer->Pos != Lexer->End && *Lexer->Pos != '\n')
            LEXER_INC(Lexer);
    }
}

/* get a single token from the source - used while scanning */
enum LexToken LexScanGetToken(Picoc *pc, struct LexState *Lexer, struct Value **Value)
{
    char ThisChar;
    char NextChar;
    enum LexToken GotToken = TokenNone;
    
    /* handle cases line multi-line comments or string constants which mess up the line count */
    if (Lexer->EmitExtraNewlines > 0)
    {
        Lexer->EmitExtraNewlines--;
        return TokenEndOfLine;
    }
    
    /* scan for a token */
    do
    {
        *Value = &pc->LexValue;
        while (Lexer->Pos != Lexer->End && isspace((int)*Lexer->Pos))
        {
            if (*Lexer->Pos == '\n')
            {
                Lexer->Line++;
                Lexer->Pos++;
                Lexer->Mode = LexModeNormal;
                Lexer->CharacterPos = 0;
                return TokenEndOfLine;
            }
            else if (Lexer->Mode == LexModeHashDefine || Lexer->Mode == LexModeHashDefineSpace)
                Lexer->Mode = LexModeHashDefineSpace;
            
            else if (Lexer->Mode == LexModeHashDefineSpaceIdent)
                Lexer->Mode = LexModeNormal;
    
            LEXER_INC(Lexer);
        }
        
        if (Lexer->Pos == Lexer->End || *Lexer->Pos == '\0')
            return TokenEOF;
        
        ThisChar = *Lexer->Pos;
        if (isCidstart((int)ThisChar))
            return LexGetWord(pc, Lexer, *Value);
        
        if (isdigit((int)ThisChar))
            return LexGetNumber(pc, Lexer, *Value);
        
        NextChar = (Lexer->Pos+1 != Lexer->End) ? *(Lexer->Pos+1) : 0;
        LEXER_INC(Lexer);
        switch (ThisChar)
        {
            case '"': GotToken = LexGetStringConstant(pc, Lexer, *Value, '"'); break;
            case '\'': GotToken = LexGetCharacterConstant(pc, Lexer, *Value); break;
            case '(': if (Lexer->Mode == LexModeHashDefineSpaceIdent) GotToken = TokenOpenMacroBracket; else GotToken = TokenOpenBracket; Lexer->Mode = LexModeNormal; break;
            case ')': GotToken = TokenCloseBracket; break;
            case '=': NEXTIS('=', TokenEqual, TokenAssign); break;
            case '+': NEXTIS3('=', TokenAddAssign, '+', TokenIncrement, TokenPlus); break;
            case '-': NEXTIS4('=', TokenSubtractAssign, '>', TokenArrow, '-', TokenDecrement, TokenMinus); break;
            case '*': NEXTIS('=', TokenMultiplyAssign, TokenAsterisk); break;
            case '/': if (NextChar == '/' || NextChar == '*') { LEXER_INC(Lexer); LexSkipComment(Lexer, NextChar, &GotToken); } else NEXTIS('=', TokenDivideAssign, TokenSlash); break;
            case '%': NEXTIS('=', TokenModulusAssign, TokenModulus); break;
            case '<': if (Lexer->Mode == LexModeHashInclude) GotToken = LexGetStringConstant(pc, Lexer, *Value, '>'); else { NEXTIS3PLUS('=', TokenLessEqual, '<', TokenShiftLeft, '=', TokenShiftLeftAssign, TokenLessThan); } break; 
            case '>': NEXTIS3PLUS('=', TokenGreaterEqual, '>', TokenShiftRight, '=', TokenShiftRightAssign, TokenGreaterThan); break;
            case ';': GotToken = TokenSemicolon; break;
            case '&': NEXTIS3('=', TokenArithmeticAndAssign, '&', TokenLogicalAnd, TokenAmpersand); break;
            case '|': NEXTIS3('=', TokenArithmeticOrAssign, '|', TokenLogicalOr, TokenArithmeticOr); break;
            case '{': GotToken = TokenLeftBrace; break;
            case '}': GotToken = TokenRightBrace; break;
            case '[': GotToken = TokenLeftSquareBracket; break;
            case ']': GotToken = TokenRightSquareBracket; break;
            case '!': NEXTIS('=', TokenNotEqual, TokenUnaryNot); break;
            case '^': NEXTIS('=', TokenArithmeticExorAssign, TokenArithmeticExor); break;
            case '~': GotToken = TokenUnaryExor; break;
            case ',': GotToken = TokenComma; break;
            case '.': NEXTISEXACTLY3('.', '.', TokenEllipsis, TokenDot); break;
            case '?': GotToken = TokenQuestionMark; break;
            case ':': GotToken = TokenColon; break;
            case '\\':
                /* C line continuation: backslash + newline is ignored
                 * (used heavily in multi-line #define macros). */
                if (NextChar == '\r' || NextChar == '\n')
                {
                    /* Pos already advanced past the backslash */
                    if (Lexer->Pos != Lexer->End && *Lexer->Pos == '\r')
                    {
                        Lexer->Pos++;
                        Lexer->CharacterPos++;
                    }
                    if (Lexer->Pos != Lexer->End && *Lexer->Pos == '\n')
                    {
                        Lexer->Pos++;
                        Lexer->Line++;
                        Lexer->CharacterPos = 0;
                    }
                    /* stay in current mode (e.g. HashDefine); keep scanning */
                    GotToken = TokenNone;
                }
                else
                    LexFail(pc, Lexer, "illegal character '\\'");
                break;
            default:  LexFail(pc, Lexer, "illegal character '%c'", ThisChar); break;
        }
    } while (GotToken == TokenNone);
    
    return GotToken;
}

/* what size value goes with each token */
int LexTokenSize(enum LexToken Token)
{
    switch (Token)
    {
        case TokenIdentifier: case TokenStringConstant: return sizeof(char *);
        case TokenIntegerConstant: return 1 + sizeof(long long); /* 1-byte BaseType tag + 8-byte value */
        case TokenCharacterConstant: return sizeof(unsigned char);
        case TokenFPConstant: return 1 + sizeof(double); /* FPFormat tag + IEEE double slot */
        default: return 0;
    }
}

/* produce tokens from the lexer and return a heap buffer with the result - used for scanning */
void *LexTokenise(Picoc *pc, struct LexState *Lexer, int *TokenLen)
{
    enum LexToken Token;
    void *HeapMem;
    struct Value *GotValue;
    int MemUsed = 0;
    int ValueSize;
    ptrdiff_t SourceLen = Lexer->End - Lexer->Pos;
    int ReserveSpace;
    void *TokenSpace;
    char *TokenPos;
    int LastCharacterPos = 0;

    if (SourceLen < 0 || SourceLen > (INT_MAX - 16) / 12)
        LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
    ReserveSpace = (int)SourceLen * 12 + 16;
#ifdef USE_MALLOC_HEAP
    TokenSpace = HeapAllocMem(pc, ReserveSpace);
    if (TokenSpace == NULL)
        LexFail(pc, Lexer, DiagnosticMsgOutOfMemory);
#else
    TokenSpace = HeapAllocStack(pc, ReserveSpace);
    if (TokenSpace == NULL)
        LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
#endif
    TokenPos = (char *)TokenSpace;
    pc->LexTokeniseStackScratch = TokenSpace;
    pc->LexTokeniseStackScratchSize = ReserveSpace;
    pc->LexTokeniseLineScratch = NULL;
    
    {
    int LineCap = (int)SourceLen + 64;
    if (LineCap < 256) LineCap = 256;
    int LineCount = 0;
    unsigned short *LineTab = (unsigned short *)HeapAllocMem(pc, LineCap * (int)sizeof(unsigned short));
    if (LineTab == NULL) LexFail(pc, Lexer, DiagnosticMsgOutOfMemory);
    pc->LexTokeniseLineScratch = LineTab;
    do {
        Token = LexScanGetToken(pc, Lexer, &GotValue);
        ValueSize = LexTokenSize(Token);
        if (MemUsed + 2 + (ValueSize > 0 ? ValueSize : 0) > ReserveSpace)
            LexFail(pc, Lexer, DiagnosticMsgLexerSourceTooLarge);
        *(unsigned char *)TokenPos = Token; TokenPos++; MemUsed++;
        *(unsigned char *)TokenPos = (unsigned char)LastCharacterPos; TokenPos++; MemUsed++;
        if (LineCount < LineCap)
            LineTab[LineCount++] = (unsigned short)(Lexer->Line > 65535 ? 65535 : Lexer->Line);
        if (ValueSize > 0) {
            if (Token == TokenIntegerConstant) {
                *(unsigned char *)TokenPos = (unsigned char)GotValue->Typ->Base;
                memcpy((char *)TokenPos + 1, &GotValue->Val->LongLongInteger, sizeof(long long));
            } else if (Token == TokenFPConstant) {
                *(unsigned char *)TokenPos = (unsigned char)GotValue->Typ->FPFormat;
                memcpy((char *)TokenPos + 1, &GotValue->Val->FP, sizeof(double));
            } else memcpy((void *)TokenPos, (void *)GotValue->Val, ValueSize);
            TokenPos += ValueSize; MemUsed += ValueSize;
        }
        LastCharacterPos = Lexer->CharacterPos;
    } while (Token != TokenEOF);
    {
        int Align = (4 - (MemUsed & 3)) & 3;
        int Extra = (int)sizeof(unsigned int) + LineCount * (int)sizeof(unsigned short);
        HeapMem = HeapAllocMem(pc, MemUsed + Align + Extra);
        if (HeapMem == NULL) LexFail(pc, Lexer, DiagnosticMsgOutOfMemory);
        memcpy(HeapMem, TokenSpace, MemUsed);
        { char *p = (char *)HeapMem + MemUsed + Align; unsigned int n = (unsigned int)LineCount;
          memcpy(p, &n, sizeof(unsigned int));
          memcpy(p + sizeof(unsigned int), LineTab, LineCount * sizeof(unsigned short));
          pc->LexLineTab = (unsigned short *)(p + sizeof(unsigned int));
          pc->LexLineCount = LineCount;
          pc->LexTokenBase = (const unsigned char *)HeapMem; }
#ifdef USE_MALLOC_HEAP
        HeapFreeMem(pc, TokenSpace);
#else
        HeapPopStack(pc, TokenSpace, ReserveSpace);
#endif
        HeapFreeMem(pc, LineTab);
        pc->LexTokeniseStackScratch = NULL;
        pc->LexTokeniseStackScratchSize = 0;
        pc->LexTokeniseLineScratch = NULL;
    }
    }
#ifdef DEBUG_LEXER
    {
        int Count;
        printf("Tokens: ");
        for (Count = 0; Count < MemUsed; Count++)
            printf("%02x ", *((unsigned char *)HeapMem+Count));
        printf("\n");
    }
#endif
    if (TokenLen)
        *TokenLen = MemUsed;
    
    return HeapMem;
}

/* LexFail leaves LexTokenise via PlatformExit/longjmp, bypassing its local
 * cleanup. Release only the currently registered scratch objects. */
void LexCleanupOnFailure(Picoc *pc)
{
    if (pc == NULL) return;
    if (pc->LexTokeniseLineScratch != NULL)
    {
        HeapFreeMem(pc, pc->LexTokeniseLineScratch);
        pc->LexTokeniseLineScratch = NULL;
    }
    if (pc->LexTokeniseStackScratch != NULL)
    {
#ifdef USE_MALLOC_HEAP
        HeapFreeMem(pc, pc->LexTokeniseStackScratch);
#else
        (void)HeapPopStack(pc, pc->LexTokeniseStackScratch,
                           pc->LexTokeniseStackScratchSize);
#endif
        pc->LexTokeniseStackScratch = NULL;
        pc->LexTokeniseStackScratchSize = 0;
    }
}

/* lexically analyse some source text */
void *LexAnalyse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int *TokenLen)
{
    struct LexState Lexer;
    
    Lexer.Pos = Source;
    Lexer.End = Source + SourceLen;
    Lexer.Line = 1;
    Lexer.FileName = FileName;
    Lexer.Mode = LexModeNormal;
    Lexer.EmitExtraNewlines = 0;
    Lexer.CharacterPos = 1;
    Lexer.SourceText = Source;
    
    return LexTokenise(pc, &Lexer, TokenLen);
}

/* TOKEN_PATH_V2 Fase 0: dump token stream (oracle for format-changing phases).
 * Format: one line "tok <name>" or "tok <name> <payload>" per token; EOF ends. */
static const char *LexTokenName(enum LexToken Token)
{
    static const char *Names[] = {
        "None","Comma",
        "Assign","AddAssign","SubAssign","MulAssign","DivAssign","ModAssign",
        "ShlAssign","ShrAssign","AndAssign","OrAssign","XorAssign",
        "Question","Colon","LogicalOr","LogicalAnd","ArithOr","ArithXor","Ampersand",
        "Equal","NotEqual","Less","Greater","LessEq","GreaterEq","ShiftLeft","ShiftRight",
        "Plus","Minus","Asterisk","Slash","Modulus",
        "Increment","Decrement","UnaryNot","UnaryXor","Sizeof","Cast",
        "LeftSquare","RightSquare","Dot","Arrow","OpenBracket","CloseBracket",
        "Identifier","Integer","FP","String","Character",
        "Semicolon","Ellipsis","LeftBrace","RightBrace",
        "Int","Char","Float","Double","Void","Enum",
        "Long","Signed","Short","Static","Auto","Register","Extern","Const","Struct","Union","Unsigned","Typedef",
        "Continue","Do","Else","For","Goto","If","While","Break","Switch","Case","Default","Return",
        "HashDefine","HashInclude","HashIf","HashIfdef","HashIfndef","HashElse","HashEndif",
        "New","Delete","OpenMacroBracket","Volatile","Offsetof",
        "EOF","EndOfLine","EndOfFunction"
    };
    unsigned i = (unsigned)Token;
    if (i < sizeof(Names)/sizeof(Names[0]))
        return Names[i];
    return "?";
}

void LexDumpTokens(FILE *Out, const void *Tokens, int TokenLen)
{
    const unsigned char *Pos = (const unsigned char *)Tokens;
    const unsigned char *End = Pos + (TokenLen > 0 ? TokenLen : 0);
    if (Out == NULL) Out = stderr;
    if (Pos == NULL) { fprintf(Out, "tok EOF\n"); return; }
    while (Pos < End)
    {
        enum LexToken Token = (enum LexToken)Pos[0];
        int Col = Pos[1];
        int Sz = LexTokenSize(Token);
        fprintf(Out, "tok %s", LexTokenName(Token));
        if (Token == TokenIdentifier && Sz >= (int)sizeof(char*))
        {
            char *id = NULL;
            memcpy(&id, Pos + TOKEN_DATA_OFFSET, sizeof(char*));
            fprintf(Out, " %s", id ? id : "?");
        }
        else if ((Token == TokenIntegerConstant || Token == TokenCharacterConstant) && Sz > 0)
        {
            /* value payload varies; print hex bytes of payload for stability */
            int i;
            fprintf(Out, " ");
            for (i = 0; i < Sz && i < 16; i++)
                fprintf(Out, "%02x", Pos[TOKEN_DATA_OFFSET + i]);
        }
        else if (Token == TokenStringConstant && Sz >= (int)sizeof(char*))
        {
            char *s = NULL;
            memcpy(&s, Pos + TOKEN_DATA_OFFSET, sizeof(char*));
            fprintf(Out, " \"%s\"", s ? s : "");
        }
        (void)Col;
        fprintf(Out, "\n");
        if (Token == TokenEOF || Token == TokenEndOfFunction)
            break;
        Pos += TOKEN_DATA_OFFSET + Sz;
        if (Sz == 0 && Token != TokenEOF)
            Pos += 0; /* header-only tokens */
    }
}


/* prepare to parse a pre-tokenised buffer */
void LexInitParser(struct ParseState *Parser, Picoc *pc, const char *SourceText, void *TokenSource, char *FileName, int RunIt, int EnableDebugger)
{
    Parser->pc = pc;
    Parser->Pos = TokenSource;
    Parser->Line = 1;
    Parser->FileName = FileName;
    Parser->Mode = RunIt ? RunModeRun : RunModeSkip;
    Parser->SearchLabel = 0;
    Parser->HashIfLevel = 0;
    Parser->HashIfEvaluateToLevel = 0;
    Parser->CharacterPos = 0;
    Parser->SourceText = SourceText;
    Parser->DebugMode = EnableDebugger;
    /* FIX: ScopeID=0 significa "scope globale permanente", mai generato da
     * VariableScopeBegin (che parte da NextScopeID=1). Senza questa riga il
     * campo eredita un valore garbage dallo stack, causando false collisioni
     * in VariableScopeEnd che marcano OutOfScope le funzioni di libreria
     * (es. printf) registrate da LibraryAdd tramite un Parser locale. */
        Parser->ScopeID = 0;
    Parser->DeclVlaDimMask = 0;
    Parser->DeclArrayDimDepth = 0;
    Parser->UnevaluatedExpr = 0;
    Parser->SizeofOperatorDepth = 0;
    Parser->LastTokenStartPos = NULL;
    Parser->ExpressionStopPos = NULL;
    Parser->TokenLineTab = pc->LexLineTab;
    Parser->TokenLineCount = pc->LexLineCount;
    Parser->TokenBase = pc->LexTokenBase ? pc->LexTokenBase : (const unsigned char *)TokenSource;
    Parser->TokenIndex = 0;
    /* Line stays 1 (set above); do not seed from LineTab[0] — that caused +1 drift. */
}

/* get the next token, without pre-processing */
enum LexToken LexGetRawToken(struct ParseState *Parser, struct Value **Value, int IncPos)
{
    enum LexToken Token = TokenNone;
    int ValueSize;
    char *Prompt = NULL;
    Picoc *pc = Parser->pc;
#ifdef BABYC_LEX_STATS
    LexStatGetRaw++;
    if (IncPos) LexStatConsume++; else LexStatPeek++;
#endif
    /* TOKEN_V2: TokenIndex tracks stream position for future offset features.
     * Line stays classic (EOL increments) — table overwrite caused residual
     * diag line drift on LexCopyTokens function bodies. */
    do
    { 
        /* get the next token */
        if (Parser->Pos == NULL && pc->InteractiveHead != NULL)
            Parser->Pos = pc->InteractiveHead->Tokens;
        
        if (Parser->FileName != pc->StrEmpty || pc->InteractiveHead != NULL)
        { 
            /* skip leading newlines */
            while ((Token = (enum LexToken)*(unsigned char *)Parser->Pos) == TokenEndOfLine)
            {
#ifdef BABYC_LEX_STATS
                LexStatEOL++;
#endif
                Parser->Line++;
                Parser->Pos += TOKEN_DATA_OFFSET;
                if (Parser->TokenLineTab != NULL)
                    Parser->TokenIndex++;
            }
        }
    
        if (Parser->FileName == pc->StrEmpty && (pc->InteractiveHead == NULL || Token == TokenEOF))
        { 
            /* we're at the end of an interactive input token list */
            char LineBuffer[LINEBUFFER_MAX];
            void *LineTokens;
            int LineBytes;
            struct TokenLine *LineNode;
            
            if (pc->InteractiveHead == NULL || (unsigned char *)Parser->Pos == &pc->InteractiveTail->Tokens[pc->InteractiveTail->NumBytes-TOKEN_DATA_OFFSET])
            { 
                /* get interactive input */
                if (pc->LexUseStatementPrompt)
                {
                    Prompt = INTERACTIVE_PROMPT_STATEMENT;
                    pc->LexUseStatementPrompt = FALSE;
                }
                else
                    Prompt = INTERACTIVE_PROMPT_LINE;
                    
                if (PlatformGetLine(&LineBuffer[0], LINEBUFFER_MAX, Prompt) == NULL)
                    return TokenEOF;

                /* put the new line at the end of the linked list of interactive lines */        
                LineTokens = LexAnalyse(pc, pc->StrEmpty, &LineBuffer[0], strlen(LineBuffer), &LineBytes);
                LineNode = VariableAlloc(pc, Parser, sizeof(struct TokenLine), TRUE);
                LineNode->Tokens = LineTokens;
                LineNode->NumBytes = LineBytes;
                if (pc->InteractiveHead == NULL)
                { 
                    /* start a new list */
                    pc->InteractiveHead = LineNode;
                    Parser->Line = 1;
                    Parser->CharacterPos = 0;
                }
                else
                    pc->InteractiveTail->Next = LineNode;

                pc->InteractiveTail = LineNode;
                pc->InteractiveCurrentLine = LineNode;
                Parser->Pos = LineTokens;
            }
            else
            { 
                /* go to the next token line */
                if (Parser->Pos != &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET])
                { 
                    /* scan for the line */
                    for (pc->InteractiveCurrentLine = pc->InteractiveHead; Parser->Pos != &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET]; pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next)
                    { assert(pc->InteractiveCurrentLine->Next != NULL); }
                }

                assert(pc->InteractiveCurrentLine != NULL);
                pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next;
                assert(pc->InteractiveCurrentLine != NULL);
                Parser->Pos = pc->InteractiveCurrentLine->Tokens;
            }

            Token = (enum LexToken)*(unsigned char *)Parser->Pos;
        }
    } while ((Parser->FileName == pc->StrEmpty && Token == TokenEOF) || Token == TokenEndOfLine);

    Parser->CharacterPos = *((unsigned char *)Parser->Pos + 1);
    ValueSize = LexTokenSize(Token);
    if (ValueSize > 0)
    { 
        /* this token requires a value - unpack it */
        if (Value != NULL)
        { 
            if (Token == TokenIntegerConstant)
            {
                /* unpack: [1-byte BaseType tag][8-byte long long value] */
                unsigned char tag = *(unsigned char *)((char *)Parser->Pos + TOKEN_DATA_OFFSET);
                long long v;
                memcpy(&v, (char *)Parser->Pos + TOKEN_DATA_OFFSET + 1, sizeof(long long));
                pc->LexValue.Val->LongLongInteger = v;
                switch ((enum BaseType)tag)
                {
                    case TypeLongLong:         pc->LexValue.Typ = &pc->LongLongType;         break;
                    case TypeUnsignedLongLong: pc->LexValue.Typ = &pc->UnsignedLongLongType; break;
                    case TypeUnsignedLong:     pc->LexValue.Typ = &pc->UnsignedLongType;     break;
                    case TypeLong:             pc->LexValue.Typ = &pc->LongType;             break;
                    case TypeUnsignedInt:      pc->LexValue.Typ = &pc->UnsignedIntType;      break;
                    default:                   pc->LexValue.Typ = &pc->IntType;              break;
                }
            }
            else
            {
                switch (Token)
                {
                    case TokenStringConstant:       pc->LexValue.Typ = pc->CharPtrType; break;
                    case TokenIdentifier:           pc->LexValue.Typ = NULL; break;
                    case TokenCharacterConstant:    pc->LexValue.Typ = &pc->CharType; break;
#ifndef NO_FP
                    case TokenFPConstant:
                        if (ValueSize > 0) {
                            unsigned char fpformat = *(unsigned char *)((char *)Parser->Pos + TOKEN_DATA_OFFSET);
                            pc->LexValue.Typ = fpformat == FPStorageBinary32
                                ? &pc->FloatType : &pc->FPType;
                            memcpy((void *)&pc->LexValue.Val->FP,
                                   (void *)((char *)Parser->Pos + TOKEN_DATA_OFFSET + 1), sizeof(double));
                        }
                        break;
#endif
                    default: break;
                }
                if (Token != TokenFPConstant)
                    memcpy((void *)pc->LexValue.Val, (void *)((char *)Parser->Pos + TOKEN_DATA_OFFSET), ValueSize);
            }
            pc->LexValue.ValOnHeap = FALSE;
            pc->LexValue.ValOnStack = FALSE;
            pc->LexValue.IsLValue = FALSE;
            pc->LexValue.LValueFrom = NULL;
            *Value = &pc->LexValue;
        }
        
        if (IncPos) {
            Parser->Pos += ValueSize + TOKEN_DATA_OFFSET;
            if (Parser->TokenLineTab != NULL)
                Parser->TokenIndex++;
        }
    } else {
        if (IncPos && Token != TokenEOF) {
            Parser->Pos += TOKEN_DATA_OFFSET;
            if (Parser->TokenLineTab != NULL)
                Parser->TokenIndex++;
        }
    }
    
#ifdef DEBUG_LEXER
    printf("Got token=%02x inc=%d pos=%d\n", Token, IncPos, Parser->CharacterPos);
#endif
    assert(Token >= TokenNone && Token <= TokenEndOfFunction);
    return Token;
}

/* correct the token position depending if we already incremented the position */
void LexHashIncPos(struct ParseState *Parser, int IncPos)
{
    if (!IncPos)
        LexGetRawToken(Parser, NULL, TRUE);
}

/* handle a #ifdef directive */
void LexHashIfdef(struct ParseState *Parser, int IfNot)
{
    /* get symbol to check */
    struct Value *IdentValue;
    struct Value *SavedValue;
    int IsDefined;
    enum LexToken Token = LexGetRawToken(Parser, &IdentValue, TRUE);
    
    if (Token != TokenIdentifier)
        ProgramFail(Parser, "identifier expected");
    
    /* is the identifier defined? */
    IsDefined = TableGet(&Parser->pc->GlobalTable, IdentValue->Val->Identifier, &SavedValue, NULL, NULL, NULL);
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel && ( (IsDefined && !IfNot) || (!IsDefined && IfNot)) )
    {
        /* #if is active, evaluate to this new level */
        Parser->HashIfEvaluateToLevel++;
    }
    
    Parser->HashIfLevel++;
}

/* Evaluate #if with C-like precedence and integer macro expansion. */
static long LexHashIfCond(struct ParseState *Parser);

struct HashIfMacroFrame {
    int NumParams;
    char **ParamName;
    long ArgVal[16];
    struct HashIfMacroFrame *Prev;
};
static struct HashIfMacroFrame *HashIfMacroStack;
static int HashIfMacroDepth;

static long LexHashIfParamValue(const char *Name, int *Found)
{
    struct HashIfMacroFrame *F;
    int i;
    *Found = 0;
    for (F = HashIfMacroStack; F != NULL; F = F->Prev) {
        for (i = 0; i < F->NumParams; i++) {
            if (F->ParamName != NULL && F->ParamName[i] != NULL &&
                strcmp(F->ParamName[i], Name) == 0) {
                *Found = 1;
                return F->ArgVal[i];
            }
        }
    }
    return 0;
}

static long LexHashIfPrimary(struct ParseState *Parser)
{
    struct Value *TokVal = NULL;
    enum LexToken Token = LexGetRawToken(Parser, &TokVal, TRUE);
    Picoc *pc = Parser->pc;

    if (Token == TokenOpenBracket) {
        long V = LexHashIfCond(Parser);
        Token = LexGetRawToken(Parser, &TokVal, TRUE);
        if (Token != TokenCloseBracket)
            ProgramFail(Parser, "')' expected in #if");
        return V;
    }
    if (Token == TokenUnaryNot) return !LexHashIfPrimary(Parser);
    if (Token == TokenUnaryExor) return ~LexHashIfPrimary(Parser);
    if (Token == TokenMinus) return -LexHashIfPrimary(Parser);
    if (Token == TokenPlus) return LexHashIfPrimary(Parser);
    if (Token == TokenIntegerConstant) return (long)TokVal->Val->Integer;
    if (Token == TokenCharacterConstant) return (long)TokVal->Val->Character;

    if (Token == TokenIdentifier && TokVal != NULL &&
        strcmp(TokVal->Val->Identifier, "defined") == 0) {
        int HasParen = 0;
        Token = LexGetRawToken(Parser, &TokVal, TRUE);
        if (Token == TokenOpenBracket) {
            HasParen = 1;
            Token = LexGetRawToken(Parser, &TokVal, TRUE);
        }
        if (Token != TokenIdentifier)
            ProgramFail(Parser, "identifier expected after defined");
        {
            int Defined = VariableDefined(pc, TokVal->Val->Identifier);
            if (HasParen) {
                Token = LexGetRawToken(Parser, &TokVal, TRUE);
                if (Token != TokenCloseBracket)
                    ProgramFail(Parser, "')' expected after defined(identifier");
            }
            return Defined ? 1 : 0;
        }
    }

    if (Token == TokenIdentifier && TokVal != NULL) {
        struct Value *SavedValue = NULL;
        int ParamFound = 0;
        const char *Name = TokVal->Val->Identifier;
        long ParamV = LexHashIfParamValue(Name, &ParamFound);
        if (ParamFound) return ParamV;
        if (!VariableDefined(pc, Name)) return 0;
        VariableGet(pc, Parser, Name, &SavedValue);
        if (SavedValue == NULL || SavedValue->Typ == NULL ||
            SavedValue->Typ->Base != TypeMacro)
            ProgramFail(Parser, "value expected in #if");
        {
            struct MacroDef *MD = &SavedValue->Val->MacroDef;
            struct ParseState MacroParser;
            long V;
            if (HashIfMacroDepth >= 32) return 0;
            if (MD->NumParams != 0) {
                struct ParseState Save;
                enum LexToken Peek;
                int i;
                struct HashIfMacroFrame Frame;
                ParserCopy(&Save, Parser);
                Peek = LexGetRawToken(Parser, &TokVal, FALSE);
                if (Peek != TokenOpenBracket) {
                    ParserCopy(Parser, &Save);
                    return 0;
                }
                LexGetRawToken(Parser, &TokVal, TRUE);
                if (MD->NumParams > 16)
                    ProgramFail(Parser, "too many macro arguments in #if");
                Frame.NumParams = MD->NumParams;
                Frame.ParamName = MD->ParamName;
                Frame.Prev = HashIfMacroStack;
                for (i = 0; i < MD->NumParams; i++) Frame.ArgVal[i] = 0;
                for (i = 0; i < MD->NumParams; i++) {
                    Frame.ArgVal[i] = LexHashIfCond(Parser);
                    Peek = LexGetRawToken(Parser, &TokVal, TRUE);
                    if (i + 1 < MD->NumParams) {
                        if (Peek != TokenComma)
                            ProgramFail(Parser, "comma expected in #if macro");
                    } else if (Peek != TokenCloseBracket) {
                        ProgramFail(Parser, "')' expected in #if macro");
                    }
                }
                HashIfMacroStack = &Frame;
                HashIfMacroDepth++;
                ParserCopy(&MacroParser, &MD->Body);
                V = LexHashIfCond(&MacroParser);
                HashIfMacroDepth--;
                HashIfMacroStack = Frame.Prev;
                return V;
            }
            HashIfMacroDepth++;
            ParserCopy(&MacroParser, &MD->Body);
            V = LexHashIfCond(&MacroParser);
            HashIfMacroDepth--;
            return V;
        }
    }
    ProgramFail(Parser, "value expected in #if");
    return 0;
}

static long LexHashIfMul(struct ParseState *Parser)
{
    long Val = LexHashIfPrimary(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token == TokenAsterisk) {
            LexGetRawToken(Parser, &TokVal, TRUE);
            Val *= LexHashIfPrimary(Parser);
        } else if (Token == TokenSlash) {
            long R;
            LexGetRawToken(Parser, &TokVal, TRUE);
            R = LexHashIfPrimary(Parser);
            Val = R ? Val / R : 0;
#ifndef NO_MODULUS
        } else if (Token == TokenModulus) {
            long R;
            LexGetRawToken(Parser, &TokVal, TRUE);
            R = LexHashIfPrimary(Parser);
            Val = R ? Val % R : 0;
#endif
        } else {
            ParserCopy(Parser, &Save);
            break;
        }
    }
    return Val;
}

static long LexHashIfAdd(struct ParseState *Parser)
{
    long Val = LexHashIfMul(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token == TokenPlus) {
            LexGetRawToken(Parser, &TokVal, TRUE);
            Val += LexHashIfMul(Parser);
        } else if (Token == TokenMinus) {
            LexGetRawToken(Parser, &TokVal, TRUE);
            Val -= LexHashIfMul(Parser);
        } else {
            ParserCopy(Parser, &Save);
            break;
        }
    }
    return Val;
}

static long LexHashIfShift(struct ParseState *Parser)
{
    long Val = LexHashIfAdd(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        long R;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenShiftLeft && Token != TokenShiftRight) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        R = LexHashIfAdd(Parser);
        if (R < 0 || R >= (long)(sizeof(long) * CHAR_BIT)) Val = 0;
        else if (Token == TokenShiftLeft) Val = (long)((unsigned long)Val << R);
        else Val >>= R;
    }
    return Val;
}

static long LexHashIfCmp(struct ParseState *Parser)
{
    long Val = LexHashIfShift(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        long R;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenLessThan && Token != TokenGreaterThan &&
            Token != TokenLessEqual && Token != TokenGreaterEqual) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        R = LexHashIfShift(Parser);
        switch (Token) {
            case TokenLessThan: Val = Val < R; break;
            case TokenGreaterThan: Val = Val > R; break;
            case TokenLessEqual: Val = Val <= R; break;
            case TokenGreaterEqual: Val = Val >= R; break;
            default: break;
        }
    }
    return Val;
}

static long LexHashIfEq(struct ParseState *Parser)
{
    long Val = LexHashIfCmp(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        long R;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenEqual && Token != TokenNotEqual) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        R = LexHashIfCmp(Parser);
        Val = (Token == TokenEqual) ? (Val == R) : (Val != R);
    }
    return Val;
}

static long LexHashIfBitAnd(struct ParseState *Parser)
{
    long Val = LexHashIfEq(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenAmpersand) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        Val &= LexHashIfEq(Parser);
    }
    return Val;
}

static long LexHashIfBitXor(struct ParseState *Parser)
{
    long Val = LexHashIfBitAnd(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenArithmeticExor) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        Val ^= LexHashIfBitAnd(Parser);
    }
    return Val;
}

static long LexHashIfBitOr(struct ParseState *Parser)
{
    long Val = LexHashIfBitXor(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenArithmeticOr) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        Val |= LexHashIfBitXor(Parser);
    }
    return Val;
}

static long LexHashIfAnd(struct ParseState *Parser)
{
    long Val = LexHashIfBitOr(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenLogicalAnd) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        {
            long R = LexHashIfBitOr(Parser);
            Val = (Val && R) ? 1 : 0;
        }
    }
    return Val;
}

static long LexHashIfExprOr(struct ParseState *Parser)
{
    long Val = LexHashIfAnd(Parser);
    for (;;) {
        struct Value *TokVal = NULL;
        enum LexToken Token;
        struct ParseState Save;
        ParserCopy(&Save, Parser);
        Token = LexGetRawToken(Parser, &TokVal, FALSE);
        if (Token != TokenLogicalOr) {
            ParserCopy(Parser, &Save);
            break;
        }
        LexGetRawToken(Parser, &TokVal, TRUE);
        {
            long R = LexHashIfAnd(Parser);
            Val = (Val || R) ? 1 : 0;
        }
    }
    return Val;
}

static long LexHashIfCond(struct ParseState *Parser)
{
    long Cond = LexHashIfExprOr(Parser);
    struct Value *TokVal = NULL;
    enum LexToken Token;
    struct ParseState Save;
    ParserCopy(&Save, Parser);
    Token = LexGetRawToken(Parser, &TokVal, FALSE);
    if (Token == TokenQuestionMark) {
        long ThenV, ElseV;
        LexGetRawToken(Parser, &TokVal, TRUE);
        ThenV = LexHashIfCond(Parser);
        Token = LexGetRawToken(Parser, &TokVal, TRUE);
        if (Token != TokenColon)
            ProgramFail(Parser, "':' expected in #if");
        ElseV = LexHashIfCond(Parser);
        return Cond ? ThenV : ElseV;
    }
    ParserCopy(Parser, &Save);
    return Cond;
}

static long LexHashIfExpr(struct ParseState *Parser)
{
    return LexHashIfCond(Parser);
}

/* handle a #if directive */
void LexHashIf(struct ParseState *Parser)
{
    long Cond = LexHashIfExpr(Parser);
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel && Cond)
        Parser->HashIfEvaluateToLevel++;
    Parser->HashIfLevel++;
}

/* handle a #else directive */
void LexHashElse(struct ParseState *Parser)
{
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel - 1)
        Parser->HashIfEvaluateToLevel++;     /* #if was not active, make this next section active */
        
    else if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel)
    {
        /* #if was active, now go inactive */
        if (Parser->HashIfLevel == 0)
            ProgramFail(Parser, "#else without #if");
            
        Parser->HashIfEvaluateToLevel--;
    }
}

/* handle a #endif directive */
void LexHashEndif(struct ParseState *Parser)
{
    if (Parser->HashIfLevel == 0)
        ProgramFail(Parser, "#endif without #if");

    Parser->HashIfLevel--;
    if (Parser->HashIfEvaluateToLevel > Parser->HashIfLevel)
        Parser->HashIfEvaluateToLevel = Parser->HashIfLevel;
}

#if 0 /* useful for debug */
void LexPrintToken(enum LexToken Token)
{
    char* TokenNames[] = {
        /* 0x00 */ "None", 
        /* 0x01 */ "Comma",
        /* 0x02 */ "Assign", "AddAssign", "SubtractAssign", "MultiplyAssign", "DivideAssign", "ModulusAssign",
        /* 0x08 */ "ShiftLeftAssign", "ShiftRightAssign", "ArithmeticAndAssign", "ArithmeticOrAssign", "ArithmeticExorAssign",
        /* 0x0d */ "QuestionMark", "Colon", 
        /* 0x0f */ "LogicalOr", 
        /* 0x10 */ "LogicalAnd", 
        /* 0x11 */ "ArithmeticOr", 
        /* 0x12 */ "ArithmeticExor", 
        /* 0x13 */ "Ampersand", 
        /* 0x14 */ "Equal", "NotEqual", 
        /* 0x16 */ "LessThan", "GreaterThan", "LessEqual", "GreaterEqual",
        /* 0x1a */ "ShiftLeft", "ShiftRight", 
        /* 0x1c */ "Plus", "Minus", 
        /* 0x1e */ "Asterisk", "Slash", "Modulus",
        /* 0x21 */ "Increment", "Decrement", "UnaryNot", "UnaryExor", "Sizeof", "Cast",
        /* 0x27 */ "LeftSquareBracket", "RightSquareBracket", "Dot", "Arrow", 
        /* 0x2b */ "OpenBracket", "CloseBracket",
        /* 0x2d */ "Identifier", "IntegerConstant", "FPConstant", "StringConstant", "CharacterConstant",
        /* 0x32 */ "Semicolon", "Ellipsis",
        /* 0x34 */ "LeftBrace", "RightBrace",
        /* 0x36 */ "IntType", "CharType", "FloatType", "DoubleType", "VoidType", "EnumType",
        /* 0x3c */ "LongType", "SignedType", "ShortType", "StaticType", "AutoType", "RegisterType", "ExternType", "StructType", "UnionType", "UnsignedType", "Typedef",
        /* 0x46 */ "Continue", "Do", "Else", "For", "Goto", "If", "While", "Break", "Switch", "Case", "Default", "Return",
        /* 0x52 */ "HashDefine", "HashInclude", "HashIf", "HashIfdef", "HashIfndef", "HashElse", "HashEndif",
        /* 0x59 */ "New", "Delete",
        /* 0x5b */ "OpenMacroBracket",
        /* 0x5c */ "EOF", "EndOfLine", "EndOfFunction"
    };
    printf("{%s}", TokenNames[Token]);
}
#endif

/* get the next token given a parser state, pre-processing as we go.
 * TOKEN_PATH_V2 Fase 1: when HashIfLevel==0 no inactive branch is possible;
 * only the five #if* tokens need the slow path. Contiguous enum range. */
enum LexToken LexGetToken(struct ParseState *Parser, struct Value **Value, int IncPos)
{
    enum LexToken Token;
    int TryNextToken;
#ifdef BABYC_LEX_STATS
    LexStatGetToken++;
    if (Parser->Mode == RunModeSkip) LexStatSkipMode++;
#endif

    /* Fast path: no nested #if, so no skip-branch and no preproc loop. */
    if (Parser->HashIfLevel == 0)
    {
        Token = LexGetRawToken(Parser, Value, IncPos);
        /* TokenHashIf .. TokenHashEndif are contiguous (after HashInclude). */
        if (Token < TokenHashIf || Token > TokenHashEndif)
            return Token;
        /* rare: #if* at top level — fall into slow path handling below */
        switch (Token)
        {
            case TokenHashIfdef:  LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, FALSE); break;
            case TokenHashIfndef: LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, TRUE); break;
            case TokenHashIf:     LexHashIncPos(Parser, IncPos); LexHashIf(Parser); break;
            case TokenHashElse:   LexHashIncPos(Parser, IncPos); LexHashElse(Parser); break;
            case TokenHashEndif:  LexHashIncPos(Parser, IncPos); LexHashEndif(Parser); break;
            default: break;
        }
        /* After handling #if, HashIfLevel may be >0 or we need next token */
        if (Parser->HashIfLevel == 0 && Token >= TokenHashIf && Token <= TokenHashEndif)
        {
            /* consumed directive; fetch next real token (recursive is fine) */
            return LexGetToken(Parser, Value, IncPos);
        }
        /* else fall through to slow loop if still in inactive/active nesting */
    }

    /* Slow path: nested #if / inactive branches */
    do
    {
        int WasPreProcToken = TRUE;

        Token = LexGetRawToken(Parser, Value, IncPos);
        switch (Token)
        {
            case TokenHashIfdef:    LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, FALSE); break;
            case TokenHashIfndef:   LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, TRUE); break;
            case TokenHashIf:       LexHashIncPos(Parser, IncPos); LexHashIf(Parser); break;
            case TokenHashElse:     LexHashIncPos(Parser, IncPos); LexHashElse(Parser); break;
            case TokenHashEndif:    LexHashIncPos(Parser, IncPos); LexHashEndif(Parser); break;
            default:                WasPreProcToken = FALSE; break;
        }

        TryNextToken = (Parser->HashIfEvaluateToLevel < Parser->HashIfLevel && Token != TokenEOF) || WasPreProcToken;
        if (!IncPos && TryNextToken && !WasPreProcToken)
            LexGetRawToken(Parser, NULL, TRUE);

    } while (TryNextToken);

    return Token;
}

/* take a quick peek at the next token, skipping any pre-processing */
enum LexToken LexRawPeekToken(struct ParseState *Parser)
{
    return (enum LexToken)*(unsigned char *)Parser->Pos;
}

/* find the end of the line */
void LexToEndOfLine(struct ParseState *Parser)
{
    while (TRUE)
    {
        enum LexToken Token = (enum LexToken)*(unsigned char *)Parser->Pos;
        if (Token == TokenEndOfLine || Token == TokenEOF)
            return;
        else
            LexGetRawToken(Parser, NULL, TRUE);
    }
}

/* copy the tokens from StartParser to EndParser into new memory, removing TokenEOFs and terminate with a TokenEndOfFunction */
void *LexCopyTokens(struct ParseState *StartParser, struct ParseState *EndParser)
{
    int MemSize = 0;
    int CopySize;
    unsigned char *Pos = (unsigned char *)StartParser->Pos;
    unsigned char *NewTokens;
    unsigned char *NewTokenPos;
    struct TokenLine *ILine;
    Picoc *pc = StartParser->pc;
    
    if (pc->InteractiveHead == NULL)
    { 
        /* non-interactive mode - copy the tokens */
        MemSize = EndParser->Pos - StartParser->Pos;
        NewTokens = VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE);
        memcpy(NewTokens, (void *)StartParser->Pos, MemSize);
    }
    else
    { 
        /* we're in interactive mode - add up line by line */
        for (pc->InteractiveCurrentLine = pc->InteractiveHead; pc->InteractiveCurrentLine != NULL && (Pos < &pc->InteractiveCurrentLine->Tokens[0] || Pos >= &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes]); pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next)
        {} /* find the line we just counted */
        
        if (pc->InteractiveCurrentLine == NULL)
        {
            /* A setup-C source can be parsed recursively while the outer
             * parser is interactive (for example, a lazy #include).  Its
             * token buffer is not part of InteractiveHead.  Treat that
             * nested source as an ordinary contiguous token stream instead
             * of dereferencing a nonexistent interactive line. */
            MemSize = EndParser->Pos - StartParser->Pos;
            NewTokens = VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE);
            memcpy(NewTokens, (void *)StartParser->Pos, MemSize);
        }
        else if (EndParser->Pos >= StartParser->Pos && EndParser->Pos < &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes])
        { 
            /* all on a single line */
            MemSize = EndParser->Pos - StartParser->Pos;
            NewTokens = VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE);
            memcpy(NewTokens, (void *)StartParser->Pos, MemSize);
        }
        else
        { 
            /* it's spread across multiple lines */
            MemSize = &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET] - Pos;

            for (ILine = pc->InteractiveCurrentLine->Next; ILine != NULL && (EndParser->Pos < &ILine->Tokens[0] || EndParser->Pos >= &ILine->Tokens[ILine->NumBytes]); ILine = ILine->Next)
                MemSize += ILine->NumBytes - TOKEN_DATA_OFFSET;
            
            assert(ILine != NULL);
            MemSize += EndParser->Pos - &ILine->Tokens[0];
            NewTokens = VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE);
            
            CopySize = &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET] - Pos;
            memcpy(NewTokens, Pos, CopySize);
            NewTokenPos = NewTokens + CopySize;
            for (ILine = pc->InteractiveCurrentLine->Next; ILine != NULL && (EndParser->Pos < &ILine->Tokens[0] || EndParser->Pos >= &ILine->Tokens[ILine->NumBytes]); ILine = ILine->Next)
            {
                memcpy(NewTokenPos, &ILine->Tokens[0], ILine->NumBytes - TOKEN_DATA_OFFSET);
                NewTokenPos += ILine->NumBytes-TOKEN_DATA_OFFSET;
            }
            assert(ILine != NULL);
            memcpy(NewTokenPos, &ILine->Tokens[0], EndParser->Pos - &ILine->Tokens[0]);
        }
    }
    
    NewTokens[MemSize] = (unsigned char)TokenEndOfFunction;
        
    return NewTokens;
}

/* indicate that we've completed up to this point in the interactive input and free expired tokens */
void LexInteractiveClear(Picoc *pc, struct ParseState *Parser)
{
    while (pc->InteractiveHead != NULL)
    {
        struct TokenLine *NextLine = pc->InteractiveHead->Next;
        
        HeapFreeMem(pc, pc->InteractiveHead->Tokens);
        HeapFreeMem(pc, pc->InteractiveHead);
        pc->InteractiveHead = NextLine;
    }

    if (Parser != NULL)
        Parser->Pos = NULL;
        
    pc->InteractiveTail = NULL;
}

/* indicate that we've completed up to this point in the interactive input and free expired tokens */
void LexInteractiveCompleted(Picoc *pc, struct ParseState *Parser)
{
    while (pc->InteractiveHead != NULL && !(Parser->Pos >= &pc->InteractiveHead->Tokens[0] && Parser->Pos < &pc->InteractiveHead->Tokens[pc->InteractiveHead->NumBytes]))
    { 
        /* this token line is no longer needed - free it */
        struct TokenLine *NextLine = pc->InteractiveHead->Next;
        
        HeapFreeMem(pc, pc->InteractiveHead->Tokens);
        HeapFreeMem(pc, pc->InteractiveHead);
        pc->InteractiveHead = NextLine;
        
        if (pc->InteractiveHead == NULL)
        { 
            /* we've emptied the list */
            Parser->Pos = NULL;
            pc->InteractiveTail = NULL;
        }
    }
}

/* the next time we prompt, make it the full statement prompt */
void LexInteractiveStatementPrompt(Picoc *pc)
{
    pc->LexUseStatementPrompt = TRUE;
}
