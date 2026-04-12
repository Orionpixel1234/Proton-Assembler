#include "v001.h"
#include <stdint.h>
#include <ctype.h>

/* ============================================================
 * File I/O
 * ============================================================ */

int inputBufferFull(char *buffer, size_t bufferSize) {
    FILE *f = fopen(inputFileName, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open input file '%s'\n", inputFileName);
        return EXIT_FAILURE;
    }
    size_t n = fread(buffer, 1, bufferSize - 1, f);
    buffer[n] = '\0';
    fclose(f);
    inputBuffer = buffer;
    return EXIT_SUCCESS;
}

/* ============================================================
 * CLI Argument Handling
 * ============================================================ */

int handleOption(const char *option, const char *value) {
    if (strcmp(option, "-h") == 0 || strcmp(option, "--help") == 0) {
        printf("Usage: v001 [-f format] <input.asm> [-o output]\n");
        printf("  -f format    Output format: bin (default)\n");
        printf("  -o output    Output file name\n");
        printf("  --file file  Input file (alternative to positional arg)\n");
        printf("  -h, --help   Show this help\n");
        exit(EXIT_SUCCESS);
    }
    if (strcmp(option, "-f") == 0) {
        outputFileFormat = value;
        return EXIT_SUCCESS;
    }
    if (strcmp(option, "-o") == 0) {
        outputFileName = value;
        outputFileDefined = 1;
        return EXIT_SUCCESS;
    }
    if (strcmp(option, "--file") == 0) {
        inputFileName = value;
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "Error: unknown option '%s'\n", option);
    return EXIT_FAILURE;
}

int checkArgs(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-o") == 0 ||
                strcmp(argv[i], "--file") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: option '%s' requires a value\n", argv[i]);
                    return EXIT_FAILURE;
                }
                if (handleOption(argv[i], argv[i + 1]) != EXIT_SUCCESS)
                    return EXIT_FAILURE;
                i++;
            } else {
                if (handleOption(argv[i], NULL) != EXIT_SUCCESS)
                    return EXIT_FAILURE;
            }
        } else {
            inputFileName = argv[i];
        }
    }
    if (!inputFileName) {
        fprintf(stderr, "Error: no input file specified\n");
        return EXIT_FAILURE;
    }
    if (!outputFileName)   outputFileName   = "a.bin";
    if (!outputFileFormat) outputFileFormat = "bin";
    return EXIT_SUCCESS;
}

/* ============================================================
 * Lexer
 * ============================================================ */

typedef enum {
    TOK_MNEMONIC,
    TOK_REGISTER,
    TOK_IMMEDIATE,
    TOK_COMMA,
    TOK_NEWLINE,
    TOK_EOF,
    TOK_UNKNOWN,
} TokenType;

typedef struct {
    TokenType type;
    char      text[64];
    long long imm_val;
} Token;

static const char *lex_pos;

static const char *REG_NAMES[] = {
    "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
    "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15",
    NULL
};

static int is_register(const char *name) {
    for (int i = 0; REG_NAMES[i]; i++)
        if (strcmp(name, REG_NAMES[i]) == 0) return 1;
    return 0;
}

static void skip_whitespace(void) {
    while (*lex_pos == ' ' || *lex_pos == '\t')
        lex_pos++;
}

