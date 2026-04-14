/* proton.c — Proton Assembler
 * Tiny NASM-like x86 assembler (16-bit real mode focus)
 * Two-pass, flat binary output.
 *
 * Directives: [ORG], [BITS], [INCLUDE]
 * Data:       DB, DW, DD, DQ, TIMES, EQU
 * Labels:     name:  (global and .local)
 * Output:     raw flat binary (-f bin)
 */

#include "proton.h"
#include <stdint.h>
#include <ctype.h>

/* ============================================================
 * Limits
 * ============================================================ */
#define MAX_SYM  2048
#define MAX_CODE (512 * 1024)
#define MAX_SRC  (1024 * 1024)
#define MAX_INC  8
#define TMAX     64

/* ============================================================
 * Symbol Table
 * ============================================================ */
typedef struct { char n[128]; long long v; int ok; } Sym;
static Sym  SYM[MAX_SYM];
static int  NSYM;

static Sym *sfind(const char *n) {
    for (int i = 0; i < NSYM; i++)
        if (!strcmp(SYM[i].n, n)) return SYM + i;
    return NULL;
}
static void sset(const char *n, long long v) {
    Sym *s = sfind(n);
    if (!s) {
        if (NSYM >= MAX_SYM) { fputs("symbol table full\n", stderr); exit(1); }
        s = SYM + NSYM++;
        strncpy(s->n, n, 127);
    }
    s->v = v; s->ok = 1;
}

/* ============================================================
 * Assembler State
 * ============================================================ */
static int    gbits = 16;
static size_t gorg  = 0;    /* ORG base address          */
static size_t gpc   = 0;    /* offset from section start */
static size_t gsec  = 0;    /* section start offset ($$) */
static int    gpass = 1;

static unsigned char gbuf[MAX_CODE];
static size_t gbn = 0;

static void  E(uint8_t b)   { if (gpass==2) { if (gbn<MAX_CODE) gbuf[gbn]=b; gbn++; } gpc++; }
static void E16(uint16_t v) { E(v & 0xFF); E(v >> 8); }
static void E32(uint32_t v) { E16(v & 0xFFFF); E16(v >> 16); }
/* Emit 0x66 operand-size prefix when operand width doesn't match current mode */
static void ospfx(int op32) { if ((op32&&gbits==16)||(!op32&&gbits==32)) E(0x66); }

/* ============================================================
 * Lexer
 * ============================================================ */
typedef enum {
    TW,   /* word: mnemonic, label, register name */
    TN,   /* integer literal                      */
    TS,   /* string literal "..."                 */
    TC,   /* comma                                */
    TCOL, /* colon                                */
    TLB,  /* [                                    */
    TRB,  /* ]                                    */
    TLP,  /* (                                    */
    TRP,  /* )                                    */
    TPLS, TMNS, TSTR, TSLH, /* + - * /           */
    TDOL, TDDOL,             /* $  $$             */
    TNL, TEOF
} TType;

typedef struct { TType t; char s[256]; long long n; } Tok;
static const char *LP; /* lexer position */

static Tok lx(void) {
    Tok t = {0};
    while (*LP == ' ' || *LP == '\t') LP++;
    if (!*LP)                        { t.t = TEOF; return t; }
    if (*LP=='\n' || *LP=='\r')      { t.t = TNL; while (*LP=='\n'||*LP=='\r') LP++; return t; }
    if (*LP == ';')                  { while (*LP && *LP!='\n') LP++; return lx(); }

#define C1(c,T,str) if (*LP==(c)) { t.t=(T); strcpy(t.s,(str)); LP++; return t; }
    C1(',',TC,",") C1(':',TCOL,":") C1('(',TLP,"(") C1(')',TRP,")")
    C1('+',TPLS,"+") C1('-',TMNS,"-") C1('*',TSTR,"*") C1('/',TSLH,"/")
    C1('[',TLB,"[") C1(']',TRB,"]")
#undef C1

    /* $$ or $ */
    if (*LP == '$') {
        if (*(LP+1)=='$') { t.t=TDDOL; strcpy(t.s,"$$"); LP+=2; }
        else              { t.t=TDOL;  strcpy(t.s,"$");  LP++;  }
        return t;
    }
    /* String literal */
    if (*LP == '"') {
        LP++; size_t i=0;
        while (*LP && *LP!='"' && i<255) t.s[i++] = *LP++;
        t.s[i]=0; if (*LP=='"') LP++;
        t.t = TS; return t;
    }
    /* Character literal */
    if (*LP == '\'') {
        LP++;
        if (*LP=='\\') {
            LP++; switch (*LP++) {
                case 'n': t.n='\n'; break; case 't': t.n='\t'; break;
                case '0': t.n=  0;  break; case 'r': t.n='\r'; break;
                case '\\':t.n='\\'; break; default:  t.n=*(LP-1);
            }
        } else t.n = (uint8_t)*LP++;
        if (*LP=='\'') LP++;
        t.t = TN; snprintf(t.s, sizeof t.s, "%lld", t.n); return t;
    }
    /* Hex: 0x... */
    if (*LP=='0' && (*(LP+1)=='x'||*(LP+1)=='X')) {
        char *e; t.n = (long long)strtoull(LP, &e, 16);
        size_t l = e-LP; if (l>255) l=255; memcpy(t.s,LP,l); t.s[l]=0; LP=e; t.t=TN; return t;
    }
    /* Binary suffix: 10110b / decimal */
    if (isdigit((uint8_t)*LP)) {
        const char *p = LP;
        while (*p=='0'||*p=='1') p++;
        if ((*p=='b'||*p=='B') && p>LP) {
            char *e; t.n=(long long)strtoull(LP,&e,2); e++;
            size_t l=e-LP; if(l>255)l=255; memcpy(t.s,LP,l); t.s[l]=0; LP=e; t.t=TN; return t;
        }
        char *e; t.n=(long long)strtoull(LP,&e,10);
        size_t l=e-LP; if(l>255)l=255; memcpy(t.s,LP,l); t.s[l]=0; LP=e; t.t=TN; return t;
    }
    /* Word */
    if (isalpha((uint8_t)*LP)||*LP=='_'||*LP=='.') {
        size_t i=0;
        while ((isalnum((uint8_t)*LP)||*LP=='_'||*LP=='.') && i<255) t.s[i++]=*LP++;
        t.s[i]=0; t.t=TW; return t;
    }
    t.s[0]=*LP++; t.s[1]=0; t.t=TW; return t;
}

