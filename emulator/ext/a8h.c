/* a8h.c - a8dev harness for Atari800: a control socket plus deterministic
 * frame stepping, breakpoints, watchpoints, a guest log channel, input
 * injection and recording, screen hashes.
 *
 * #included at the end of memory.c (patch 0001), so it sees memory.c's
 * static extended-RAM buffers. Everything runs on the emulator's main
 * thread: commands are read between frames, or inside the CPU hook while
 * the CPU is stopped mid-frame. See emulator/patches/README.md.
 *
 * Copyright the a8dev authors. GPL-2.0-or-later, like Atari800.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
typedef SOCKET a8h_sock;
#define A8H_BADSOCK INVALID_SOCKET
#define a8h_close closesocket
#define getpid _getpid
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
typedef int a8h_sock;
#define A8H_BADSOCK (-1)
#define a8h_close close
#endif

#include "a8h.h"
#include "afile.h"
#include "antic.h"
#include "cartridge.h"
#include "colours.h"
#include "cpu.h"
#include "gtia.h"
#include "input.h"
#include "akey.h"
#include "pia.h"
#include "pokey.h"
#include "rtime.h"
#include "screen.h"
#include "sio.h"
#include "statesav.h"
#include "sysrom.h"
#include "util.h"
#ifdef SOUND
#include "sound.h"
#endif
#ifdef LIBATARI800
#include "libatari800/libatari800.h"
#include "libatari800/input.h"
#include "libatari800/cpu_crash.h"
#include "libatari800/statesav.h"
#include "platform.h"
#include <zlib.h>
#endif

#define A8H_VERSION "1"

int A8H_enabled = 0;
int A8H_cpu_check = 0;
jmp_buf A8H_jmpbuf;
void (*A8H_platform_idle)(int redraw) = NULL;

/* ------------------------------------------------------------------ */
/* options and global state */

static int opt_port = -1;
static char opt_host[64] = "127.0.0.1";
static int opt_free = 0;            /* start free-running (window default) */
static ULONG opt_seed = 0;          /* POKEY RANDOM seed after each power-on */

enum { MODE_PAUSED, MODE_FREE };
static int mode = MODE_PAUSED;
static int free_turbo = 0;

static long long frame = 0;         /* frames since the last load/power-on */
static long long cycles_base = 0;   /* machine cycles at the start of this frame */
static int in_cpu = 0;              /* inside the CPU hook (stopped mid-frame) */
static double last_fps = 0.0;
static char machine_name[16] = "800xl";
static char loaded_path[1024] = "";

/* a run: the emulator executes frames for a client (client >= 0) or
   free-runs (client < 0) */
static struct {
	int active;
	int client;
	long long max_frames;
	long long frames_done;
	int has_cond;
	long steps;                     /* -1: not stepping */
	int over;
	UWORD over_pc;
	UBYTE over_s;
	int to;                         /* run to address */
	UWORD to_pc;
	long long at;                   /* stop at the first boundary at/after this cycle (-1: off) */
	int nobreak;
	int until_signal;
	unsigned char until[256];
	int until_len;
	long long until_since;
	double t0;
} run;

/* a stop to make at the next instruction boundary (after a watched access,
   the log byte that completed a match, a signal) */
static struct {
	int active;
	char reason[16];
	char detail[160];
	int ea;             /* watch: the address, value reported at the stop */
} pend;

static char last_stop[640] = "why=none";
static long parent_pid = 0;     /* A8H_PARENT_WATCH: exit when the parent goes away */

static void check_parent(void)
{
#ifndef _WIN32
	if (parent_pid && (long) getppid() != parent_pid)
		exit(0);
#endif
}

/* ------------------------------------------------------------------ */
/* small helpers */

typedef struct {
	char *s;
	size_t len, cap;
} sbuf;

static void sb_grow(sbuf *b, size_t extra)
{
	if (b->len + extra + 1 > b->cap) {
		size_t n = b->cap ? b->cap : 256;
		while (n < b->len + extra + 1)
			n *= 2;
		b->s = (char *) Util_realloc(b->s, n);
		b->cap = n;
	}
}

static void sb_addf(sbuf *b, const char *fmt, ...)
{
	va_list ap;
	int n;
	char tmp[512];
	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t) n < sizeof(tmp)) {
		sb_grow(b, n);
		memcpy(b->s + b->len, tmp, n);
		b->len += n;
	}
	else {
		sb_grow(b, n);
		va_start(ap, fmt);
		vsnprintf(b->s + b->len, n + 1, fmt, ap);
		va_end(ap);
		b->len += n;
	}
	b->s[b->len] = 0;
}

static void sb_hex(sbuf *b, const UBYTE *p, size_t n)
{
	static const char hx[] = "0123456789abcdef";
	size_t i;
	sb_grow(b, n * 2);
	for (i = 0; i < n; i++) {
		b->s[b->len++] = hx[p[i] >> 4];
		b->s[b->len++] = hx[p[i] & 15];
	}
	b->s[b->len] = 0;
}

/* key="value with spaces" */
static void sb_kv_str(sbuf *b, const char *key, const char *v)
{
	sb_addf(b, " %s=\"", key);
	for (; *v; v++) {
		if (*v == '"' || *v == '\\')
			sb_addf(b, "\\%c", *v);
		else if (*v == '\n')
			sb_addf(b, "\\n");
		else
			sb_addf(b, "%c", *v);
	}
	sb_addf(b, "\"");
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* hex string -> bytes; returns length or -1 */
static long unhex(const char *s, UBYTE *out, long max)
{
	long n = 0;
	while (s[0] && s[1]) {
		int h = hexval(s[0]), l = hexval(s[1]);
		if (h < 0 || l < 0 || n >= max)
			return -1;
		out[n++] = (UBYTE) (h << 4 | l);
		s += 2;
	}
	return s[0] ? -1 : n;
}

/* numbers: 123, $7f, 0x7f, 7fh, %0101 */
static int parse_num(const char *s, long long *out)
{
	char *end;
	size_t n;
	int neg = 0;
	if (s == NULL || !*s)
		return 0;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	n = strlen(s);
	if (*s == '$')
		*out = strtoll(s + 1, &end, 16);
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		*out = strtoll(s + 2, &end, 16);
	else if (*s == '%')
		*out = strtoll(s + 1, &end, 2);
	else if (n > 1 && (s[n - 1] == 'h' || s[n - 1] == 'H')) {
		*out = strtoll(s, &end, 16);
		if (end == s + n - 1)
			end++;
	}
	else
		*out = strtoll(s, &end, 10);
	if (*end)
		return 0;
	if (neg)
		*out = -*out;
	return 1;
}

static unsigned long fnv1a(const UBYTE *p, size_t n)
{
	unsigned long h = 2166136261UL;
	size_t i;
	for (i = 0; i < n; i++) {
		h ^= p[i];
		h = (h * 16777619UL) & 0xffffffffUL;
	}
	return h;
}

static unsigned long screen_hash(void)
{
	return fnv1a((const UBYTE *) Screen_atari, Screen_WIDTH * Screen_HEIGHT);
}

static long long cycles_now(void)
{
	if (in_cpu)
		return cycles_base + (long long) ANTIC_ypos * ANTIC_LINE_C + ANTIC_XPOS;
	return cycles_base;
}

static int tv_lines(void)
{
	return Atari800_tv_mode;   /* 312 PAL, 262 NTSC */
}

/* ------------------------------------------------------------------ */
/* the guest log: bytes written to $D6FF, signals to $D6FE */

#define LOG_SIZE (4 * 1024 * 1024)
static UBYTE *log_ring = NULL;
static long long log_end = 0;       /* absolute offset of the next byte */
static long long log_mark = 0;      /* log_clear: older bytes are forgotten */
static long long signal_count = 0;
static int signal_last = -1;
static long long signal_frame = -1;

static long long log_base(void)
{
	long long b = log_end - LOG_SIZE;
	if (b < log_mark)
		b = log_mark;
	return b < 0 ? 0 : b;
}

static void set_pending(const char *reason, const char *fmt, ...)
{
	va_list ap;
	if (pend.active)
		return;
	pend.active = 1;
	pend.ea = -1;
	snprintf(pend.reason, sizeof(pend.reason), "%s", reason);
	va_start(ap, fmt);
	vsnprintf(pend.detail, sizeof(pend.detail), fmt, ap);
	va_end(ap);
	A8H_cpu_check = 1;
}

static void log_byte(UBYTE c)
{
	if (c == 0x9b)
		c = '\n';            /* ATASCII EOL */
	log_ring[log_end % LOG_SIZE] = c;
	log_end++;
	if (run.active && run.until_len > 0 && log_end - run.until_since >= run.until_len
	    && log_end - run.until_len >= log_base()) {
		int i;
		long long start = log_end - run.until_len;
		for (i = 0; i < run.until_len; i++)
			if (log_ring[(start + i) % LOG_SIZE] != run.until[i])
				break;
		if (i == run.until_len) {
			run.until_len = 0;
			set_pending("log", "match=%lld", start);
		}
	}
}

int A8H_D6Put(UWORD addr, UBYTE byte)
{
	if (!A8H_enabled)
		return 0;
	if (addr == 0xd6ff) {
		log_byte(byte);
		return 1;
	}
	if (addr == 0xd6fe) {
		signal_count++;
		signal_last = byte;
		signal_frame = frame;
		if (run.active && run.until_signal)
			set_pending("signal", "signal=%d", byte);
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* breakpoint conditions: a tiny expression language compiled to RPN
 *   A X Y S P PC  N V B D I Z C (flags, 0/1)  FRAME LINE XPOS BANK
 *   numbers ($hex 0xhex %bin dec), [addr] / PEEK(addr), DPEEK(addr)
 *   = == != <> < > <= >=  + - & | ^  AND OR NOT && || !  ( )
 */

enum {
	E_NUM, E_REG, E_PEEK, E_DPEEK, E_NOT,
	E_ADD, E_SUB, E_BAND, E_BOR, E_BXOR,
	E_EQ, E_NE, E_LT, E_GT, E_LE, E_GE, E_LAND, E_LOR
};
enum { R_A, R_X, R_Y, R_S, R_P, R_PC, R_N, R_V, R_B, R_D, R_I, R_Z, R_C,
       R_FRAME, R_LINE, R_XPOS, R_BANK };
static const char *const reg_names[] = { "A", "X", "Y", "S", "P", "PC", "N", "V", "B", "D", "I", "Z", "C",
                                         "FRAME", "LINE", "XPOS", "BANK", NULL };

#define EXPR_MAX 64
typedef struct {
	int op;
	long long v;
} enode;
typedef struct {
	enode n[EXPR_MAX];
	int len;
} expr_t;

typedef struct {
	const char *p;
	expr_t *e;
	int err;
	char tok[32];
	int tk;             /* 0 end, 1 num, 2 ident, 3 punct */
	long long num;
} eparser;

static void enext(eparser *ps)
{
	const char *p = ps->p;
	while (*p == ' ' || *p == '\t')
		p++;
	ps->tok[0] = 0;
	if (!*p) {
		ps->tk = 0;
	}
	else if (isdigit((unsigned char) *p) || *p == '$' || *p == '%') {
		int n = 0;
		char buf[40];
		if (*p == '$' || *p == '%')
			buf[n++] = *p++;
		while (isalnum((unsigned char) *p) && n < 38)
			buf[n++] = *p++;
		buf[n] = 0;
		if (!parse_num(buf, &ps->num))
			ps->err = 1;
		ps->tk = 1;
	}
	else if (isalpha((unsigned char) *p) || *p == '_') {
		int n = 0;
		while ((isalnum((unsigned char) *p) || *p == '_') && n < 30)
			ps->tok[n++] = (char) toupper((unsigned char) *p++);
		ps->tok[n] = 0;
		ps->tk = 2;
	}
	else {
		static const char *const two[] = { "==", "!=", "<>", "<=", ">=", "&&", "||", NULL };
		int i;
		ps->tk = 3;
		for (i = 0; two[i]; i++)
			if (p[0] == two[i][0] && p[1] == two[i][1]) {
				ps->tok[0] = p[0];
				ps->tok[1] = p[1];
				ps->tok[2] = 0;
				p += 2;
				break;
			}
		if (!two[i]) {
			ps->tok[0] = *p++;
			ps->tok[1] = 0;
		}
	}
	ps->p = p;
}

static void emit(eparser *ps, int op, long long v)
{
	if (ps->e->len >= EXPR_MAX) {
		ps->err = 1;
		return;
	}
	ps->e->n[ps->e->len].op = op;
	ps->e->n[ps->e->len].v = v;
	ps->e->len++;
}

static int is_tok(eparser *ps, const char *t)
{
	return ps->tk != 0 && strcmp(ps->tok, t) == 0;
}

static void p_or(eparser *ps);

static void p_primary(eparser *ps)
{
	if (ps->err)
		return;
	if (ps->tk == 1) {
		emit(ps, E_NUM, ps->num);
		enext(ps);
	}
	else if (is_tok(ps, "(")) {
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, ")")) { ps->err = 1; return; }
		enext(ps);
	}
	else if (is_tok(ps, "[")) {
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, "]")) { ps->err = 1; return; }
		enext(ps);
		emit(ps, E_PEEK, 0);
	}
	else if (is_tok(ps, "PEEK") || is_tok(ps, "DPEEK")) {
		int op = is_tok(ps, "PEEK") ? E_PEEK : E_DPEEK;
		enext(ps);
		if (!is_tok(ps, "(")) { ps->err = 1; return; }
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, ")")) { ps->err = 1; return; }
		enext(ps);
		emit(ps, op, 0);
	}
	else if (ps->tk == 2) {
		int i;
		for (i = 0; reg_names[i]; i++)
			if (strcmp(ps->tok, reg_names[i]) == 0)
				break;
		if (!reg_names[i]) { ps->err = 1; return; }
		emit(ps, E_REG, i);
		enext(ps);
	}
	else
		ps->err = 1;
}