static Token next_token(void) {
    Token tok = {0};
    skip_whitespace();

    if (*lex_pos == '\0') {
        tok.type = TOK_EOF;
        return tok;
    }

    /* Newline */
    if (*lex_pos == '\n' || *lex_pos == '\r') {
        tok.type = TOK_NEWLINE;
        while (*lex_pos == '\n' || *lex_pos == '\r') lex_pos++;
        return tok;
    }

    /* Comment — skip to end of line */
    if (*lex_pos == ';') {
        while (*lex_pos && *lex_pos != '\n') lex_pos++;
        return next_token();
    }

    /* Comma */
    if (*lex_pos == ',') {
        tok.type = TOK_COMMA;
        tok.text[0] = ',';
        tok.text[1] = '\0';
        lex_pos++;
        return tok;
    }

    /* Hex immediate: 0x... */
    if (*lex_pos == '0' && (*(lex_pos + 1) == 'x' || *(lex_pos + 1) == 'X')) {
        char *end;
        tok.type    = TOK_IMMEDIATE;
        tok.imm_val = (long long)strtoull(lex_pos, &end, 16);
        size_t len  = (size_t)(end - lex_pos);
        if (len >= sizeof(tok.text)) len = sizeof(tok.text) - 1;
        memcpy(tok.text, lex_pos, len);
        tok.text[len] = '\0';
        lex_pos = end;
        return tok;
    }

    /* Decimal immediate */
    if (*lex_pos >= '0' && *lex_pos <= '9') {
        char *end;
        tok.type    = TOK_IMMEDIATE;
        tok.imm_val = (long long)strtoull(lex_pos, &end, 10);
        size_t len  = (size_t)(end - lex_pos);
        if (len >= sizeof(tok.text)) len = sizeof(tok.text) - 1;
        memcpy(tok.text, lex_pos, len);
        tok.text[len] = '\0';
        lex_pos = end;
        return tok;
    }

    /* Word — mnemonic or register */
    if (isalpha((unsigned char)*lex_pos) || *lex_pos == '_') {
        size_t i = 0;
        while ((isalnum((unsigned char)*lex_pos) || *lex_pos == '_') &&
               i < sizeof(tok.text) - 1)
            tok.text[i++] = *lex_pos++;
        tok.text[i] = '\0';
        tok.type = is_register(tok.text) ? TOK_REGISTER : TOK_MNEMONIC;
        return tok;
    }

    /* Unknown character */
    tok.type    = TOK_UNKNOWN;
    tok.text[0] = *lex_pos++;
    tok.text[1] = '\0';
    return tok;
}

/* ============================================================
 * Encoder
 * ============================================================ */

static int reg_index(const char *name) {
    for (int i = 0; REG_NAMES[i]; i++)
        if (strcmp(name, REG_NAMES[i]) == 0) return i;
    return -1;
}

/* Emit a 64-bit little-endian immediate into buf. */
static void emit_imm64(unsigned char *buf, size_t *n, long long val) {
    unsigned long long v = (unsigned long long)val;
    for (int i = 0; i < 8; i++) { buf[(*n)++] = v & 0xFF; v >>= 8; }
}

/*
 * Encode one line of tokens into machine code appended at buf[*n].
 * Returns 0 on success, -1 on error.
 *
 * x86-64 encodings used:
 *   SYSCALL            0F 05
 *   PUSH r64           [REX.B] 50+rd        (REX.B = 0x41 for r8-r15)
 *   POP  r64           [REX.B] 58+rd
 *   MOV  r64, imm64    REX.W (48|B) B8+rd imm64   (MOVABS)
 *   MOV  r64, r64      REX.W (48|R<<2|B) 89 ModRM  (r/m64, r64)
 *   XOR  r64, r64      REX.W (48|R<<2|B) 31 ModRM  (r/m64, r64)
 *
 *   REX.W = 0x48 | (src_ext << 2) | dst_ext
 *   ModRM = 0xC0 | (src & 7) << 3 | (dst & 7)
 */