/* Collect one line of tokens; last entry is TNL or TEOF. */
static int gln(Tok *ts) {
    int n=0;
    for (;;) {
        Tok t=lx(); if (t.t==TEOF||t.t==TNL) { ts[n++]=t; return n; }
        if (n<TMAX) ts[n++]=t;
    }
}

/* ============================================================
 * Expression Evaluator  (recursive descent)
 * ============================================================ */
static Tok *ET; static int EP, EL;
static long long pexpr(void); /* forward */

static long long pprim(void) {
    if (EP >= EL) return 0;
    Tok *t = ET + EP;
    if (t->t==TDDOL){ EP++; return (long long)(gorg + gsec); }
    if (t->t==TDOL) { EP++; return (long long)(gorg + gpc);  }
    if (t->t==TN)   { EP++; return t->n; }
    if (t->t==TLP)  { EP++; long long v=pexpr(); if (EP<EL&&ET[EP].t==TRP) EP++; return v; }
    if (t->t==TMNS) { EP++; return -pprim(); }
    if (t->t==TW)   { EP++; Sym *s=sfind(t->s); return (s&&s->ok)?s->v:0; }
    EP++; return 0;
}
static long long pterm(void) {
    long long v=pprim();
    while (EP<EL) {
        TType op=ET[EP].t; if (op!=TSTR&&op!=TSLH) break;
        EP++; long long r=pprim(); v=(op==TSTR)?v*r:(r?v/r:0);
    }
    return v;
}
static long long pexpr(void) {
    long long v=pterm();
    while (EP<EL) {
        TType op=ET[EP].t; if (op!=TPLS&&op!=TMNS) break;
        EP++; long long r=pterm(); v=(op==TPLS)?v+r:v-r;
    }
    return v;
}
static long long xeval(Tok *ts, int s, int e) { ET=ts; EP=s; EL=e; return pexpr(); }

/* ============================================================
 * Register Tables
 * ============================================================ */
static const char *R16[]={"AX","CX","DX","BX","SP","BP","SI","DI",0};
static const char *R8[] ={"AL","CL","DL","BL","AH","CH","DH","BH",0};
static const char *R32[]={"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI",0};
static const char *SREG[]={"ES","CS","SS","DS","FS","GS",0};
static int r16 (const char *n){for(int i=0;R16[i]; i++)if(!strcmp(n,R16[i])) return i;return -1;}
static int r8  (const char *n){for(int i=0;R8[i];  i++)if(!strcmp(n,R8[i]))  return i;return -1;}
static int r32 (const char *n){for(int i=0;R32[i]; i++)if(!strcmp(n,R32[i])) return i;return -1;}
static int sreg(const char *n){for(int i=0;SREG[i];i++)if(!strcmp(n,SREG[i]))return i;return -1;}

/* ============================================================
 * Instruction Encoder — 16-bit x86
 *
 * Encodings used:
 *   HLT/CLI/STI/NOP/RET/RETF/PUSHA/POPA/LODSB/STOSB
 *   INT  imm8              CD ib
 *   PUSH r16               50+r
 *   PUSH imm8/16           6A ib  /  68 iw
 *   POP  r16               58+r
 *   JMP  near              E9 rel16   (always 3 bytes for 2-pass consistency)
 *   CALL near              E8 rel16
 *   Jcc  short             7x rel8
 *   IN/OUT                 E4/E5/E6/E7/EC/ED/EE/EF
 *   LGDT [mem]             0F 01 16 disp16
 *   MOV  r8,  imm8         B0+r ib
 *   MOV  r16, imm16        B8+r iw
 *   MOV  r8,  r8           8A /r
 *   MOV  r16, r16          89 /r
 *   MOV  r16, [mem]        8B /r  (AX→A1)
 *   MOV  [mem], r16        89 /r  (AX→A3)
 *   MOV  CR0, r16          0F 22 /r
 *   XOR/AND/OR/ADD/SUB/CMP  r,r  or  r,imm
 *   TEST r, r
 *   INC/DEC r16/r8
 *   SHL/SHR/SAR/ROL/ROR  r, 1 / CL / imm8
 *   MOVZX r16, r8
 * ============================================================ */