static void p_not(eparser *ps)
{
	if (is_tok(ps, "NOT") || is_tok(ps, "!")) {
		enext(ps);
		p_not(ps);
		emit(ps, E_NOT, 0);
	}
	else
		p_primary(ps);
}

static void p_sum(eparser *ps)
{
	p_not(ps);
	while (!ps->err && ps->tk == 3 && (is_tok(ps, "+") || is_tok(ps, "-") || is_tok(ps, "&")
	                                   || is_tok(ps, "|") || is_tok(ps, "^"))) {
		int op = is_tok(ps, "+") ? E_ADD : is_tok(ps, "-") ? E_SUB : is_tok(ps, "&") ? E_BAND
		         : is_tok(ps, "|") ? E_BOR : E_BXOR;
		enext(ps);
		p_not(ps);
		emit(ps, op, 0);
	}
}

static void p_cmp(eparser *ps)
{
	p_sum(ps);
	if (!ps->err && ps->tk == 3) {
		int op = -1;
		if (is_tok(ps, "=") || is_tok(ps, "==")) op = E_EQ;
		else if (is_tok(ps, "!=") || is_tok(ps, "<>")) op = E_NE;
		else if (is_tok(ps, "<")) op = E_LT;
		else if (is_tok(ps, ">")) op = E_GT;
		else if (is_tok(ps, "<=")) op = E_LE;
		else if (is_tok(ps, ">=")) op = E_GE;
		if (op >= 0) {
			enext(ps);
			p_sum(ps);
			emit(ps, op, 0);
		}
	}
}

static void p_and(eparser *ps)
{
	p_cmp(ps);
	while (!ps->err && (is_tok(ps, "AND") || is_tok(ps, "&&"))) {
		enext(ps);
		p_cmp(ps);
		emit(ps, E_LAND, 0);
	}
}

static void p_or(eparser *ps)
{
	p_and(ps);
	while (!ps->err && (is_tok(ps, "OR") || is_tok(ps, "||"))) {
		enext(ps);
		p_and(ps);
		emit(ps, E_LOR, 0);
	}
}

static int expr_compile(const char *src, expr_t *e)
{
	eparser ps;
	memset(&ps, 0, sizeof(ps));
	ps.p = src;
	ps.e = e;
	e->len = 0;
	enext(&ps);
	p_or(&ps);
	if (ps.tk != 0)
		ps.err = 1;
	return !ps.err && e->len > 0;
}

static int cpu_xe_bank(void);

static long long reg_value(int r)
{
	switch (r) {
	case R_A: return CPU_regA;
	case R_X: return CPU_regX;
	case R_Y: return CPU_regY;
	case R_S: return CPU_regS;
	case R_P: return CPU_regP;
	case R_PC: return CPU_regPC;
	case R_N: return (CPU_regP >> 7) & 1;
	case R_V: return (CPU_regP >> 6) & 1;
	case R_B: return (CPU_regP >> 4) & 1;
	case R_D: return (CPU_regP >> 3) & 1;
	case R_I: return (CPU_regP >> 2) & 1;
	case R_Z: return (CPU_regP >> 1) & 1;
	case R_C: return CPU_regP & 1;
	case R_FRAME: return frame;
	case R_LINE: return ANTIC_ypos;
	case R_XPOS: return ANTIC_XPOS;
	case R_BANK: return cpu_xe_bank();
	}
	return 0;
}

static long long expr_eval(const expr_t *e)
{
	long long st[EXPR_MAX];
	int sp = 0, i;
	for (i = 0; i < e->len; i++) {
		const enode *n = &e->n[i];
		long long a, b;
		switch (n->op) {
		case E_NUM: st[sp++] = n->v; break;
		case E_REG: st[sp++] = reg_value((int) n->v); break;
		case E_PEEK: st[sp - 1] = MEMORY_SafeGetByte((UWORD) st[sp - 1]); break;
		case E_DPEEK:
			a = (UWORD) st[sp - 1];
			st[sp - 1] = MEMORY_SafeGetByte((UWORD) a) | (MEMORY_SafeGetByte((UWORD) (a + 1)) << 8);
			break;
		case E_NOT: st[sp - 1] = !st[sp - 1]; break;
		default:
			b = st[--sp];
			a = st[--sp];
			switch (n->op) {
			case E_ADD: a = a + b; break;
			case E_SUB: a = a - b; break;
			case E_BAND: a = a & b; break;
			case E_BOR: a = a | b; break;
			case E_BXOR: a = a ^ b; break;
			case E_EQ: a = a == b; break;
			case E_NE: a = a != b; break;
			case E_LT: a = a < b; break;
			case E_GT: a = a > b; break;
			case E_LE: a = a <= b; break;
			case E_GE: a = a >= b; break;
			case E_LAND: a = a && b; break;
			case E_LOR: a = a || b; break;
			}
			st[sp++] = a;
		}
	}
	return sp ? st[sp - 1] : 0;
}

/* ------------------------------------------------------------------ */
/* breakpoints and watchpoints */

#define MAX_BP 64
typedef struct {
	int used;
	long id;
	int pc;             /* -1: any */
	int has_cond;
	expr_t cond;
	char src[200];
	int once;
	long long hits;
	/* watchpoint */
	int watch;
	UWORD lo, hi;
	int kind;           /* 1 read, 2 write */
} bp_t;
static bp_t bps[MAX_BP];
static int n_bp = 0, n_wp = 0;

static void recount_bp(void)
{
	int i;
	n_bp = n_wp = 0;
	for (i = 0; i < MAX_BP; i++)
		if (bps[i].used) {
			if (bps[i].watch) n_wp++;
			else n_bp++;
		}
}

static void update_cpu_check(void)
{
	A8H_cpu_check = run.active && (pend.active || run.steps >= 0 || run.over || run.to || run.at >= 0
	                               || (!run.nobreak && (n_bp > 0 || n_wp > 0)));
}

/* opcode decode tables (generated by harness cpu6502.py: c_tables()) */
static const char a8h_opmode[257] =
	"ipipzzzzi#i#aaaarqiqxxxxiYiYXXXXapipzzzzi#i#aaaarqiqxxxxiYiYXXXX"
	"ipipzzzzi#i#aaaarqiqxxxxiYiYXXXXipipzzzzi#i#naaarqiqxxxxiYiYXXXX"
	"#p#pzzzzi#i#aaaarqiqxxyyiYiYXXYY#p#pzzzzi#i#aaaarqiqxxyyiYiYXXYY"
	"#p#pzzzzi#i#aaaarqiqxxxxiYiYXXXX#p#pzzzzi#i#aaaarqiqxxxxiYiYXXXX";
static const char a8h_opaccess[257] =
	"-R-MRRMM----RRMM-R-MRRMM-R-MRRMM-R-MRRMM----RRMM-R-MRRMM-R-MRRMM"
	"-R-MRRMM-----RMM-R-MRRMM-R-MRRMM-R-MRRMM-----RMM-R-MRRMM-R-MRRMM"
	"-W-WWWWW----WWWW-W-WWWWW-W-WWWWW-R-RRRRR----RRRR-R-RRRRR-R-RRRRR"
	"-R-MRRMM----RRMM-R-MRRMM-R-MRRMM-R-MRRMM----RRMM-R-MRRMM-R-MRRMM";

/* effective data address of the instruction at pc; returns access mask
   (1 read, 2 write) or 0 */