static int encode_line(Token *toks, int ntoks, unsigned char *buf, size_t *n) {
    if (ntoks == 0) return 0;
    if (toks[0].type != TOK_MNEMONIC) return 0;

    const char *mn = toks[0].text;

    /* ------ SYSCALL ------ */
    if (strcmp(mn, "SYSCALL") == 0) {
        buf[(*n)++] = 0x0F;
        buf[(*n)++] = 0x05;
        return 0;
    }

    /* ------ PUSH reg64 ------ */
    if (strcmp(mn, "PUSH") == 0) {
        if (ntoks < 2 || toks[1].type != TOK_REGISTER) {
            fprintf(stderr, "Error: PUSH requires a register operand\n");
            return -1;
        }
        int r = reg_index(toks[1].text);
        if (r < 0) { fprintf(stderr, "Error: unknown register '%s'\n", toks[1].text); return -1; }
        if (r >= 8) buf[(*n)++] = 0x41;          /* REX.B */
        buf[(*n)++] = 0x50 | (r & 7);
        return 0;
    }

    /* ------ POP reg64 ------ */
    if (strcmp(mn, "POP") == 0) {
        if (ntoks < 2 || toks[1].type != TOK_REGISTER) {
            fprintf(stderr, "Error: POP requires a register operand\n");
            return -1;
        }
        int r = reg_index(toks[1].text);
        if (r < 0) { fprintf(stderr, "Error: unknown register '%s'\n", toks[1].text); return -1; }
        if (r >= 8) buf[(*n)++] = 0x41;          /* REX.B */
        buf[(*n)++] = 0x58 | (r & 7);
        return 0;
    }

    /* ------ MOV dst, src ------ */
    if (strcmp(mn, "MOV") == 0) {
        if (ntoks < 4 || toks[1].type != TOK_REGISTER || toks[2].type != TOK_COMMA) {
            fprintf(stderr, "Error: MOV syntax: MOV dst, src\n");
            return -1;
        }
        int dst = reg_index(toks[1].text);
        if (dst < 0) { fprintf(stderr, "Error: unknown register '%s'\n", toks[1].text); return -1; }

        if (toks[3].type == TOK_IMMEDIATE) {
            /* MOVABS: REX.W + B8+rd + imm64 */
            buf[(*n)++] = 0x48 | ((dst >> 3) & 1);
            buf[(*n)++] = 0xB8 | (dst & 7);
            emit_imm64(buf, n, toks[3].imm_val);
        } else if (toks[3].type == TOK_REGISTER) {
            /* MOV r/m64, r64: REX.W + 0x89 + ModRM */
            int src = reg_index(toks[3].text);
            if (src < 0) { fprintf(stderr, "Error: unknown register '%s'\n", toks[3].text); return -1; }
            buf[(*n)++] = 0x48 | ((src >> 3) << 2) | ((dst >> 3) & 1);
            buf[(*n)++] = 0x89;
            buf[(*n)++] = 0xC0 | ((src & 7) << 3) | (dst & 7);
        } else {
            fprintf(stderr, "Error: MOV source must be a register or immediate\n");
            return -1;
        }
        return 0;
    }

    /* ------ XOR dst, src ------ */
    if (strcmp(mn, "XOR") == 0) {
        if (ntoks < 4 || toks[1].type != TOK_REGISTER ||
            toks[2].type != TOK_COMMA || toks[3].type != TOK_REGISTER) {
            fprintf(stderr, "Error: XOR syntax: XOR dst, src\n");
            return -1;
        }
        int dst = reg_index(toks[1].text);
        int src = reg_index(toks[3].text);
        if (dst < 0 || src < 0) { fprintf(stderr, "Error: unknown register\n"); return -1; }
        buf[(*n)++] = 0x48 | ((src >> 3) << 2) | ((dst >> 3) & 1);
        buf[(*n)++] = 0x31;
        buf[(*n)++] = 0xC0 | ((src & 7) << 3) | (dst & 7);
        return 0;
    }

    fprintf(stderr, "Error: unknown mnemonic '%s'\n", mn);
    return -1;
}

/* ============================================================
 * Assembler Driver
 * ============================================================ */

#define MAX_FILE_SIZE (1 << 20)   /* 1 MB */
#define MAX_CODE_SIZE (1 << 20)

int handle(void) {
    static char file_buf[MAX_FILE_SIZE];

    if (inputBufferFull(file_buf, sizeof(file_buf)) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    unsigned char *code = malloc(MAX_CODE_SIZE);
    if (!code) { fprintf(stderr, "Error: out of memory\n"); return EXIT_FAILURE; }
    size_t code_n = 0;

    lex_pos = inputBuffer;
    int line = 1;
    int err  = 0;

    while (*lex_pos) {
        /* Collect all tokens on this line */
        Token toks[16];
        int   ntoks = 0;
        Token t;

        while (1) {
            t = next_token();
            if (t.type == TOK_NEWLINE || t.type == TOK_EOF) break;
            if (t.type == TOK_UNKNOWN) {
                fprintf(stderr, "Error: unexpected character '%s' on line %d\n",
                        t.text, line);
                err = 1;
                break;
            }
            if (ntoks < 16) toks[ntoks++] = t;
        }

        if (!err && encode_line(toks, ntoks, code, &code_n) != 0) {
            fprintf(stderr, "  (on line %d)\n", line);
            err = 1;
        }

        if (err || t.type == TOK_EOF) break;
        line++;
    }

    if (err) { free(code); return EXIT_FAILURE; }

    /* Write flat binary output */
    FILE *out = fopen(outputFileName, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file '%s'\n", outputFileName);
        free(code);
        return EXIT_FAILURE;
    }
    fwrite(code, 1, code_n, out);
    fclose(out);
    free(code);

    printf("Assembled %zu byte%s -> %s\n", code_n, code_n == 1 ? "" : "s", outputFileName);
    return EXIT_SUCCESS;
}

/* ============================================================
 * Entry Point
 * ============================================================ */

int main(int argc, char *argv[]) {
    if (checkArgs(argc, argv) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    return handle();
}