/* Instruction / ALU tables at file scope */
static const struct { const char *m; uint8_t op; } JCC[] = {
    {"JZ",0x74},{"JE",0x74},{"JNZ",0x75},{"JNE",0x75},
    {"JC",0x72},{"JNC",0x73},{"JB",0x72},{"JAE",0x73},
    {"JA",0x77},{"JBE",0x76},{"JL",0x7C},{"JGE",0x7D},
    {"JG",0x7F},{"JLE",0x7E},{"JS",0x78},{"JNS",0x79},
    {"JCXZ",0xE3},{NULL,0}
};
static const struct { const char *m; uint8_t rr8,rr16,grp,tst; } ALU[] = {
    {"XOR",0x30,0x31,6,0},{"AND",0x20,0x21,4,0},{"OR",0x08,0x09,1,0},
    {"ADD",0x00,0x01,0,0},{"SUB",0x28,0x29,5,0},{"CMP",0x38,0x39,7,0},
    {"TEST",0x84,0x85,0,1},{NULL,0,0,0,0}
};
static const struct { const char *m; uint8_t g; } SH[] = {
    {"SHL",4},{"SHR",5},{"SAR",7},{"ROL",0},{"ROR",1},{NULL,0}
};

static int findcm(Tok *ts, int n) {
    for (int i=1; i<n; i++) if (ts[i].t==TC) return i;
    return -1;
}
/* Extract address from [expr] token range */
static long long memop(Tok *ts, int s, int e) {
    if (s<e && ts[s].t==TLB) s++;
    int c=s; while (c<e && ts[c].t!=TRB) c++;
    return xeval(ts, s, c);
}