static int insn_access(UWORD pc, UWORD *ea)
{
	UBYTE op = MEMORY_dGetByte(pc);
	char acc = a8h_opaccess[op];
	UWORD lo = MEMORY_dGetByte((UWORD) (pc + 1));
	UWORD w = lo | (MEMORY_dGetByte((UWORD) (pc + 2)) << 8);
	UWORD a;
	if (acc == '-')
		return 0;
	switch (a8h_opmode[op]) {
	case 'z': a = lo; break;
	case 'x': a = (UBYTE) (lo + CPU_regX); break;
	case 'y': a = (UBYTE) (lo + CPU_regY); break;
	case 'a': a = w; break;
	case 'X': a = (UWORD) (w + CPU_regX); break;
	case 'Y': a = (UWORD) (w + CPU_regY); break;
	case 'p': {
		UBYTE zp = (UBYTE) (lo + CPU_regX);
		a = MEMORY_dGetByte(zp) | (MEMORY_dGetByte((UBYTE) (zp + 1)) << 8);
		break;
	}
	case 'q':
		a = (UWORD) ((MEMORY_dGetByte(lo) | (MEMORY_dGetByte((UBYTE) (lo + 1)) << 8)) + CPU_regY);
		break;
	default:
		return 0;
	}
	*ea = a;
	return acc == 'R' ? 1 : acc == 'W' ? 2 : 3;
}

/* ------------------------------------------------------------------ */
/* memory spaces */

static int cpu_xe_bank(void)
{
	UBYTE portb = PIA_PORTB | PIA_PORTB_mask;
	if (MEMORY_ram_size <= 64 || (portb & 0x10))
		return -1;
	return MEMORY_xe_bank - 1;
}

static int num_xe_banks(void)
{
	if (MEMORY_ram_size <= 64)
		return 0;
	if (MEMORY_ram_size == MEMORY_RAM_320_RAMBO || MEMORY_ram_size == MEMORY_RAM_320_COMPY_SHOP)
		return 16;
	return (MEMORY_ram_size - 64) / 16;
}

/* pointer to the byte of 'space' at addr, or NULL (hardware/no RAM).
   space 0: CPU view (handled by the caller), 1: RAM (under ROM), 2: XE bank */
static UBYTE *space_ptr(int space, int bank, UWORD addr)
{
	if (space == 2) {
		UWORD off = addr & 0x3fff;
		if (bank < 0 || bank >= num_xe_banks() || atarixe_memory == NULL)
			return NULL;
		if (bank == cpu_xe_bank())
			return &MEMORY_mem[0x4000 + off];
		return &atarixe_memory[((bank + 1) << 14) + off];
	}
	/* space 1: RAM as stored, whatever covers it */
	if (addr >= 0x4000 && addr < 0x8000 && cpu_xe_bank() >= 0 && atarixe_memory != NULL)
		return &atarixe_memory[addr - 0x4000];
	if (Atari800_machine_type == Atari800_MACHINE_XLXE) {
		if (addr >= 0xd000 && addr < 0xd800)
			return NULL;
		if (addr >= 0xc000 && MEMORY_attrib[addr] == MEMORY_ROM)
			return &under_atarixl_os[addr < 0xd000 ? addr - 0xc000 : addr - 0xc000];
		if (addr >= 0x5000 && addr < 0x5800 && MEMORY_selftest_enabled)
			return &under_atarixl_os[addr - 0x4000];
		if (addr >= 0xa000 && addr < 0xc000 && MEMORY_attrib[addr] == MEMORY_ROM)
			return &under_cartA0BF[addr - 0xa000];
	}
	if (MEMORY_attrib[addr] == MEMORY_HARDWARE)
		return NULL;
	if (MEMORY_attrib[addr] == MEMORY_ROM)
		return NULL;   /* no RAM under it on this machine */
	return &MEMORY_mem[addr];
}

static UBYTE space_get(int space, int bank, UWORD addr)
{
	UBYTE *p;
	if (space == 0)
		return MEMORY_SafeGetByte(addr);
	p = space_ptr(space, bank, addr);
	return p ? *p : 0xff;
}

static void space_put(int space, int bank, UWORD addr, UBYTE v)
{
	UBYTE *p;
	if (space == 0) {
		if (MEMORY_attrib[addr] == MEMORY_HARDWARE)
			MEMORY_HwPutByte(addr, v);
		else
			MEMORY_mem[addr] = v;   /* RAM, or patch the ROM image */
		return;
	}
	p = space_ptr(space, bank, addr);
	if (p)
		*p = v;
}

/* ------------------------------------------------------------------ */
/* input */

static int h_key = AKEY_NONE, h_shift = 0, h_consol = 0;   /* consol: pressed bits */
static UBYTE h_port[2] = { 0xff, 0xff };
static int h_trig = 0;                                      /* pressed bits */
static int f_active = 0, f_key = AKEY_NONE, f_shift = 0, f_consol = INPUT_CONSOL_NONE;
static UBYTE f_port[2] = { 0xff, 0xff };
static int f_trig = 0xf;                                    /* raw TRIG bits (1 = up) */

typedef struct {
	int key, shift, consol;
	UBYTE port[2];
	int trig;             /* raw bits, bit n = TRIG n (1 = not pressed) */
} instate;
static instate cur_in = { AKEY_NONE, 0, INPUT_CONSOL_NONE, { 0xff, 0xff }, 0xf };

void A8H_InputKeys(void)
{
	if (!A8H_enabled)
		return;
	if (f_active) {
		INPUT_key_code = f_key;
		INPUT_key_shift = f_shift;
		INPUT_key_consol = f_consol;
	}
	else {
		if (h_key != AKEY_NONE)
			INPUT_key_code = h_key;
		if (h_shift)
			INPUT_key_shift = 1;
		INPUT_key_consol &= ~h_consol & INPUT_CONSOL_NONE;
	}
	cur_in.key = INPUT_key_code;
	cur_in.shift = INPUT_key_shift;
	cur_in.consol = INPUT_key_consol;
}

int A8H_InputPort(int num, int value)
{
	if (!A8H_enabled || num < 0 || num > 1)
		return value;
	if (f_active)
		value = f_port[num];
	else
		value &= h_port[num];
	cur_in.port[num] = (UBYTE) value;
	return value;
}

int A8H_InputTrig(int num, int value)
{
	if (!A8H_enabled || num < 0 || num > 3)
		return value;
	if (f_active)
		value = (f_trig >> num) & 1;
	else if (h_trig & (1 << num))
		value = 0;
	if (value)
		cur_in.trig |= 1 << num;
	else
		cur_in.trig &= ~(1 << num);
	return value;
}

/* input recorder: one entry per change, at frame granularity (Atari800
   samples keyboard, sticks, triggers and console keys once per frame) */
typedef struct {
	long long frame;
	instate in;
} recent;
static recent *rec = NULL;
static long rec_n = 0, rec_cap = 0;
static int rec_on = 0;
static long long rec_start_frame = 0;
static instate rec_last;

static void rec_add(void)
{
	if (rec_n >= rec_cap) {
		rec_cap = rec_cap ? rec_cap * 2 : 1024;
		rec = (recent *) Util_realloc(rec, rec_cap * sizeof(recent));
	}
	rec[rec_n].frame = frame;
	rec[rec_n].in = cur_in;
	rec_n++;
	rec_last = cur_in;
}

/* per-frame hashes */
static unsigned long *hashes = NULL;
static long hash_n = 0, hash_cap = 0;
static long long hash_first = 0;
static int hash_on = 0;

/* ------------------------------------------------------------------ */
/* machines, power-on, loading */

typedef struct {
	const char *name;
	int type, ram, basic, leds, fkeys;
} machine_t;
static const machine_t machines[] = {
	{ "400", Atari800_MACHINE_800, 16, 0, 0, 0 },
	{ "800", Atari800_MACHINE_800, 48, 0, 0, 0 },
	{ "1200xl", Atari800_MACHINE_XLXE, 64, 0, 1, 1 },
	{ "600xl", Atari800_MACHINE_XLXE, 16, 1, 0, 0 },
	{ "800xl", Atari800_MACHINE_XLXE, 64, 1, 0, 0 },
	{ "65xe", Atari800_MACHINE_XLXE, 64, 1, 0, 0 },
	{ "130xe", Atari800_MACHINE_XLXE, 128, 1, 0, 0 },
	{ "320xe", Atari800_MACHINE_XLXE, MEMORY_RAM_320_COMPY_SHOP, 1, 0, 0 },
	{ NULL, 0, 0, 0, 0, 0 }
};

static void use_altirra_roms(void)
{
	SYSROM_os_versions[Atari800_MACHINE_800] = SYSROM_ALTIRRA_800;
	SYSROM_os_versions[Atari800_MACHINE_XLXE] = SYSROM_ALTIRRA_XL;
	SYSROM_basic_version = SYSROM_ALTIRRA_BASIC;
}

static void reset_counters(void)
{
	frame = 0;
	cycles_base = 0;
	hash_n = 0;
	hash_first = 0;
	rec_n = 0;
	rec_on = 0;
}

/* a defined power-on state: RAM zeroed, chip registers cleared, RANDOM
   seeded, so a boot does not depend on what ran before */
static void power_on(void)
{
	int i;
	use_altirra_roms();
	Atari800_InitialiseMachine();
	for (i = 0; i < 0x10000; i++)
		if (MEMORY_attrib[i] == MEMORY_RAM)
			MEMORY_mem[i] = 0;
	if (atarixe_memory != NULL)
		memset(atarixe_memory, 0, atarixe_memory_size);
	memset(under_atarixl_os, 0, sizeof(under_atarixl_os));
	memset(under_cartA0BF, 0, sizeof(under_cartA0BF));
	for (i = 0; i < 0x1e; i++)
		GTIA_PutByte((UWORD) (0xd000 + i), 0);
	GTIA_PutByte(0xd01e, 0);   /* HITCLR */
	for (i = 0; i < 9; i++)
		POKEY_PutByte((UWORD) (0xd200 + i), 0);
	POKEY_PutByte(0xd20e, 0);  /* IRQEN */
	POKEY_PutByte(0xd20f, 0);  /* SKCTL */
	POKEY_SetRandomCounter(opt_seed);
	MEMORY_dPutByte(0x244, 1); /* coldstart flag */
	RTIME_enabled = 0;
	srand(0);
	ANTIC_xpos = 0;
	reset_counters();
}

static void unmount_all(void)
{
	int i;
	for (i = 1; i <= 8; i++)
		SIO_Dismount(i);
	CARTRIDGE_Remove();
}