static int enc(Tok *ts, int n) {
    if (!n || ts[0].t!=TW) return 0;
    const char *mn = ts[0].s;
    int end = n-1, co = findcm(ts, n);
    int o1s=1, o1e=(co>=0)?co:end;
    int o2s=(co>=0)?co+1:-1, o2e=(co>=0)?end:-1;

    /* Strip BYTE/WORD/DWORD/QWORD size specifiers from operands */
    if (o1s<o1e && ts[o1s].t==TW &&
        (!strcmp(ts[o1s].s,"BYTE")||!strcmp(ts[o1s].s,"WORD")||
         !strcmp(ts[o1s].s,"DWORD")||!strcmp(ts[o1s].s,"QWORD"))) o1s++;
    if (o2s>=0 && o2s<o2e && ts[o2s].t==TW &&
        (!strcmp(ts[o2s].s,"BYTE")||!strcmp(ts[o2s].s,"WORD")||
         !strcmp(ts[o2s].s,"DWORD")||!strcmp(ts[o2s].s,"QWORD"))) o2s++;

    /* ---- Zero-operand ---- */
    if (!strcmp(mn,"HLT"))  { E(0xF4); return 0; }
    if (!strcmp(mn,"CLI"))  { E(0xFA); return 0; }
    if (!strcmp(mn,"STI"))  { E(0xFB); return 0; }
    if (!strcmp(mn,"NOP"))  { E(0x90); return 0; }
    if (!strcmp(mn,"RET"))  { E(0xC3); return 0; }
    if (!strcmp(mn,"RETF")) { E(0xCB); return 0; }
    if (!strcmp(mn,"PUSHA")){ E(0x60); return 0; }
    if (!strcmp(mn,"POPA")) { E(0x61); return 0; }
    if (!strcmp(mn,"LODSB")){ E(0xAC); return 0; }
    if (!strcmp(mn,"STOSB")){ E(0xAA); return 0; }
    if (!strcmp(mn,"CLD"))  { E(0xFC); return 0; }
    if (!strcmp(mn,"STD"))  { E(0xFD); return 0; }
    if (!strcmp(mn,"CBW"))  { E(0x98); return 0; }
    if (!strcmp(mn,"CWD"))  { E(0x99); return 0; }
    if (!strcmp(mn,"IRET")) { E(0xCF); return 0; }

    /* ---- INT imm8 ---- */
    if (!strcmp(mn,"INT")) { E(0xCD); E((uint8_t)xeval(ts,o1s,o1e)); return 0; }

    /* ---- PUSH / POP ---- */
    if (!strcmp(mn,"PUSH")) {
        int r = r16(ts[o1s].s); if (r>=0) { ospfx(0); E(0x50+r); return 0; }
            r = r32(ts[o1s].s); if (r>=0) { ospfx(1); E(0x50+r); return 0; }
        long long v = xeval(ts,o1s,o1e);
        if (v>=-128&&v<=127) { E(0x6A); E((uint8_t)(int8_t)v); }
        else                 { E(0x68); E16((uint16_t)v); }
        return 0;
    }
    if (!strcmp(mn,"POP")) {
        int r = r16(ts[o1s].s); if (r>=0) { ospfx(0); E(0x58+r); return 0; }
            r = r32(ts[o1s].s); if (r>=0) { ospfx(1); E(0x58+r); return 0; }
        fprintf(stderr,"POP: bad operand\n"); return -1;
    }

    /* ---- JMP: far (seg:off) or near ---- */
    if (!strcmp(mn,"JMP")) {
        /* Far jump: JMP seg:offset — look for a colon in the operand tokens */
        for (int i=o1s; i<o1e; i++) {
            if (ts[i].t==TCOL) {
                long long seg = xeval(ts,o1s,i);
                long long off = xeval(ts,i+1,o1e);
                E(0xEA); E16((uint16_t)off); E16((uint16_t)seg);
                return 0;
            }
        }
        /* Near JMP: rel16 in 16-bit mode, rel32 in 32-bit mode */
        long long tgt = xeval(ts,o1s,o1e);
        long long cur = (long long)(gorg+gpc);
        if (gbits==32) { E(0xE9); E32((uint32_t)(int32_t)(tgt-(cur+5))); }
        else           { E(0xE9); E16((uint16_t)(int16_t)(tgt-(cur+3))); }
        return 0;
    }

    /* ---- CALL near ---- */
    if (!strcmp(mn,"CALL")) {
        long long tgt = xeval(ts,o1s,o1e);
        long long cur = (long long)(gorg+gpc);
        if (gbits==32) { E(0xE8); E32((uint32_t)(int32_t)(tgt-(cur+5))); }
        else           { E(0xE8); E16((uint16_t)(int16_t)(tgt-(cur+3))); }
        return 0;
    }

    /* ---- Jcc short ---- */
    for (int i=0; JCC[i].m; i++) if (!strcmp(mn,JCC[i].m)) {
        long long tgt = xeval(ts,o1s,o1e);
        long long cur = (long long)(gorg+gpc);
        long long d   = tgt-(cur+2);
        if (gpass==2&&(d<-128||d>127)) {
            fprintf(stderr,"Error: %s target out of short range\n",mn); return -1;
        }
        E(JCC[i].op); E((uint8_t)(int8_t)d); return 0;
    }

    /* ---- IN / OUT ---- */
    if (!strcmp(mn,"IN")) {
        int al = !strcmp(ts[o1s].s,"AL");
        if (o2s>=0&&!strcmp(ts[o2s].s,"DX")) { E(al?0xEC:0xED); }
        else { E(al?0xE4:0xE5); E((uint8_t)xeval(ts,o2s,o2e)); }
        return 0;
    }
    if (!strcmp(mn,"OUT")) {
        int al = (o2s>=0&&!strcmp(ts[o2s].s,"AL"));
        if (!strcmp(ts[o1s].s,"DX")) { E(al?0xEE:0xEF); }
        else { E(al?0xE6:0xE7); E((uint8_t)xeval(ts,o1s,o1e)); }
        return 0;
    }

    /* ---- LGDT [mem16] ---- */
    if (!strcmp(mn,"LGDT")) {
        long long a = memop(ts,o1s,o1e);
        E(0x0F); E(0x01); E(0x16); E16((uint16_t)a);
        return 0;
    }

    /* ---- INC / DEC (single-operand: register or BYTE [mem]) ---- */
    if (!strcmp(mn,"INC")||!strcmp(mn,"DEC")) {
        int dc = !strcmp(mn,"DEC");
        int r = r16(ts[o1s].s); if (r>=0) { ospfx(0); E((dc?0x48:0x40)+r); return 0; }
            r = r32(ts[o1s].s); if (r>=0) { ospfx(1); E((dc?0x48:0x40)+r); return 0; }
            r = r8(ts[o1s].s);  if (r>=0) { E(0xFE); E(0xC0|(dc?8:0)|r); return 0; }
        /* BYTE [mem]: FE /0=INC /1=DEC, mod=00 rm=110 = direct addr */
        if (ts[o1s].t==TLB) {
            long long a = memop(ts,o1s,o1e);
            E(0xFE); E(dc?0x0E:0x06); E16((uint16_t)a); return 0;
        }
        fprintf(stderr,"%s: bad operand\n",mn); return -1;
    }

    /* ---- Two-operand instructions ---- */
    if (co < 0) { fprintf(stderr,"Error: '%s' missing operands\n",mn); return -1; }

    int d16=r16(ts[o1s].s), d8=r8(ts[o1s].s), d32=r32(ts[o1s].s), dsr=sreg(ts[o1s].s);
    int dm = (ts[o1s].t==TLB);
    long long da = dm ? memop(ts,o1s,o1e) : 0;

    int s16=-1, s8=-1, s32=-1, ssr=-1; long long imm=0;
    int sm = (o2s>=0 && ts[o2s].t==TLB);
    long long sa = sm ? memop(ts,o2s,o2e) : 0;
    if (!sm && o2s>=0) {
        s16=r16(ts[o2s].s); s8=r8(ts[o2s].s); s32=r32(ts[o2s].s); ssr=sreg(ts[o2s].s);
        if (s16<0 && s8<0 && s32<0 && ssr<0) imm=xeval(ts,o2s,o2e);
    }

    /* ---- MOV ---- */
    if (!strcmp(mn,"MOV")) {
        /* r8,  r8   */ if (d8>=0&&s8>=0)                      { E(0x8A); E(0xC0|(d8<<3)|s8); return 0; }
        /* r8,  imm8 */ if (d8>=0&&!sm&&s8<0&&s16<0&&s32<0)    { E(0xB0+d8); E((uint8_t)imm); return 0; }
        /* r16, r16  */ if (d16>=0&&s16>=0)                     { ospfx(0); E(0x89); E(0xC0|(s16<<3)|d16); return 0; }
        /* r32, r32  */ if (d32>=0&&s32>=0)                     { ospfx(1); E(0x89); E(0xC0|(s32<<3)|d32); return 0; }
        /* r16, imm  */ if (d16>=0&&!sm&&s8<0&&s16<0&&s32<0)   { ospfx(0); E(0xB8+d16); E16((uint16_t)imm); return 0; }
        /* r32, imm  */ if (d32>=0&&!sm&&s8<0&&s16<0&&s32<0)   { ospfx(1); E(0xB8+d32); E32((uint32_t)imm); return 0; }
        /* r16, [m]  */ if (d16>=0&&sm) {
            ospfx(0);
            if (d16==0) { E(0xA1); E16((uint16_t)sa); return 0; }
            E(0x8B); E(0x06|(d16<<3)); E16((uint16_t)sa); return 0;
        }
        /* r32, [m]  */ if (d32>=0&&sm) {
            ospfx(1); E(0x8B); E(0x06|(d32<<3)); E16((uint16_t)sa); return 0;
        }
        /* r8,  [m]  */ if (d8>=0&&sm) {
            if (d8==0) { E(0xA0); E16((uint16_t)sa); return 0; }
            E(0x8A); E(0x06|(d8<<3)); E16((uint16_t)sa); return 0;
        }
        /* [m], r32  */ if (dm&&s32>=0) {
            ospfx(1); E(0x89); E(0x06|(s32<<3)); E16((uint16_t)da); return 0;
        }
        /* [m], r16  */ if (dm&&s16>=0) {
            ospfx(0);
            if (s16==0) { E(0xA3); E16((uint16_t)da); return 0; }
            E(0x89); E(0x06|(s16<<3)); E16((uint16_t)da); return 0;
        }
        /* [m], r8   */ if (dm&&s8>=0) {
            if (s8==0) { E(0xA2); E16((uint16_t)da); return 0; }
            E(0x88); E(0x06|(s8<<3)); E16((uint16_t)da); return 0;
        }
        /* CR0, r32  */ if (!strcmp(ts[o1s].s,"CR0")&&s32>=0) { E(0x0F);E(0x22);E(0xC0|s32);return 0; }
        /* CR0, r16  */ if (!strcmp(ts[o1s].s,"CR0")&&s16>=0) { E(0x0F);E(0x22);E(0xC0|s16);return 0; }
        /* r32, CR0  */ if (d32>=0&&o2s>=0&&!strcmp(ts[o2s].s,"CR0")) { E(0x0F);E(0x20);E(0xC0|d32);return 0; }
        /* r16, CR0  */ if (d16>=0&&o2s>=0&&!strcmp(ts[o2s].s,"CR0")) { E(0x0F);E(0x20);E(0xC0|d16);return 0; }
        /* sreg, r32 */ if (dsr>=0&&s32>=0) { E(0x8E); E(0xC0|(dsr<<3)|s32); return 0; }
        /* sreg, r16 */ if (dsr>=0&&s16>=0) { E(0x8E); E(0xC0|(dsr<<3)|s16); return 0; }
        /* r16, sreg */ if (d16>=0&&ssr>=0) { E(0x8C); E(0xC0|(ssr<<3)|d16); return 0; }
        fprintf(stderr,"MOV: unsupported operand combination\n"); return -1;
    }

    /* ---- ALU: XOR AND OR ADD SUB CMP TEST ---- */
    for (int i=0; ALU[i].m; i++) if (!strcmp(mn,ALU[i].m)) {
        if (ALU[i].tst) {
            /* r, r */
            if (d8>=0&&s8>=0)   { E(0x84); E(0xC0|(s8<<3)|d8);   return 0; }
            if (d16>=0&&s16>=0) { ospfx(0); E(0x85); E(0xC0|(s16<<3)|d16); return 0; }
            if (d32>=0&&s32>=0) { ospfx(1); E(0x85); E(0xC0|(s32<<3)|d32); return 0; }
            /* r8, imm8 — A8 for AL, F6 /0 for others */
            if (d8>=0&&s8<0&&s16<0&&s32<0) {
                if (d8==0) { E(0xA8); E((uint8_t)imm); }
                else       { E(0xF6); E(0xC0|d8); E((uint8_t)imm); }
                return 0;
            }
            /* r16, imm16 — A9 for AX, F7 /0 for others */
            if (d16>=0&&s8<0&&s16<0&&s32<0) {
                ospfx(0);
                if (d16==0) { E(0xA9); E16((uint16_t)imm); }
                else        { E(0xF7); E(0xC0|d16); E16((uint16_t)imm); }
                return 0;
            }
            /* r32, imm32 — A9(+66) for EAX, F7 /0 for others */
            if (d32>=0&&s8<0&&s16<0&&s32<0) {
                ospfx(1);
                if (d32==0) { E(0xA9); E32((uint32_t)imm); }
                else        { E(0xF7); E(0xC0|d32); E32((uint32_t)imm); }
                return 0;
            }
            fprintf(stderr,"TEST: bad operands\n"); return -1;
        }
        if (d8>=0&&s8>=0)   { E(ALU[i].rr8);  E(0xC0|(s8<<3)|d8);  return 0; }
        if (d16>=0&&s16>=0) { ospfx(0); E(ALU[i].rr16); E(0xC0|(s16<<3)|d16); return 0; }
        if (d32>=0&&s32>=0) { ospfx(1); E(ALU[i].rr16); E(0xC0|(s32<<3)|d32); return 0; }
        if (d8>=0)  { E(0x80); E(0xC0|(ALU[i].grp<<3)|d8); E((uint8_t)imm); return 0; }
        if (d16>=0) {
            ospfx(0);
            if (imm>=-128&&imm<=127) { E(0x83); E(0xC0|(ALU[i].grp<<3)|d16); E((uint8_t)(int8_t)imm); }
            else                     { E(0x81); E(0xC0|(ALU[i].grp<<3)|d16); E16((uint16_t)imm); }
            return 0;
        }
        if (d32>=0) {
            ospfx(1);
            if (imm>=-128&&imm<=127) { E(0x83); E(0xC0|(ALU[i].grp<<3)|d32); E((uint8_t)(int8_t)imm); }
            else                     { E(0x81); E(0xC0|(ALU[i].grp<<3)|d32); E32((uint32_t)imm); }
            return 0;
        }
        fprintf(stderr,"%s: bad operands\n",mn); return -1;
    }

    /* ---- SHL SHR SAR ROL ROR ---- */
    for (int i=0; SH[i].m; i++) if (!strcmp(mn,SH[i].m)) {
        int r=r16(ts[o1s].s), rb=r8(ts[o1s].s), r3=r32(ts[o1s].s);
        int cl  = (o2s>=0 && !strcmp(ts[o2s].s,"CL"));
        int cnt = (int)(o2s>=0 ? xeval(ts,o2s,o2e) : 0);
        if (r>=0) {
            ospfx(0);
            if (cl)        { E(0xD3); E(0xC0|(SH[i].g<<3)|r); }
            else if(cnt==1){ E(0xD1); E(0xC0|(SH[i].g<<3)|r); }
            else           { E(0xC1); E(0xC0|(SH[i].g<<3)|r); E((uint8_t)cnt); }
            return 0;
        }
        if (r3>=0) {
            ospfx(1);
            if (cl)        { E(0xD3); E(0xC0|(SH[i].g<<3)|r3); }
            else if(cnt==1){ E(0xD1); E(0xC0|(SH[i].g<<3)|r3); }
            else           { E(0xC1); E(0xC0|(SH[i].g<<3)|r3); E((uint8_t)cnt); }
            return 0;
        }
        if (rb>=0) {
            if (cl)        { E(0xD2); E(0xC0|(SH[i].g<<3)|rb); }
            else if(cnt==1){ E(0xD0); E(0xC0|(SH[i].g<<3)|rb); }
            else           { E(0xC0); E(0xC0|(SH[i].g<<3)|rb); E((uint8_t)cnt); }
            return 0;
        }
        fprintf(stderr,"%s: bad operand\n",mn); return -1;
    }

    /* ---- MOVZX ---- */
    if (!strcmp(mn,"MOVZX")) {
        if (d16>=0&&s8>=0)  { ospfx(0); E(0x0F); E(0xB6); E(0xC0|(d16<<3)|s8);  return 0; }
        if (d32>=0&&s8>=0)  { ospfx(1); E(0x0F); E(0xB6); E(0xC0|(d32<<3)|s8);  return 0; }
        if (d32>=0&&s16>=0) { ospfx(1); E(0x0F); E(0xB7); E(0xC0|(d32<<3)|s16); return 0; }
        fprintf(stderr,"MOVZX: unsupported operand combination\n"); return -1;
    }

    fprintf(stderr,"Error: unknown mnemonic '%s'\n", mn); return -1;
}