static int set_machine(const char *name, const char *tv, int basic)
{
	const machine_t *m;
	for (m = machines; m->name; m++)
		if (strcmp(m->name, name) == 0)
			break;
	if (!m->name)
		return 0;
	Atari800_SetMachineType(m->type);
	MEMORY_ram_size = m->ram;
	Atari800_builtin_basic = m->basic;
	Atari800_keyboard_leds = m->leds;
	Atari800_f_keys = m->fkeys;
	Atari800_jumper = FALSE;
	Atari800_builtin_game = FALSE;
	Atari800_keyboard_detached = FALSE;
	if (basic >= 0)
		Atari800_disable_basic = !basic;
	if (tv && *tv)
		Atari800_SetTVMode(strcmp(tv, "ntsc") == 0 ? Atari800_TV_NTSC : Atari800_TV_PAL);
	snprintf(machine_name, sizeof(machine_name), "%s", m->name);
	return 1;
}

/* ------------------------------------------------------------------ */
/* sockets */

#define MAX_CLIENTS 8
typedef struct {
	a8h_sock fd;
	char *buf;
	size_t len, cap;
} client_t;
static a8h_sock listen_fd = A8H_BADSOCK;
static client_t clients[MAX_CLIENTS];
static int bound_port = 0;

static void client_drop(int i)
{
	if (clients[i].fd != A8H_BADSOCK)
		a8h_close(clients[i].fd);
	clients[i].fd = A8H_BADSOCK;
	clients[i].len = 0;
}

static void send_reply(int ci, const char *s, size_t n)
{
	size_t off = 0;
	if (ci < 0 || ci >= MAX_CLIENTS || clients[ci].fd == A8H_BADSOCK)
		return;
	while (off < n) {
		long w = (long) send(clients[ci].fd, s + off, (int) (n - off), 0);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			client_drop(ci);
			return;
		}
		off += (size_t) w;
	}
}

static void reply_sb(int ci, sbuf *b)
{
	sb_grow(b, 1);
	b->s[b->len++] = '\n';
	b->s[b->len] = 0;
	send_reply(ci, b->s, b->len);
}

static void reply_err(int ci, const char *fmt, ...)
{
	char tmp[600];
	va_list ap;
	int n;
	strcpy(tmp, "ERR ");
	va_start(ap, fmt);
	vsnprintf(tmp + 4, sizeof(tmp) - 6, fmt, ap);
	va_end(ap);
	n = (int) strlen(tmp);
	tmp[n++] = '\n';
	send_reply(ci, tmp, n);
}

static int open_listen(void)
{
	struct sockaddr_in sa;
	socklen_t sl = sizeof(sa);
	int one = 1;
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == A8H_BADSOCK)
		return 0;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short) opt_port);
	sa.sin_addr.s_addr = inet_addr(opt_host);
	if (bind(listen_fd, (struct sockaddr *) &sa, sizeof(sa)) != 0 || listen(listen_fd, 4) != 0)
		return 0;
	getsockname(listen_fd, (struct sockaddr *) &sa, &sl);
	bound_port = ntohs(sa.sin_port);
	return 1;
}

static void accept_client(void)
{
	int i, one = 1;
	a8h_sock fd = accept(listen_fd, NULL, NULL);
	if (fd == A8H_BADSOCK)
		return;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].fd == A8H_BADSOCK)
			break;
	if (i == MAX_CLIENTS) {
		a8h_close(fd);
		return;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
	clients[i].fd = fd;
	clients[i].len = 0;
}

/* read what is available; timeout_ms < 0 blocks */
static void poll_sockets(int timeout_ms)
{
	struct pollfd pf[MAX_CLIENTS + 1];
	int idx[MAX_CLIENTS + 1];
	int n = 0, i, r;
	pf[n].fd = listen_fd;
	pf[n].events = POLLIN;
	idx[n++] = -1;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].fd != A8H_BADSOCK) {
			pf[n].fd = clients[i].fd;
			pf[n].events = POLLIN;
			idx[n++] = i;
		}
#ifdef _WIN32
	r = WSAPoll(pf, n, timeout_ms);
#else
	r = poll(pf, n, timeout_ms);
#endif
	if (r <= 0)
		return;
	for (i = 0; i < n; i++) {
		if (!(pf[i].revents & (POLLIN | POLLHUP | POLLERR)))
			continue;
		if (idx[i] < 0)
			accept_client();
		else {
			client_t *c = &clients[idx[i]];
			long got;
			if (c->len + 65536 > c->cap) {
				c->cap = c->cap ? c->cap * 2 : 131072;
				while (c->len + 65536 > c->cap)
					c->cap *= 2;
				c->buf = (char *) Util_realloc(c->buf, c->cap);
			}
			got = (long) recv(c->fd, c->buf + c->len, 65536, 0);
			if (got <= 0)
				client_drop(idx[i]);
			else
				c->len += (size_t) got;
		}
	}
}

/* take one complete line from client i (malloc'd) */
static char *take_line(int i)
{
	client_t *c = &clients[i];
	char *nl, *line;
	size_t n;
	if (c->fd == A8H_BADSOCK || c->len == 0)
		return NULL;
	nl = (char *) memchr(c->buf, '\n', c->len);
	if (!nl)
		return NULL;
	n = (size_t) (nl - c->buf);
	line = (char *) Util_malloc(n + 1);
	memcpy(line, c->buf, n);
	line[n] = 0;
	if (n && line[n - 1] == '\r')
		line[n - 1] = 0;
	memmove(c->buf, nl + 1, c->len - n - 1);
	c->len -= n + 1;
	return line;
}

/* ------------------------------------------------------------------ */
/* command arguments: cmd key=value key="quoted value" */

#define MAX_ARGS 24
typedef struct {
	char *cmd;
	int n;
	char *k[MAX_ARGS];
	char *v[MAX_ARGS];
} args_t;

static int parse_args(char *line, args_t *a)
{
	char *p = line;
	a->n = 0;
	while (*p == ' ')
		p++;
	a->cmd = p;
	while (*p && *p != ' ')
		p++;
	if (*p)
		*p++ = 0;
	for (;;) {
		char *k, *w;
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (a->n >= MAX_ARGS)
			return 0;
		k = p;
		while (*p && *p != '=' && *p != ' ')
			p++;
		if (*p != '=') {
			/* bare word: a flag */
			if (*p)
				*p++ = 0;
			a->k[a->n] = k;
			a->v[a->n++] = (char *) "1";
			continue;
		}
		*p++ = 0;
		a->k[a->n] = k;
		if (*p == '"') {
			w = ++p;
			a->v[a->n] = w;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1]) {
					p++;
					*w++ = *p == 'n' ? '\n' : *p;
					p++;
				}
				else
					*w++ = *p++;
			}
			if (*p == '"')
				p++;
			*w = 0;
		}
		else {
			a->v[a->n] = p;
			while (*p && *p != ' ')
				p++;
			if (*p)
				*p++ = 0;
		}
		a->n++;
	}
	return 1;
}

static const char *arg(const args_t *a, const char *k)
{
	int i;
	for (i = 0; i < a->n; i++)
		if (strcmp(a->k[i], k) == 0)
			return a->v[i];
	return NULL;
}

static int arg_num(const args_t *a, const char *k, long long *out)
{
	const char *v = arg(a, k);
	return v != NULL && parse_num(v, out);
}

static long long arg_num_def(const args_t *a, const char *k, long long def)
{
	long long v;
	return arg_num(a, k, &v) ? v : def;
}

static int arg_bool(const args_t *a, const char *k, int def)
{
	const char *v = arg(a, k);
	if (!v)
		return def;
	return !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0);
}

/* ------------------------------------------------------------------ */
/* status lines */

static void add_regs(sbuf *b)
{
	if (!in_cpu)
		CPU_GetStatus();
	sb_addf(b, " pc=%04x a=%02x x=%02x y=%02x s=%02x p=%02x", CPU_regPC, CPU_regA, CPU_regX,
	        CPU_regY, CPU_regS, CPU_regP);
}

static void add_position(sbuf *b)
{
	sb_addf(b, " frame=%lld cycles=%lld midframe=%d", frame, cycles_now(), in_cpu);
	if (in_cpu)
		sb_addf(b, " line=%d xpos=%d", ANTIC_ypos, ANTIC_XPOS);
}

/* end the current run and answer its client */
static void finish_run(const char *why, const char *detail)
{
	sbuf b = { NULL, 0, 0 };
	double dt = Util_time() - run.t0;
	if (dt > 0 && run.frames_done > 0)
		last_fps = run.frames_done / dt;
	sb_addf(&b, "why=%s ran=%lld", why, run.frames_done);
	add_position(&b);
	add_regs(&b);
	if (detail && *detail)
		sb_addf(&b, " %s", detail);
	sb_addf(&b, " log=%lld", log_end);
	snprintf(last_stop, sizeof(last_stop), "%s", b.s);
	if (run.client >= 0) {
		sbuf r = { NULL, 0, 0 };
		sb_addf(&r, "OK %s fps=%.1f speed=%.2f", b.s, last_fps,
		        last_fps / (Atari800_tv_mode == Atari800_TV_PAL ? 49.86 : 59.92));
		reply_sb(run.client, &r);
		free(r.s);
	}
	free(b.s);
	run.active = 0;
	run.until_len = 0;
	pend.active = 0;
	A8H_cpu_check = 0;
	Atari800_turbo = free_turbo;
}

static void start_run(int client)
{
	run.active = 1;
	run.client = client;
	run.frames_done = 0;
	run.t0 = Util_time();
	pend.active = 0;
	Atari800_turbo = client >= 0 ? TRUE : free_turbo;
	Atari800_turbo_speed = 0;
	update_cpu_check();
}

static void start_free_run(void)
{
	memset(&run, 0, sizeof(run));
	run.steps = -1;
	run.at = -1;
	run.max_frames = -1;
	start_run(-1);
}

/* ------------------------------------------------------------------ */
/* commands */

#define F_BOUNDARY 1    /* needs a frame boundary (longjmp out of a stopped frame) */

typedef void (*cmd_fn)(int ci, args_t *a);
typedef struct {
	const char *name;
	cmd_fn fn;
	int flags;
	const char *help;
} cmd_t;

static void serve(int block);
static char *pending_line = NULL;
static int pending_client = -1;