/* ============================================================
 * Include Preprocessor
 * Reads a file and expands [INCLUDE "file"] recursively
 * into a single flat source buffer.
 * ============================================================ */
static char FSRC[MAX_SRC];
static size_t FSN = 0;

static void srcapp(const char *s, size_t l) {
    if (FSN + l >= MAX_SRC) { fputs("Error: source too large\n",stderr); exit(1); }
    memcpy(FSRC+FSN, s, l); FSN+=l; FSRC[FSN]=0;
}

static void dirof(const char *path, char *out, size_t sz) {
    const char *sl = strrchr(path,'/');
    if (!sl) sl = strrchr(path,'\\');
    if (!sl) { strncpy(out,".",sz-1); out[sz-1]=0; return; }
    size_t l = sl-path; if (l>=sz) l=sz-1;
    memcpy(out,path,l); out[l]=0;
}

static int preproc(const char *filepath, int depth);

static int procbuf(const char *src, const char *base, int depth) {
    const char *p = src;
    while (*p) {
        /* Skip leading whitespace to detect directive */
        const char *ls = p;
        while (*p==' '||*p=='\t') p++;

        /* [INCLUDE "file"] */
        if ((strncmp(p,"[INCLUDE",8)==0||strncmp(p,"[include",8)==0) &&
            (p[8]==' '||p[8]=='"')) {
            const char *q = p+8;
            while (*q==' '||*q=='\t') q++;
            if (*q=='"') {
                q++;
                const char *e = strchr(q,'"');
                if (e) {
                    char fn[512]; size_t fl=e-q; if(fl>=512)fl=511;
                    memcpy(fn,q,fl); fn[fl]=0;
                    char fp[1024]; snprintf(fp,sizeof fp,"%s/%s",base,fn);
                    if (preproc(fp,depth+1) != 0) return -1;
                    while (*p && *p!='\n') p++; /* skip rest of INCLUDE line */
                    if (*p=='\n') { srcapp("\n",1); p++; }
                    continue;
                }
            }
        }
        /* Copy line as-is */
        p = ls; /* rewind to include any leading whitespace */
        while (*p && *p!='\n') p++;
        srcapp(ls, (size_t)(p-ls));
        if (*p=='\n') { srcapp("\n",1); p++; }
    }
    return 0;
}