static void cmd_status(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	(void) a;
	sb_addf(&b, "OK mode=%s", run.active && run.client >= 0 ? "running" : mode == MODE_FREE ? "free" : "paused");
	sb_addf(&b, " machine=%s tv=%s", machine_name, Atari800_tv_mode == Atari800_TV_PAL ? "pal" : "ntsc");
	add_position(&b);
	add_regs(&b);
	sb_addf(&b, " log=%lld signals=%lld signal=%d signal_frame=%lld", log_end, signal_count, signal_last, signal_frame);
	sb_addf(&b, " fps=%.1f hashing=%d recording=%d breakpoints=%d watchpoints=%d", last_fps, hash_on, rec_on, n_bp, n_wp);
	sb_addf(&b, " xe_banks=%d cpu_bank=%d turbo=%d pid=%ld version=%s", num_xe_banks(), cpu_xe_bank(), free_turbo,
	        (long) getpid(), A8H_VERSION);
	sb_kv_str(&b, "loaded", loaded_path);
	sb_kv_str(&b, "last_stop", last_stop);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_run(int ci, args_t *a)
{
	long long v;
	const char *u;
	memset(&run, 0, sizeof(run));
	run.max_frames = arg_num_def(a, "frames", 1);
	if (run.max_frames <= 0) {
		reply_err(ci, "frames must be > 0");
		return;
	}
	run.steps = -1;
	if (arg_num(a, "steps", &v)) {
		/* stopped mid-frame, the current instruction runs without a hook call */
		run.steps = in_cpu ? v - 1 : v;
		run.has_cond = 1;
		if (run.steps < 0)
			run.steps = 0;
	}
	if (arg_bool(a, "over", 0)) {
		UWORD pc = CPU_regPC;
		if (MEMORY_dGetByte(pc) == 0x20) {
			run.over = 1;
			run.over_pc = (UWORD) (pc + 3);
			run.over_s = CPU_regS;
		}
		else
			run.steps = in_cpu ? 0 : 1;
		run.has_cond = 1;
	}
	run.at = -1;
	if (arg_num(a, "at", &v)) {
		/* a machine-cycle position (frames since load * cycles per frame + offset) */
		run.at = v;
		run.has_cond = 1;
	}
	if (arg_num(a, "to", &v)) {
		run.to = 1;
		run.to_pc = (UWORD) v;
		run.has_cond = 1;
	}
	u = arg(a, "until");
	if (u) {
		long n = unhex(u, run.until, sizeof(run.until));
		if (n <= 0) {
			reply_err(ci, "until: hex bytes expected");
			return;
		}
		run.until_len = (int) n;
		run.until_since = arg_num_def(a, "since", log_end);
		run.has_cond = 1;
	}
	run.until_signal = arg_bool(a, "until_signal", 0);
	if (run.until_signal)
		run.has_cond = 1;
	run.nobreak = arg_bool(a, "nobreak", 0);
	if (n_bp + n_wp > 0 && !run.nobreak)
		run.has_cond = 1;
	start_run(ci);
}

static void cmd_pause(int ci, args_t *a)
{
	(void) a;
	mode = MODE_PAUSED;
	if (run.active && run.client < 0)
		run.active = 0;
	Atari800_turbo = FALSE;
	send_reply(ci, "OK mode=paused\n", 15);
}

static void cmd_resume(int ci, args_t *a)
{
	mode = MODE_FREE;
	free_turbo = arg_bool(a, "turbo", 0);
	send_reply(ci, "OK mode=free\n", 13);
	if (!run.active)
		start_free_run();
}

static void cmd_reset(int ci, args_t *a)
{
	if (arg_bool(a, "hard", 1)) {
		unmount_all();
		power_on();
		loaded_path[0] = 0;
	}
	else
		Atari800_Warmstart();
	send_reply(ci, "OK\n", 3);
}

static void cmd_machine(int ci, args_t *a)
{
	const char *n = arg(a, "name");
	const char *tv = arg(a, "tv");
	sbuf b = { NULL, 0, 0 };
	if (!n && !tv && !arg(a, "basic")) {
		sb_addf(&b, "OK machine=%s tv=%s basic=%d", machine_name, Atari800_tv_mode == Atari800_TV_PAL ? "pal" : "ntsc",
		        !Atari800_disable_basic);
		reply_sb(ci, &b);
		free(b.s);
		return;
	}
	if (tv && strcmp(tv, "pal") != 0 && strcmp(tv, "ntsc") != 0) {
		reply_err(ci, "tv: pal or ntsc");
		return;
	}
	if (!set_machine(n ? n : machine_name, tv, arg(a, "basic") ? arg_bool(a, "basic", 0) : -1)) {
		reply_err(ci, "unknown machine %s (400 800 600xl 800xl 1200xl 65xe 130xe 320xe)", n);
		return;
	}
	unmount_all();
	power_on();
	loaded_path[0] = 0;
	sb_addf(&b, "OK machine=%s tv=%s", machine_name, Atari800_tv_mode == Atari800_TV_PAL ? "pal" : "ntsc");
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_load(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	int type;
	sbuf b = { NULL, 0, 0 };
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	if (arg(a, "basic"))
		Atari800_disable_basic = !arg_bool(a, "basic", 0);
	unmount_all();
	power_on();
	type = AFILE_OpenFile(path, TRUE, 1, arg_bool(a, "readonly", 0));
	if (type == AFILE_ERROR) {
		loaded_path[0] = 0;
		reply_err(ci, "cannot load %s", path);
		return;
	}
	snprintf(loaded_path, sizeof(loaded_path), "%s", path);
	sb_addf(&b, "OK type=%d", type);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_mount(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	long long d = arg_num_def(a, "drive", 1);
	if (!path || d < 1 || d > 8) {
		reply_err(ci, "path and drive=1..8 required");
		return;
	}
	if (!SIO_Mount((int) d, path, arg_bool(a, "readonly", 0))) {
		reply_err(ci, "cannot mount %s", path);
		return;
	}
	send_reply(ci, "OK\n", 3);
}

static void cmd_regs(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	char fl[9];
	int i;
	(void) a;
	sb_addf(&b, "OK");
	add_regs(&b);
	for (i = 0; i < 8; i++)
		fl[i] = (CPU_regP & (0x80 >> i)) ? "NV-BDIZC"[i] : '-';
	fl[8] = 0;
	sb_addf(&b, " flags=%s", fl);
	add_position(&b);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_setreg(int ci, args_t *a)
{
	const char *n = arg(a, "name");
	long long v;
	if (!n || !arg_num(a, "value", &v)) {
		reply_err(ci, "name and value required");
		return;
	}
	if (!in_cpu)
		CPU_GetStatus();
	if (!strcmp(n, "a") || !strcmp(n, "A")) CPU_regA = (UBYTE) v;
	else if (!strcmp(n, "x") || !strcmp(n, "X")) CPU_regX = (UBYTE) v;
	else if (!strcmp(n, "y") || !strcmp(n, "Y")) CPU_regY = (UBYTE) v;
	else if (!strcmp(n, "s") || !strcmp(n, "S") || !strcmp(n, "sp")) CPU_regS = (UBYTE) v;
	else if (!strcmp(n, "p") || !strcmp(n, "P")) CPU_regP = (UBYTE) (v | 0x30);
	else if (!strcmp(n, "pc") || !strcmp(n, "PC")) CPU_regPC = (UWORD) v;
	else {
		reply_err(ci, "unknown register %s", n);
		return;
	}
	if (!in_cpu)
		CPU_PutStatus();
	send_reply(ci, "OK\n", 3);
}

static int get_space(int ci, args_t *a, int *space, int *bank)
{
	const char *s = arg(a, "space");
	long long bk;
	*space = 0;
	*bank = -1;
	if (arg_num(a, "bank", &bk)) {
		*space = 2;
		*bank = (int) bk;
		if (bk < 0 || bk >= num_xe_banks()) {
			reply_err(ci, "bank %lld: this machine has %d extended banks", bk, num_xe_banks());
			return 0;
		}
		return 1;
	}
	if (!s || !strcmp(s, "cpu"))
		return 1;
	if (!strcmp(s, "ram")) {
		*space = 1;
		return 1;
	}
	reply_err(ci, "space: cpu, ram (or bank=N)");
	return 0;
}

static void cmd_peek(int ci, args_t *a)
{
	long long addr, len = arg_num_def(a, "len", 1);
	int space, bank;
	UBYTE *buf;
	long i;
	sbuf b = { NULL, 0, 0 };
	if (!arg_num(a, "addr", &addr) || len < 1 || len > 0x10000) {
		reply_err(ci, "addr and len=1..65536 required");
		return;
	}
	if (!get_space(ci, a, &space, &bank))
		return;
	if (space == 2 && addr >= 0x4000 && addr < 0x8000)
		addr -= 0x4000;
	if (space == 2 && addr + len > 0x4000) {
		reply_err(ci, "a bank is 16 KB (offset 0..$3FFF or $4000..$7FFF)");
		return;
	}
	buf = (UBYTE *) Util_malloc((size_t) len);
	for (i = 0; i < len; i++)
		buf[i] = space_get(space, bank, (UWORD) (addr + i));
	sb_addf(&b, "OK addr=%04llx len=%lld data=", addr & 0xffff, len);
	sb_hex(&b, buf, (size_t) len);
	reply_sb(ci, &b);
	free(b.s);
	free(buf);
}

static void cmd_poke(int ci, args_t *a)
{
	long long addr;
	int space, bank;
	UBYTE buf[4096];
	long n, i;
	const char *d = arg(a, "data");
	if (!arg_num(a, "addr", &addr) || !d || (n = unhex(d, buf, sizeof(buf))) <= 0) {
		reply_err(ci, "addr and data=HEX (up to 4096 bytes) required");
		return;
	}
	if (!get_space(ci, a, &space, &bank))
		return;
	if (space == 2 && addr >= 0x4000 && addr < 0x8000)
		addr -= 0x4000;
	for (i = 0; i < n; i++)
		space_put(space, bank, (UWORD) (addr + i), buf[i]);
	send_reply(ci, "OK\n", 3);
}

static void cmd_find(int ci, args_t *a)
{
	UBYTE pat[256];
	long n, hits = 0;
	long long start = arg_num_def(a, "start", 0), end = arg_num_def(a, "end", -1), maxh = arg_num_def(a, "max", 64), i;
	int space, bank, j;
	const char *d = arg(a, "data");
	sbuf b = { NULL, 0, 0 };
	if (!d || (n = unhex(d, pat, sizeof(pat))) <= 0) {
		reply_err(ci, "data=HEX required");
		return;
	}
	if (!get_space(ci, a, &space, &bank))
		return;
	if (end < 0)
		end = space == 2 ? 0x3fff : 0xffff;
	sb_addf(&b, "OK addrs=");
	for (i = start; i + n - 1 <= end && hits < maxh; i++) {
		for (j = 0; j < n; j++)
			if (space_get(space, bank, (UWORD) (i + j)) != pat[j])
				break;
		if (j == n) {
			sb_addf(&b, hits ? ",%04llx" : "%04llx", i);
			hits++;
		}
	}
	sb_addf(&b, " count=%ld", hits);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_hw(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	int i;
	(void) a;
	sb_addf(&b, "OK antic.dmactl=%02x antic.chactl=%02x antic.dlist=%04x antic.hscrol=%02x antic.vscrol=%02x"
	        " antic.pmbase=%02x antic.chbase=%02x antic.nmien=%02x antic.nmist=%02x antic.ypos=%d",
	        ANTIC_DMACTL, ANTIC_CHACTL, ANTIC_dlist, ANTIC_HSCROL, ANTIC_VSCROL, ANTIC_PMBASE, ANTIC_CHBASE,
	        ANTIC_NMIEN, ANTIC_NMIST, ANTIC_ypos);
	sb_addf(&b, " gtia.hposp=%02x,%02x,%02x,%02x gtia.hposm=%02x,%02x,%02x,%02x gtia.sizep=%02x,%02x,%02x,%02x gtia.sizem=%02x",
	        GTIA_HPOSP0, GTIA_HPOSP1, GTIA_HPOSP2, GTIA_HPOSP3, GTIA_HPOSM0, GTIA_HPOSM1, GTIA_HPOSM2, GTIA_HPOSM3,
	        GTIA_SIZEP0, GTIA_SIZEP1, GTIA_SIZEP2, GTIA_SIZEP3, GTIA_SIZEM);
	sb_addf(&b, " gtia.grafp=%02x,%02x,%02x,%02x gtia.grafm=%02x gtia.colpm=%02x,%02x,%02x,%02x"
	        " gtia.colpf=%02x,%02x,%02x,%02x gtia.colbk=%02x gtia.prior=%02x gtia.vdelay=%02x gtia.gractl=%02x",
	        GTIA_GRAFP0, GTIA_GRAFP1, GTIA_GRAFP2, GTIA_GRAFP3, GTIA_GRAFM, GTIA_COLPM0, GTIA_COLPM1, GTIA_COLPM2,
	        GTIA_COLPM3, GTIA_COLPF0, GTIA_COLPF1, GTIA_COLPF2, GTIA_COLPF3, GTIA_COLBK, GTIA_PRIOR, GTIA_VDELAY,
	        GTIA_GRACTL);
	sb_addf(&b, " gtia.trig=%d,%d,%d,%d gtia.consol=%x", GTIA_TRIG[0], GTIA_TRIG[1], GTIA_TRIG[2], GTIA_TRIG[3],
	        INPUT_key_consol);
	sb_addf(&b, " pokey.audf=");
	for (i = 0; i < 4; i++)
		sb_addf(&b, i ? ",%02x" : "%02x", POKEY_AUDF[i]);
	sb_addf(&b, " pokey.audc=");
	for (i = 0; i < 4; i++)
		sb_addf(&b, i ? ",%02x" : "%02x", POKEY_AUDC[i]);
	sb_addf(&b, " pokey.audctl=%02x pokey.kbcode=%02x pokey.irqen=%02x pokey.irqst=%02x pokey.skstat=%02x pokey.skctl=%02x pokey.random=%lu",
	        POKEY_AUDCTL[0], POKEY_KBCODE, POKEY_IRQEN, POKEY_IRQST, POKEY_SKSTAT, POKEY_SKCTL,
	        (unsigned long) POKEY_GetRandomCounter());
	sb_addf(&b, " pia.porta=%02x pia.portb=%02x pia.pactl=%02x pia.pbctl=%02x pia.porta_mask=%02x pia.portb_mask=%02x",
	        PIA_PORTA, PIA_PORTB, PIA_PACTL, PIA_PBCTL, PIA_PORTA_mask, PIA_PORTB_mask);
	sb_addf(&b, " mem.ram_kb=%d mem.xe_banks=%d mem.cpu_bank=%d mem.selftest=%d mem.basic=%d",
	        MEMORY_ram_size, num_xe_banks(), cpu_xe_bank(), MEMORY_selftest_enabled, MEMORY_cartA0BF_enabled);
	reply_sb(ci, &b);
	free(b.s);
}

static bp_t *bp_find(long id)
{
	int i;
	for (i = 0; i < MAX_BP; i++)
		if (bps[i].used && bps[i].id == id)
			return &bps[i];
	return NULL;
}

static bp_t *bp_new(int ci, long id)
{
	int i;
	bp_t *p = bp_find(id);
	if (!p)
		for (i = 0; i < MAX_BP; i++)
			if (!bps[i].used) {
				p = &bps[i];
				break;
			}
	if (!p) {
		reply_err(ci, "too many breakpoints (%d)", MAX_BP);
		return NULL;
	}
	memset(p, 0, sizeof(*p));
	p->id = id;
	p->pc = -1;
	return p;
}

static void cmd_break(int ci, args_t *a)
{
	long long id, pc;
	const char *c = arg(a, "cond");
	bp_t *p;
	expr_t e;
	if (!arg_num(a, "id", &id)) {
		reply_err(ci, "id required");
		return;
	}
	if (c && !expr_compile(c, &e)) {
		reply_err(ci, "bad condition: %s", c);
		return;
	}
	if (!arg_num(a, "pc", &pc) && !c) {
		reply_err(ci, "pc or cond required");
		return;
	}
	if (!(p = bp_new(ci, (long) id)))
		return;
	p->used = 1;
	if (arg_num(a, "pc", &pc))
		p->pc = (int) (pc & 0xffff);
	if (c) {
		p->has_cond = 1;
		p->cond = e;
		snprintf(p->src, sizeof(p->src), "%s", c);
	}
	p->once = arg_bool(a, "once", 0);
	recount_bp();
	send_reply(ci, "OK\n", 3);
}

static void cmd_watch(int ci, args_t *a)
{
	long long id, addr, len = arg_num_def(a, "len", 1);
	const char *k = arg(a, "kind");
	bp_t *p;
	int kind = 3;
	if (!arg_num(a, "id", &id) || !arg_num(a, "addr", &addr) || len < 1) {
		reply_err(ci, "id, addr (and len) required");
		return;
	}
	if (k) {
		if (!strcmp(k, "r")) kind = 1;
		else if (!strcmp(k, "w")) kind = 2;
		else if (!strcmp(k, "rw")) kind = 3;
		else {
			reply_err(ci, "kind: r, w or rw");
			return;
		}
	}
	if (!(p = bp_new(ci, (long) id)))
		return;
	p->used = 1;
	p->watch = 1;
	p->lo = (UWORD) addr;
	p->hi = (UWORD) (addr + len - 1);
	p->kind = kind;
	recount_bp();
	send_reply(ci, "OK\n", 3);
}

static void cmd_delete(int ci, args_t *a)
{
	long long id;
	bp_t *p;
	if (arg_bool(a, "all", 0)) {
		memset(bps, 0, sizeof(bps));
		recount_bp();
		send_reply(ci, "OK\n", 3);
		return;
	}
	if (!arg_num(a, "id", &id) || !(p = bp_find((long) id))) {
		reply_err(ci, "no such breakpoint");
		return;
	}
	p->used = 0;
	recount_bp();
	send_reply(ci, "OK\n", 3);
}

static void cmd_breaks(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	int i, first = 1;
	(void) a;
	sb_addf(&b, "OK list=\"");
	for (i = 0; i < MAX_BP; i++) {
		bp_t *p = &bps[i];
		if (!p->used)
			continue;
		if (!first)
			sb_addf(&b, ";");
		first = 0;
		if (p->watch)
			sb_addf(&b, "%ld:watch:%04x-%04x:%s:%lld", p->id, p->lo, p->hi,
			        p->kind == 1 ? "r" : p->kind == 2 ? "w" : "rw", p->hits);
		else
			sb_addf(&b, "%ld:break:%d:%s:%lld", p->id, p->pc, p->src, p->hits);
	}
	sb_addf(&b, "\"");
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_log(int ci, args_t *a)
{
	long long base = log_base();
	long long since = arg_num_def(a, "since", base), mx = arg_num_def(a, "max", 65536), i, next;
	sbuf b = { NULL, 0, 0 };
	if (since < base)
		since = base;
	if (since > log_end)
		since = log_end;
	next = since + mx < log_end ? since + mx : log_end;
	sb_addf(&b, "OK base=%lld end=%lld since=%lld next=%lld data=", base, log_end, since, next);
	sb_grow(&b, (size_t) (next - since) * 2);
	for (i = since; i < next; i++) {
		UBYTE c = log_ring[i % LOG_SIZE];
		sb_hex(&b, &c, 1);
	}
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_log_clear(int ci, args_t *a)
{
	(void) a;
	log_mark = log_end;
	send_reply(ci, "OK\n", 3);
}

static void cmd_screen(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	(void) a;
	sb_addf(&b, "OK w=%d h=%d vx1=%d vy1=%d vx2=%d vy2=%d hash=%08lx data=", Screen_WIDTH, Screen_HEIGHT,
	        Screen_visible_x1, Screen_visible_y1, Screen_visible_x2, Screen_visible_y2, screen_hash());
	sb_hex(&b, (const UBYTE *) Screen_atari, Screen_WIDTH * Screen_HEIGHT);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_palette(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	UBYTE rgb[768];
	int i;
	(void) a;
	for (i = 0; i < 256; i++) {
		rgb[i * 3] = Colours_GetR(i);
		rgb[i * 3 + 1] = Colours_GetG(i);
		rgb[i * 3 + 2] = Colours_GetB(i);
	}
	sb_addf(&b, "OK data=");
	sb_hex(&b, rgb, sizeof(rgb));
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_screen_hash(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	(void) a;
	sb_addf(&b, "OK hash=%08lx frame=%lld", screen_hash(), frame);
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_hash(int ci, args_t *a)
{
	const char *act = arg(a, "action");
	sbuf b = { NULL, 0, 0 };
	if (!act || !strcmp(act, "status")) {
		sb_addf(&b, "OK on=%d first=%lld count=%ld", hash_on, hash_first, hash_n);
	}
	else if (!strcmp(act, "on")) {
		if (!hash_on) {
			hash_on = 1;
			hash_n = 0;
			hash_first = frame + 1;
		}
		sb_addf(&b, "OK on=1 first=%lld", hash_first);
	}
	else if (!strcmp(act, "off")) {
		hash_on = 0;
		sb_addf(&b, "OK on=0");
	}
	else if (!strcmp(act, "clear")) {
		hash_n = 0;
		hash_first = frame + 1;
		sb_addf(&b, "OK first=%lld", hash_first);
	}
	else if (!strcmp(act, "get")) {
		long long since = arg_num_def(a, "since", hash_first), i;
		long mx = (long) arg_num_def(a, "max", 100000), k = 0;
		if (since < hash_first)
			since = hash_first;
		sb_addf(&b, "OK first=%lld data=", since);
		for (i = since - hash_first; i < hash_n && k < mx; i++, k++)
			sb_addf(&b, "%08lx", hashes[i]);
		sb_addf(&b, " count=%ld", k);
	}
	else {
		reply_err(ci, "action: on off clear get status");
		return;
	}
	reply_sb(ci, &b);
	free(b.s);
}

static void add_instate(sbuf *b, const instate *s)
{
	sb_addf(b, "%d:%d:%x:%02x:%02x:%x", s->key, s->shift, s->consol, s->port[0], s->port[1], s->trig);
}

static void cmd_rec(int ci, args_t *a)
{
	const char *act = arg(a, "action");
	sbuf b = { NULL, 0, 0 };
	if (!act || !strcmp(act, "status")) {
		sb_addf(&b, "OK on=%d start=%lld count=%ld", rec_on, rec_start_frame, rec_n);
	}
	else if (!strcmp(act, "start")) {
		rec_on = 1;
		rec_n = 0;
		rec_start_frame = frame;
		memset(&rec_last, 0xff, sizeof(rec_last));
		sb_addf(&b, "OK start=%lld midframe=%d", frame, in_cpu);
	}
	else if (!strcmp(act, "stop")) {
		rec_on = 0;
		sb_addf(&b, "OK count=%ld end=%lld", rec_n, frame);
	}
	else if (!strcmp(act, "get")) {
		long i, since = (long) arg_num_def(a, "since", 0);
		sb_addf(&b, "OK start=%lld end=%lld count=%ld data=", rec_start_frame, frame, rec_n);
		for (i = since < 0 ? 0 : since; i < rec_n; i++) {
			if (i > since)
				sb_addf(&b, ",");
			sb_addf(&b, "%lld:", rec[i].frame);
			add_instate(&b, &rec[i].in);
		}
	}
	else {
		reply_err(ci, "action: start stop get status");
		return;
	}
	reply_sb(ci, &b);
	free(b.s);
}

static void cmd_input(int ci, args_t *a)
{
	long long v;
	sbuf b = { NULL, 0, 0 };
	if (arg(a, "raw")) {
		f_active = arg_bool(a, "raw", 0);
		if (arg_num(a, "key", &v)) f_key = (int) v;
		if (arg_num(a, "shift", &v)) f_shift = (int) v;
		if (arg_num(a, "consol", &v)) f_consol = (int) v;
		if (arg_num(a, "port0", &v)) f_port[0] = (UBYTE) v;
		if (arg_num(a, "port1", &v)) f_port[1] = (UBYTE) v;
		if (arg_num(a, "trig", &v)) f_trig = (int) v;
	}
	else {
		if (arg_num(a, "key", &v)) h_key = (int) v;
		if (arg_num(a, "shift", &v)) h_shift = (int) v;
		if (arg_num(a, "consol", &v)) h_consol = (int) v & 7;
		if (arg_num(a, "port0", &v)) h_port[0] = (UBYTE) v;
		if (arg_num(a, "port1", &v)) h_port[1] = (UBYTE) v;
		if (arg_num(a, "trig", &v)) h_trig = (int) v & 0xf;
	}
	sb_addf(&b, "OK key=%d shift=%d consol=%d port0=%02x port1=%02x trig=%x raw=%d seen=", h_key, h_shift,
	        h_consol, h_port[0], h_port[1], h_trig, f_active);
	add_instate(&b, &cur_in);
	reply_sb(ci, &b);
	free(b.s);
}

/* libatari800 keeps states in memory: save through its buffer and write the
   file here; loads accept Atari800's gzip files and plain ones */
#ifdef LIBATARI800
static UBYTE state_buf[STATESAV_MAX_SIZE];
static statesav_tags_t state_tags;

static int state_save(const char *path)
{
	FILE *f;
	ULONG n;
	LIBATARI800_StateSav_buffer = state_buf;
	LIBATARI800_StateSav_tags = &state_tags;
	if (!StateSav_SaveAtariState(path, "wb", TRUE))
		return 0;
	n = StateSav_Tell();
	f = fopen(path, "wb");
	if (!f)
		return 0;
	if (fwrite(state_buf, 1, n, f) != n) {
		fclose(f);
		return 0;
	}
	return fclose(f) == 0;
}

static int state_load(const char *path)
{
	gzFile g = gzopen(path, "rb");
	int n;
	if (!g)
		return 0;
	memset(state_buf, 0, sizeof(state_buf));
	n = gzread(g, state_buf, sizeof(state_buf));
	gzclose(g);
	if (n <= 8)
		return 0;
	LIBATARI800_StateSav_buffer = state_buf;
	return StateSav_ReadAtariState(path, "rb");
}
#else
static int state_save(const char *path)
{
	return StateSav_SaveAtariState(path, "wb", TRUE);
}

static int state_load(const char *path)
{
	return StateSav_ReadAtariState(path, "rb");
}
#endif

static void cmd_savestate(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	char side[1100];
	FILE *f;
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	/* Mid-frame (stopped at a breakpoint), the state holds the CPU, memory and
	   chips as they are, but Atari800 cannot resume ANTIC inside a frame: a load
	   starts a new frame at line 0 with the CPU where it was. */
	if (!state_save(path)) {
		reply_err(ci, "cannot save %s", path);
		return;
	}
	snprintf(side, sizeof(side), "%s.a8h", path);
	f = fopen(side, "w");
	if (f) {
		fprintf(f, "frame=%lld\ncycles=%lld\nmidframe=%d\nmachine=%s\nloaded=%s\n", frame, cycles_base, in_cpu,
		        machine_name, loaded_path);
		fclose(f);
	}
	if (in_cpu)
		send_reply(ci, "OK midframe=1\n", 14);
	else
		send_reply(ci, "OK midframe=0\n", 14);
}

static void cmd_loadstate(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	char side[1100], line[1100];
	FILE *f;
	int loaded_midframe = 0;
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	if (!state_load(path)) {
		reply_err(ci, "cannot load state %s", path);
		return;
	}
	reset_counters();
	snprintf(side, sizeof(side), "%s.a8h", path);
	f = fopen(side, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			size_t n = strlen(line);
			if (n && line[n - 1] == '\n')
				line[--n] = 0;
			if (!strncmp(line, "frame=", 6)) frame = atoll(line + 6);
			else if (!strncmp(line, "cycles=", 7)) cycles_base = atoll(line + 7);
			else if (!strncmp(line, "midframe=", 9)) loaded_midframe = atoi(line + 9);
			else if (!strncmp(line, "machine=", 8)) snprintf(machine_name, sizeof(machine_name), "%s", line + 8);
			else if (!strncmp(line, "loaded=", 7)) snprintf(loaded_path, sizeof(loaded_path), "%s", line + 7);
		}
		fclose(f);
	}
	if (loaded_midframe)
		cycles_base = frame * tv_lines() * ANTIC_LINE_C;   /* the frame restarts at line 0 */
	send_reply(ci, loaded_midframe ? "OK midframe=1\n" : "OK midframe=0\n", 14);
}

static void cmd_quit(int ci, args_t *a)
{
	(void) a;
	send_reply(ci, "OK bye\n", 7);
	exit(0);
}

static void cmd_help(int ci, args_t *a);

static const cmd_t commands[] = {
	{ "status", cmd_status, 0, "mode, position, registers, log end, last_stop" },
	{ "run", cmd_run, 0, "frames=N [steps=N] [over] [to=ADDR] [until=HEX since=OFF] [until_signal] [nobreak]" },
	{ "pause", cmd_pause, 0, "stop free-running at the next frame boundary" },
	{ "resume", cmd_resume, 0, "[turbo=1] free-run (real time, or as fast as possible)" },
	{ "reset", cmd_reset, F_BOUNDARY, "[hard=1] power-on (RAM cleared, drives emptied) or hard=0: RESET key" },
	{ "machine", cmd_machine, F_BOUNDARY, "[name=800xl|130xe|800|400|...] [tv=pal|ntsc] [basic=0/1]: power-on" },
	{ "load", cmd_load, F_BOUNDARY, "path=FILE [basic=0/1]: power-on and boot a XEX/ATR/CAR/BAS" },
	{ "mount", cmd_mount, 0, "path=ATR drive=1..8 [readonly=1]" },
	{ "regs", cmd_regs, 0, "A X Y S P PC, flags" },
	{ "setreg", cmd_setreg, 0, "name=a|x|y|s|p|pc value=N" },
	{ "peek", cmd_peek, 0, "addr=A len=N [space=cpu|ram] [bank=N]" },
	{ "poke", cmd_poke, 0, "addr=A data=HEX [space=cpu|ram] [bank=N]" },
	{ "find", cmd_find, 0, "data=HEX [start= end= max=] [space=] [bank=]" },
	{ "hw", cmd_hw, 0, "ANTIC/GTIA/POKEY/PIA registers (write shadows) and memory config" },
	{ "break", cmd_break, 0, "id=N [pc=ADDR] [cond=EXPR] [once=1]" },
	{ "watch", cmd_watch, 0, "id=N addr=A [len=N] [kind=r|w|rw]" },
	{ "delete", cmd_delete, 0, "id=N | all" },
	{ "breaks", cmd_breaks, 0, "list breakpoints and watchpoints" },
	{ "log", cmd_log, 0, "[since=OFF] [max=N]: guest log bytes" },
	{ "log_clear", cmd_log_clear, 0, "forget the buffered log" },
	{ "screen", cmd_screen, 0, "the 384x240 frame buffer (palette indices)" },
	{ "palette", cmd_palette, 0, "the 256-colour palette (RGB)" },
	{ "screen_hash", cmd_screen_hash, 0, "FNV-1a hash of the frame buffer" },
	{ "hash", cmd_hash, 0, "action=on|off|clear|get|status [since=F]: per-frame hashes" },
	{ "rec", cmd_rec, 0, "action=start|stop|get|status: input recorder" },
	{ "input", cmd_input, 0, "[key= shift= consol= port0= port1= trig=] [raw=1 ...]" },
	{ "savestate", cmd_savestate, 0, "path=FILE (at a frame boundary)" },
	{ "loadstate", cmd_loadstate, F_BOUNDARY, "path=FILE" },
	{ "quit", cmd_quit, 0, "exit the emulator" },
	{ "help", cmd_help, 0, "this list" },
	{ NULL, NULL, 0, NULL }
};

static void cmd_help(int ci, args_t *a)
{
	sbuf b = { NULL, 0, 0 };
	const cmd_t *c;
	(void) a;
	sb_addf(&b, "OK commands=\"");
	for (c = commands; c->name; c++)
		sb_addf(&b, "%s%s: %s", c == commands ? "" : ";", c->name, c->help);
	sb_addf(&b, "\"");
	reply_sb(ci, &b);
	free(b.s);
}

/* execute one line. Returns 1 if it must be deferred to a frame boundary. */
static int exec_line(int ci, char *line)
{
	args_t a;
	const cmd_t *c;
	char *copy;
	if (!*line)
		return 0;
	copy = Util_strdup(line);
	if (!parse_args(copy, &a)) {
		reply_err(ci, "too many arguments");
		free(copy);
		return 0;
	}
	for (c = commands; c->name; c++)
		if (strcmp(c->name, a.cmd) == 0)
			break;
	if (!c->name) {
		reply_err(ci, "unknown command %s (try help)", a.cmd);
		free(copy);
		return 0;
	}
	if ((c->flags & F_BOUNDARY) && in_cpu) {
		free(copy);
		return 1;
	}
	c->fn(ci, &a);
	free(copy);
	return 0;
}

/* Process commands. block: wait until a run starts. */
static void serve(int block)
{
	int i, idle = 0;
	for (;;) {
		int did = 0;
		for (i = 0; i < MAX_CLIENTS && !run.active; i++) {
			char *line;
			while (!run.active && (line = take_line(i)) != NULL) {
				did = 1;
				if (exec_line(i, line)) {
					/* abandon the stopped frame: continue at the loop top */
					pending_line = line;
					pending_client = i;
					in_cpu = 0;
					longjmp(A8H_jmpbuf, 1);
				}
				free(line);
			}
		}
		if (run.active)
			return;
		if (!block) {
			poll_sockets(0);
			for (i = 0; i < MAX_CLIENTS; i++)
				if (clients[i].fd != A8H_BADSOCK && clients[i].len && memchr(clients[i].buf, '\n', clients[i].len))
					break;
			if (i == MAX_CLIENTS)
				return;
			continue;
		}
		if (!did) {
			check_parent();
			if (A8H_platform_idle) {
				poll_sockets(15);
				A8H_platform_idle(idle == 0);
				idle++;
			}
			else
				poll_sockets(1000);
		}
	}
}

/* stopped at an instruction boundary inside a frame */
static void stop_here(const char *why, const char *detail)
{
	finish_run(why, detail);
	if (strcmp(why, "frames") != 0)
		mode = MODE_PAUSED;
	in_cpu = 1;
#if defined(SOUND) && !defined(LIBATARI800)
	Sound_Pause();
#endif
	serve(1);
#if defined(SOUND) && !defined(LIBATARI800)
	Sound_Continue();
#endif
	in_cpu = 0;
	update_cpu_check();
}

void A8H_CpuHook(void)
{
	UWORD pc = CPU_regPC, ea = 0;
	int i, acc;
	char det[160];
	if (!run.active) {
		A8H_cpu_check = 0;
		return;
	}
	in_cpu = 1;
	if (pend.active) {
		char r[16];
		snprintf(r, sizeof(r), "%s", pend.reason);
		if (pend.ea >= 0)
			snprintf(det, sizeof(det), "%s value=%02x", pend.detail, MEMORY_SafeGetByte((UWORD) pend.ea));
		else
			snprintf(det, sizeof(det), "%s", pend.detail);
		pend.active = 0;
		stop_here(r, det);
	}
	else if (run.steps >= 0) {
		if (run.steps == 0)
			stop_here("step", "");
		else
			run.steps--;
	}
	else if (run.over && pc == run.over_pc && CPU_regS >= run.over_s) {
		stop_here("step", "");
	}
	else if (run.to && pc == run.to_pc) {
		stop_here("to", "");
	}
	else if (run.at >= 0 && cycles_now() >= run.at) {
		run.at = -1;
		stop_here("at", "");
	}
	else if (!run.nobreak && n_bp > 0) {
		for (i = 0; i < MAX_BP; i++) {
			bp_t *p = &bps[i];
			if (!p->used || p->watch)
				continue;
			if (p->pc >= 0 && p->pc != pc)
				continue;
			if (p->has_cond && !expr_eval(&p->cond))
				continue;
			p->hits++;
			if (p->has_cond)
				snprintf(det, sizeof(det), "id=%ld cond=\"%s\"", p->id, p->src);
			else
				snprintf(det, sizeof(det), "id=%ld", p->id);
			if (p->once) {
				p->used = 0;
				recount_bp();
			}
			stop_here("break", det);
			break;
		}
	}
	/* the instruction about to run: does it touch a watched address? */
	if (run.active && !run.nobreak && n_wp > 0 && (acc = insn_access(CPU_regPC, &ea)) != 0) {
		for (i = 0; i < MAX_BP; i++) {
			bp_t *p = &bps[i];
			if (!p->used || !p->watch || !(p->kind & acc) || ea < p->lo || ea > p->hi)
				continue;
			p->hits++;
			set_pending("watch", "id=%ld addr=%04x access=%s insn_pc=%04x old=%02x", p->id, ea,
			            (acc & p->kind) == 3 ? "rw" : (acc & p->kind) == 2 ? "w" : "r", CPU_regPC,
			            MEMORY_SafeGetByte(ea));
			pend.ea = ea;
			break;
		}
	}
	in_cpu = 0;
	update_cpu_check();
}

int A8H_Cim(UBYTE insn)
{
	char det[64];
	if (!A8H_enabled)
		return 0;
	snprintf(det, sizeof(det), "opcode=%02x", insn);
	if (!run.active)
		start_free_run();
	in_cpu = 1;
	stop_here("cim", det);
	in_cpu = 0;
	return 1;
}

/* ------------------------------------------------------------------ */
/* the main loop */

void A8H_FrameStart(void)
{
	in_cpu = 0;
	if (pending_line) {
		char *l = pending_line;
		pending_line = NULL;
		ANTIC_xpos = 0;
#if defined(SOUND) && !defined(LIBATARI800)
		Sound_Continue();   /* paused by the stop we jumped out of */
#endif
		exec_line(pending_client, l);
		free(l);
	}
	for (;;) {
		if (!run.active) {
			if (mode == MODE_FREE)
				start_free_run();
			else
				serve(1);
		}
		else if (run.client < 0)
			serve(0);
		if (run.active)
			break;
	}
	update_cpu_check();
}

void A8H_FrameEnd(void)
{
	static double next_t = 0.0;
	frame++;
	cycles_base += (long long) tv_lines() * ANTIC_LINE_C;
	if (hash_on) {
		if (hash_n >= hash_cap) {
			hash_cap = hash_cap ? hash_cap * 2 : 4096;
			hashes = (unsigned long *) Util_realloc(hashes, hash_cap * sizeof(unsigned long));
		}
		hashes[hash_n++] = screen_hash();
	}
	if (rec_on) {
		long long f = frame;
		frame--;      /* the input was sampled during the frame that just ended */
		if (rec_n == 0 || memcmp(&cur_in, &rec_last, sizeof(instate)) != 0)
			rec_add();
		frame = f;
	}
	if ((frame & 63) == 0)
		check_parent();
	if (!run.active)
		return;
	run.frames_done++;
	if (run.client >= 0) {
		if (run.frames_done >= run.max_frames)
			finish_run(run.has_cond ? "frame_limit" : "frames", "");
	}
	else {
		double fps = Atari800_tv_mode == Atari800_TV_PAL ? 49.86 : 59.92;
		double dt = Util_time() - run.t0;
		if (dt > 0.5)
			last_fps = run.frames_done / dt;
#ifdef LIBATARI800
		/* free-running headless: pace to real time unless turbo */
		if (!free_turbo) {
			double now = Util_time();
			if (next_t == 0.0 || now - next_t > 0.25)
				next_t = now;
			next_t += 1.0 / fps;
			if (next_t > now)
				Util_sleep(next_t - now);
		}
#else
		(void) fps;
		(void) next_t;
#endif
	}
}

void A8H_Init(int *argc, char *argv[])
{
	int i, j;
	for (i = j = 1; i < *argc; i++) {
		if (strcmp(argv[i], "-a8h-port") == 0 && i + 1 < *argc) {
			opt_port = atoi(argv[++i]);
			A8H_enabled = 1;
		}
		else if (strcmp(argv[i], "-a8h-host") == 0 && i + 1 < *argc)
			snprintf(opt_host, sizeof(opt_host), "%s", argv[++i]);
		else if (strcmp(argv[i], "-a8h-free") == 0)
			opt_free = 1;
		else if (strcmp(argv[i], "-a8h-paused") == 0)
			opt_free = 0;
		else if (strcmp(argv[i], "-a8h-seed") == 0 && i + 1 < *argc)
			opt_seed = (ULONG) strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "-a8h-machine") == 0 && i + 1 < *argc)
			snprintf(machine_name, sizeof(machine_name), "%s", argv[++i]);
		else
			argv[j++] = argv[i];
	}
	*argc = j;
	argv[j] = NULL;
	if (A8H_enabled) {
		use_altirra_roms();
		srand(0);
	}
}

void A8H_Start(void)
{
	int i;
	if (!A8H_enabled)
		return;
	for (i = 0; i < MAX_CLIENTS; i++)
		clients[i].fd = A8H_BADSOCK;
	log_ring = (UBYTE *) Util_malloc(LOG_SIZE);
	if (!open_listen()) {
		fprintf(stderr, "a8h: cannot listen on %s:%d\n", opt_host, opt_port);
		exit(4);
	}
	{
		/* a defined power-on state for the machine chosen on the command line;
		   a file given on the command line is not used (load it with 'load') */
		const char *tv = Atari800_tv_mode == Atari800_TV_PAL ? "pal" : "ntsc";
		char nm[16];
		snprintf(nm, sizeof(nm), "%s", machine_name);
		if (!set_machine(nm, tv, -1))
			set_machine("800xl", tv, -1);
		unmount_all();
		power_on();
	}
	memset(&run, 0, sizeof(run));
	run.steps = -1;
	run.at = -1;
	mode = opt_free ? MODE_FREE : MODE_PAUSED;
	if (getenv("A8H_PARENT_WATCH"))
		parent_pid = (long) getppid();
	printf("A8H port=%d pid=%ld\n", bound_port, (long) getpid());
	fflush(stdout);
}

#ifdef LIBATARI800
int A8H_HeadlessMain(int argc, char *argv[])
{
	static input_template_t tmpl;
	A8H_Init(&argc, argv);
	if (!A8H_enabled) {
		fprintf(stderr, "usage: %s -a8h-port N [-a8h-free] [-a8h-seed N] [-a8h-machine 800xl|130xe|...] [atari800 options]\n",
		        argv[0]);
		return 2;
	}
	libatari800_continue_on_brk = TRUE;
	libatari800_clear_input_array(&tmpl);
	LIBATARI800_Input_array = &tmpl;
	if (!Atari800_Initialise(&argc, argv))
		return 3;
	A8H_Start();
	for (;;) {
		A8H_LOOP_TOP();
		INPUT_key_code = PLATFORM_Keyboard();
		Atari800_Frame();
		A8H_FrameEnd();
	}
	return 0;
}
#endif