static int preproc(const char *filepath, int depth) {
    if (depth > MAX_INC) { fprintf(stderr,"Error: include too deep\n"); return -1; }
    FILE *f = fopen(filepath,"r");
    if (!f) { fprintf(stderr,"Error: cannot open '%s'\n",filepath); return -1; }
    char *rb = malloc(MAX_SRC);
    if (!rb) { fputs("Error: out of memory\n",stderr); return -1; }
    size_t n = fread(rb,1,MAX_SRC-1,f); rb[n]=0; fclose(f);
    char base[512]; dirof(filepath,base,sizeof base);
    int r = procbuf(rb,base,depth);
    free(rb); return r;
}

/* ============================================================
 * Assembly Pass
 * ============================================================ */
static int asmpass(const char *src) {
    LP = src; gpc = 0; gsec = 0;
    int line=1, err=0;

    while (*LP) {
        Tok ts[TMAX]; int n=gln(ts); int end=n-1;

        /* skip empty lines */
        if (n<=1) { line++; continue; }

        /* ---- EQU: name EQU expr  (no colon) ---- */
        if (ts[0].t==TW && n>2 && ts[1].t==TW && !strcmp(ts[1].s,"EQU")) {
            sset(ts[0].s, xeval(ts,2,end));
            line++; continue;
        }

        /* ---- Label definition ---- */
        int ti = 0;
        if (ts[0].t==TW && n>1 && ts[1].t==TCOL) {
            /* Standard:  name: [instruction] */
            sset(ts[0].s, (long long)(gorg + gpc));
            ti = 2;
        } else if (ts[0].t==TW && n>1 && ts[1].t==TW) {
            /* NASM allows label without colon before data: name DB/DW/DD/DQ/TIMES val */
            const char *nx = ts[1].s;
            if (!strcmp(nx,"DB")||!strcmp(nx,"DW")||!strcmp(nx,"DD")||
                !strcmp(nx,"DQ")||!strcmp(nx,"TIMES")) {
                sset(ts[0].s, (long long)(gorg + gpc));
                ti = 1; /* process directive starting at ts[1] */
            }
        }
        if (ti >= end) { line++; continue; }

        Tok *rt = ts+ti; int rn = n-ti;

        /* ---- Bracket directive: [ORG n] / [BITS n] / [INCLUDE "f"] ---- */
        if (rt[0].t == TLB) {
            int cl = 1; while (cl<rn && rt[cl].t!=TRB) cl++;
            if (rn>1 && rt[1].t==TW) {
                const char *dn = rt[1].s;
                if (!strcmp(dn,"ORG") || !strcmp(dn,"org"))
                    gorg = (size_t)xeval(rt,2,cl);
                else if (!strcmp(dn,"BITS")||!strcmp(dn,"bits"))
                    gbits = (int)xeval(rt,2,cl);
                else if (!strcmp(dn,"INCLUDE")||!strcmp(dn,"include"))
                    ; /* already flattened by preprocessor */
                else if (gpass==2)
                    fprintf(stderr,"Warning: unknown directive [%s] line %d\n",dn,line);
            }
            line++; continue;
        }

        /* ---- Plain directives ---- */
        if (rn>0 && rt[0].t==TW) {
            const char *d = rt[0].s;

            /* SECTION — reset $$ */
            if (!strcmp(d,"SECTION")) { gsec=gpc; line++; continue; }
            /* GLOBAL / EXTERN — ignored for flat binary */
            if (!strcmp(d,"GLOBAL")||!strcmp(d,"EXTERN")) { line++; continue; }

            /* DB — bytes and strings */
            if (!strcmp(d,"DB")) {
                int i=1;
                while (i < rn-1) {
                    if (rt[i].t==TC) { i++; continue; }
                    if (rt[i].t==TS) {
                        for (const char *c=rt[i].s; *c; c++) E((uint8_t)*c);
                        i++; continue;
                    }
                    int j=i; while (j<rn-1 && rt[j].t!=TC) j++;
                    E((uint8_t)xeval(rt,i,j)); i=j;
                }
                line++; continue;
            }

            /* DW — 16-bit words */
            if (!strcmp(d,"DW")) {
                int i=1;
                while (i < rn-1) {
                    if (rt[i].t==TC) { i++; continue; }
                    int j=i; while (j<rn-1 && rt[j].t!=TC) j++;
                    E16((uint16_t)xeval(rt,i,j)); i=j;
                }
                line++; continue;
            }

            /* DD — 32-bit dwords */
            if (!strcmp(d,"DD")) {
                int i=1;
                while (i < rn-1) {
                    if (rt[i].t==TC) { i++; continue; }
                    int j=i; while (j<rn-1 && rt[j].t!=TC) j++;
                    E32((uint32_t)xeval(rt,i,j)); i=j;
                }
                line++; continue;
            }

            /* DQ — 64-bit qwords */
            if (!strcmp(d,"DQ")) {
                int i=1;
                while (i < rn-1) {
                    if (rt[i].t==TC) { i++; continue; }
                    int j=i; while (j<rn-1 && rt[j].t!=TC) j++;
                    long long v=xeval(rt,i,j);
                    E32((uint32_t)(v & 0xFFFFFFFF));
                    E32((uint32_t)((unsigned long long)v >> 32));
                    i=j;
                }
                line++; continue;
            }

            /* TIMES count DB/DW/DD value */
            if (!strcmp(d,"TIMES")) {
                int dp=-1;
                for (int i=1; i<rn; i++)
                    if (rt[i].t==TW &&
                        (!strcmp(rt[i].s,"DB")||!strcmp(rt[i].s,"DW")||!strcmp(rt[i].s,"DD")))
                        { dp=i; break; }
                if (dp<0) {
                    fprintf(stderr,"Error: TIMES missing DB/DW/DD (line %d)\n",line);
                    err=1; break;
                }
                long long cnt = xeval(rt,1,dp);
                if (cnt < 0) cnt = 0;
                const char *dt = rt[dp].s;
                long long val = xeval(rt,dp+1,rn-1);
                for (long long j=0; j<cnt; j++) {
                    if      (!strcmp(dt,"DB")) E((uint8_t)val);
                    else if (!strcmp(dt,"DW")) E16((uint16_t)val);
                    else                       E32((uint32_t)val);
                }
                line++; continue;
            }
        }

        /* ---- Instruction ---- */
        if (enc(rt, rn) != 0) {
            fprintf(stderr,"  (line %d)\n", line);
            err=1; break;
        }
        line++;
    }
    return err ? -1 : 0;
}

/* ============================================================
 * File I/O
 * ============================================================ */
int inputBufferFull(char *buf, size_t sz) {
    FILE *f = fopen(inputFileName,"r");
    if (!f) { fprintf(stderr,"Error: cannot open '%s'\n",inputFileName); return EXIT_FAILURE; }
    size_t n = fread(buf,1,sz-1,f); buf[n]=0; fclose(f);
    inputBuffer = buf; return EXIT_SUCCESS;
}

/* ============================================================
 * CLI Argument Handling
 * ============================================================ */
int handleOption(const char *opt, const char *val) {
    if (!strcmp(opt,"-h")||!strcmp(opt,"--help")) {
        printf("Usage: proton [-f format] <input.asm> [-o output]\n");
        printf("  -f format    Output format: bin (default)\n");
        printf("  -o output    Output file\n");
        printf("  --file file  Input file\n");
        printf("  -h, --help   This help\n");
        exit(0);
    }
    if (!strcmp(opt,"-f"))     { outputFileFormat = val; return 0; }
    if (!strcmp(opt,"-o"))     { outputFileName = val; outputFileDefined = 1; return 0; }
    if (!strcmp(opt,"--file")) { inputFileName = val; return 0; }
    fprintf(stderr,"Error: unknown option '%s'\n",opt); return 1;
}

int checkArgs(int argc, char *argv[]) {
    for (int i=1; i<argc; i++) {
        if (argv[i][0]=='-') {
            if (!strcmp(argv[i],"-f")||!strcmp(argv[i],"-o")||!strcmp(argv[i],"--file")) {
                if (i+1>=argc) { fprintf(stderr,"Option '%s' needs a value\n",argv[i]); return 1; }
                if (handleOption(argv[i],argv[i+1])) return 1;
                i++;
            } else { if (handleOption(argv[i],NULL)) return 1; }
        } else inputFileName = argv[i];
    }
    if (!inputFileName)   { fprintf(stderr,"Error: no input file\n"); return 1; }
    if (!outputFileName)   outputFileName   = "a.bin";
    if (!outputFileFormat) outputFileFormat = "bin";
    return 0;
}

/* ============================================================
 * Assembler Driver
 * ============================================================ */
int handle(void) {
    /* Step 1: preprocess (flatten [INCLUDE] directives) */
    FSN = 0;
    if (preproc(inputFileName, 0) != 0) return EXIT_FAILURE;

    /* Step 2: pass 1 — collect labels and EQU symbols */
    gpass = 1; gbn = 0; gbits = 16; gorg = 0;
    if (asmpass(FSRC) != 0) return EXIT_FAILURE;

    /* Step 3: pass 2 — emit machine code */
    gpass = 2; gbn = 0; gbits = 16; gorg = 0;
    if (asmpass(FSRC) != 0) return EXIT_FAILURE;

    /* Step 4: write output */
    FILE *out = fopen(outputFileName,"wb");
    if (!out) { fprintf(stderr,"Error: cannot open '%s'\n",outputFileName); return EXIT_FAILURE; }
    fwrite(gbuf,1,gbn,out); fclose(out);
    printf("Assembled %zu byte%s -> %s\n", gbn, gbn==1?"":"s", outputFileName);
    return EXIT_SUCCESS;
}

/* ============================================================
 * Entry Point
 * ============================================================ */
int main(int argc, char *argv[]) {
    if (checkArgs(argc,argv)) return EXIT_FAILURE;
    return handle();
}
