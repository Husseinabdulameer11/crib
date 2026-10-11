/*
 * crib - a small language for the maths of a cryptography course, built on
 * gram. Write the formulas the way they are written on paper, and let
 * `sim` run the textbook procedures step by step.
 *
 *   crib file.crib                 run a program
 *   crib --html out.html file.crib also write an interactive page with
 *                                  animated LFSRs, curve plots, ...
 *   crib -t file.crib              print the syntax tree
 *
 * The language (see README.md for the full tour):
 *
 *   p = 17; alpha = 3; a = 6        variables (semicolons optional)
 *   show alpha^a mod p              prints "alpha^a mod p = 15"
 *   m = t * r^-a mod p              everything left of `mod` is computed
 *                                   mod p: ^ is square-and-multiply, a
 *                                   negative power is an inverse, / is
 *                                   multiplication by an inverse
 *   check beta^k * m mod p == t     prints [ok] or [FAIL] with both sides
 *   g, x, y = egcd(7, 192)          tuples and destructuring
 *   fn h(x) = alpha^x mod p         one-line functions
 *   fn f(a, b) { ... return a }     block functions, if/else, while,
 *   for i in 1..10 { }              inclusive ranges
 *   f = poly(x^4 + x + 1)           polynomials over GF(2)
 *   sim lfsr(f)                     step-by-step simulations (crib_sim.c)
 *
 * crib.gram describes the syntax; gramgen turns it into crib_grammar.h,
 * which holds the rule ids used by the switch statements below.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crib.h"
#include "crib_grammar.h"

#ifndef CRIB_VERSION
#define CRIB_VERSION "dev"   /* set by the build: make VERSION=x.y.z */
#endif

#define TREE_MEM (4 * 1024 * 1024)
#define HEAP_MEM (32 * 1024 * 1024)
#define MAX_VARS 4096
#define MAX_FUNCS 256
#define MAX_ARGS 16
#define MAX_CALLS 500

/* output layer (crib_out.c) */
void out_set_source(const char *name, const char *text, size_t len);
int out_html_begin(void);
int out_html_write(const char *path);
void out_set_stmt(const char *code, size_t len, int line);

/* ---- memory ------------------------------------------------------------- */

static char *heap;
static size_t heap_used;

void *heap_alloc(size_t n)
{
    void *p;
    n = (n + 15) & ~(size_t)15;
    if (n > HEAP_MEM - heap_used) fail(NULL, "out of memory");
    p = heap + heap_used;
    heap_used += n;
    return p;
}

char *fmt(const char *f, ...)
{
    va_list ap;
    int n;
    char *p;
    va_start(ap, f);
    n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    p = (char *)heap_alloc((size_t)n + 1);
    va_start(ap, f);
    vsnprintf(p, (size_t)n + 1, f, ap);
    va_end(ap);
    return p;
}

/* Own formatter: %lld is not reliable on every Windows C runtime. */
const char *N(i64 x)
{
    static char buf[32][24];
    static int k;
    char tmp[24], *out = buf[k = (k + 1) & 31];
    unsigned long long u = x < 0 ? 0ULL - (unsigned long long)x : (unsigned long long)x;
    int n = 0, i = 0;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    if (x < 0) out[i++] = '-';
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
    return out;
}

/* ---- source text and errors --------------------------------------------- */

static const char *src_name, *src_text;
static size_t src_len;
static jmp_buf trap;

static int line_of(const gram_node *at)
{
    size_t line = 0;
    if (!at || !src_text || at->text < src_text || at->text > src_text + src_len) return 0;
    gram_locate(src_text, src_len, (size_t)(at->text - src_text), &line, NULL);
    return (int)line;
}

void fail(const gram_node *at, const char *f, ...)
{
    va_list ap;
    int line = line_of(at);
    fflush(stdout);
    if (line) fprintf(stderr, "%s:%d: ", src_name, line);
    fputs("error: ", stderr);
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
    longjmp(trap, 1);
}

#define NAME(n) (int)(n)->len, (n)->text

/* ---- values ------------------------------------------------------------- */

value v_nil(void) { value v; memset(&v, 0, sizeof v); return v; }
value v_int(i64 i) { value v = v_nil(); v.t = V_INT; v.i = i; return v; }
value v_poly(i64 b) { value v = v_nil(); v.t = V_POLY; v.i = b; return v; }
value v_str(const char *s, size_t n) { value v = v_nil(); v.t = V_STR; v.s = s; v.n = n; return v; }
value v_cstr(const char *s) { return v_str(s, strlen(s)); }

static value v_seq(vtype t, size_t n, const value *items)
{
    value v = v_nil();
    v.t = t;
    v.n = n;
    v.a = (value *)heap_alloc(n * sizeof(value) + 1);
    if (n) memcpy(v.a, items, n * sizeof(value));
    return v;
}
value v_tuple(size_t n, const value *items) { return v_seq(V_TUPLE, n, items); }
value v_list(size_t n, const value *items) { return v_seq(V_LIST, n, items); }

const char *vtype_name(vtype t)
{
    switch (t) {
    case V_NIL: return "nothing";
    case V_INT: return "a number";
    case V_STR: return "a string";
    case V_LIST: return "a list";
    case V_TUPLE: return "a tuple";
    default: return "a polynomial";
    }
}

const char *show_value(value v)
{
    size_t i;
    char *s;
    switch (v.t) {
    case V_NIL: return "O";
    case V_INT: return N(v.i);
    case V_POLY: return poly_text(v.i);
    case V_STR: return fmt("%.*s", (int)v.n, v.s);
    default:
        s = v.t == V_LIST ? "[" : "(";
        for (i = 0; i < v.n; i++) {
            value e = v.a[i];
            const char *t = show_value(e);
            if (e.t == V_STR) t = fmt("\"%s\"", t);
            s = fmt("%s%s%s", s, i ? ", " : "", t);
        }
        return fmt("%s%s", s, v.t == V_LIST ? "]" : ")");
    }
}

static int truthy(value v)
{
    switch (v.t) {
    case V_NIL: return 0;
    case V_INT: case V_POLY: return v.i != 0;
    default: return v.n != 0;
    }
}

static int equal(value a, value b)
{
    size_t i;
    if (a.t != b.t) return 0;
    switch (a.t) {
    case V_NIL: return 1;
    case V_INT: case V_POLY: return a.i == b.i;
    case V_STR: return a.n == b.n && memcmp(a.s, b.s, a.n) == 0;
    default:
        if (a.n != b.n) return 0;
        for (i = 0; i < a.n; i++) if (!equal(a.a[i], b.a[i])) return 0;
        return 1;
    }
}

/* ---- number theory -------------------------------------------------------- */

i64 add_ck(const gram_node *at, i64 a, i64 b)
{
    i64 r;
    if (__builtin_add_overflow(a, b, &r)) fail(at, "number too large (over 63 bits)");
    return r;
}

i64 mul_ck(const gram_node *at, i64 a, i64 b)
{
    i64 r;
    if (__builtin_mul_overflow(a, b, &r)) fail(at, "number too large (over 63 bits)");
    return r;
}

i64 mod_norm(i64 a, i64 m)
{
    i64 r = a % m;
    return r < 0 ? r + m : r;
}

i64 mulmod(i64 a, i64 b, i64 m)
{
#ifdef __SIZEOF_INT128__
    return (i64)(((__int128)mod_norm(a, m) * mod_norm(b, m)) % m);
#else
    unsigned long long x = (unsigned long long)mod_norm(a, m), y = (unsigned long long)mod_norm(b, m),
                       r = 0, um = (unsigned long long)m;
    while (y) {
        if (y & 1) { r += x; if (r >= um) r -= um; }
        x += x; if (x >= um) x -= um;
        y >>= 1;
    }
    return (i64)r;
#endif
}

i64 powmod(i64 a, i64 e, i64 m)
{
    i64 r = 1 % m;
    a = mod_norm(a, m);
    while (e > 0) {
        if (e & 1) r = mulmod(r, a, m);
        a = mulmod(a, a, m);
        e >>= 1;
    }
    return r;
}

i64 gcd_ll(i64 a, i64 b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { i64 t = a % b; a = b; b = t; }
    return a;
}

i64 egcd_ll(i64 a, i64 b, i64 *x, i64 *y)
{
    i64 r0 = a, r1 = b, s0 = 1, s1 = 0, t0 = 0, t1 = 1;
    while (r1 != 0) {
        i64 q = r0 / r1, t;
        t = r0 - q * r1; r0 = r1; r1 = t;
        t = s0 - q * s1; s0 = s1; s1 = t;
        t = t0 - q * t1; t0 = t1; t1 = t;
    }
    if (r0 < 0) { r0 = -r0; s0 = -s0; t0 = -t0; }
    *x = s0;
    *y = t0;
    return r0;
}

i64 inv_mod(const gram_node *at, i64 a, i64 m)
{
    i64 x, y, g;
    if (m <= 0) fail(at, "modulus must be positive");
    g = egcd_ll(mod_norm(a, m), m, &x, &y);
    if (g != 1) fail(at, "%s has no inverse mod %s (gcd = %s)", N(a), N(m), N(g));
    return mod_norm(x, m);
}

int is_prime(i64 n)
{
    static const i64 bases[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    i64 d;
    int s = 0, i, r;
    if (n < 2) return 0;
    for (i = 0; i < 12; i++) {
        if (n == bases[i]) return 1;
        if (n % bases[i] == 0) return 0;
    }
    for (d = n - 1; (d & 1) == 0; d >>= 1) s++;
    for (i = 0; i < 12; i++) { /* deterministic Miller-Rabin for 64 bits */
        i64 x = powmod(bases[i], d, n);
        if (x == 1 || x == n - 1) continue;
        for (r = 1; r < s; r++) {
            x = mulmod(x, x, n);
            if (x == n - 1) break;
        }
        if (r == s) return 0;
    }
    return 1;
}

int factorize(i64 n, i64 *p, int *e)
{
    int k = 0;
    i64 d;
    if (n < 0) n = -n;
    for (d = 2; d <= n / d; d += d == 2 ? 1 : 2) {
        if (n % d) continue;
        p[k] = d; e[k] = 0;
        while (n % d == 0) { n /= d; e[k]++; }
        k++;
        if (is_prime(n)) break;
    }
    if (n > 1) { p[k] = n; e[k] = 1; k++; }
    return k;
}

i64 phi_ll(i64 n)
{
    i64 p[64], r = n;
    int e[64], k = factorize(n, p, e), i;
    for (i = 0; i < k; i++) r = r / p[i] * (p[i] - 1);
    return r;
}

static i64 iroot(i64 c, int e)
{
    /* largest r with r^e <= c */
    i64 lo = 0, hi = 1;
    while (1) {
        i64 t = 1;
        int i, over = 0;
        for (i = 0; i < e && !over; i++) over = __builtin_mul_overflow(t, hi, &t);
        if (over || t > c) break;
        hi *= 2;
    }
    while (lo < hi) {
        i64 mid = lo + (hi - lo + 1) / 2, t = 1;
        int i, over = 0;
        for (i = 0; i < e && !over; i++) over = __builtin_mul_overflow(t, mid, &t);
        if (over || t > c) hi = mid - 1; else lo = mid;
    }
    return lo;
}

/* ---- elliptic curves y^2 = x^3 + ax + b (mod p) --------------------------- */

static value point(i64 x, i64 y)
{
    value v[2];
    v[0] = v_int(x);
    v[1] = v_int(y);
    return v_tuple(2, v);
}

static int get_point(const gram_node *at, value v, i64 *x, i64 *y)
{
    if (v.t == V_NIL) return 0;
    if (v.t != V_TUPLE && v.t != V_LIST) fail(at, "expected a point (x, y), got %s", vtype_name(v.t));
    if (v.n != 2 || v.a[0].t != V_INT || v.a[1].t != V_INT) fail(at, "a point is (x, y)");
    *x = v.a[0].i;
    *y = v.a[1].i;
    return 1;
}

static value ec_add_v(const gram_node *at, value P, value Q, i64 a, i64 p)
{
    i64 x1, y1, x2, y2, l, x3;
    if (!get_point(at, P, &x1, &y1)) return Q;
    if (!get_point(at, Q, &x2, &y2)) return P;
    x1 = mod_norm(x1, p); y1 = mod_norm(y1, p); x2 = mod_norm(x2, p); y2 = mod_norm(y2, p);
    if (x1 == x2 && mod_norm(y1 + y2, p) == 0) return v_nil();
    if (x1 == x2)
        l = mulmod(mod_norm(3 * mulmod(x1, x1, p) + a, p), inv_mod(at, 2 * y1, p), p);
    else
        l = mulmod(y2 - y1, inv_mod(at, x2 - x1, p), p);
    x3 = mod_norm(mulmod(l, l, p) - x1 - x2, p);
    return point(x3, mod_norm(mulmod(l, x1 - x3, p) - y1, p));
}

/* ---- GF(2) polynomials: bit k is the coefficient of x^k ------------------ */

int poly_deg(i64 f)
{
    int d = -1;
    while (f) { f = (i64)((unsigned long long)f >> 1); d++; }
    return d;
}

static i64 poly_mul(const gram_node *at, i64 a, i64 b)
{
    i64 r = 0;
    int i;
    if (a && b && poly_deg(a) + poly_deg(b) > 62) fail(at, "polynomial degree too large");
    for (i = 0; i <= 62; i++)
        if ((b >> i) & 1) r ^= a << i;
    return r;
}

static i64 poly_mod(i64 a, i64 m)
{
    int dm = poly_deg(m);
    while (a && poly_deg(a) >= dm) a ^= m << (poly_deg(a) - dm);
    return a;
}

static i64 poly_div(i64 a, i64 m)
{
    int dm = poly_deg(m);
    i64 q = 0;
    while (a && poly_deg(a) >= dm) {
        int s = poly_deg(a) - dm;
        q |= (i64)1 << s;
        a ^= m << s;
    }
    return q;
}

const char *poly_text(i64 f)
{
    char *s = "";
    int k;
    if (!f) return "0";
    for (k = poly_deg(f); k >= 0; k--) {
        const char *t;
        if (!((f >> k) & 1)) continue;
        t = k == 0 ? "1" : k == 1 ? "x" : fmt("x^%d", k);
        s = fmt("%s%s%s", s, *s ? " + " : "", t);
    }
    return s;
}

const char *poly_factor_text(i64 f, int *irreducible)
{
    char *s = "";
    i64 d;
    int nf = 0;
    if (poly_deg(f) < 1) { *irreducible = 0; return poly_text(f); }
    for (d = 2; poly_deg(d) <= poly_deg(f) / 2 && f != 1; d++) {
        int e = 0;
        while (poly_deg(f) >= poly_deg(d) && poly_mod(f, d) == 0) { f = poly_div(f, d); e++; }
        if (!e) continue;
        nf += e;
        s = fmt("%s(%s)%s", s, poly_text(d), e > 1 ? fmt("^%d", e) : "");
    }
    if (f != 1) { nf++; s = fmt("%s(%s)", s, poly_text(f)); }
    *irreducible = nf == 1;
    return s;
}

/* ---- variables and functions --------------------------------------------- */

static struct { const char *name; size_t len; value v; } vars[MAX_VARS];
static int nvars, nglobals, frame, call_depth;

static struct { const char *name; size_t len; gram_node *params, *body; } funcs[MAX_FUNCS];
static int nfuncs;

static int same(const gram_node *id, const char *name, size_t len)
{
    return id->len == len && memcmp(id->text, name, len) == 0;
}

static value *find_var(const gram_node *id)
{
    int i;
    for (i = nvars - 1; i >= frame; i--)
        if (same(id, vars[i].name, vars[i].len)) return &vars[i].v;
    if (call_depth)
        for (i = nglobals - 1; i >= 0; i--)
            if (same(id, vars[i].name, vars[i].len)) return &vars[i].v;
    return NULL;
}

static void set_var(const gram_node *id, value v)
{
    int i;
    for (i = nvars - 1; i >= frame; i--)
        if (same(id, vars[i].name, vars[i].len)) { vars[i].v = v; return; }
    if (nvars == MAX_VARS) fail(id, "too many variables");
    vars[nvars].name = id->text;
    vars[nvars].len = id->len;
    vars[nvars].v = v;
    nvars++;
    if (!call_depth) nglobals = nvars;
}

static void define(gram_node *fn)
{
    gram_node *id = fn->child;
    int i;
    for (i = 0; i < nfuncs; i++)
        if (same(id, funcs[i].name, funcs[i].len)) break;
    if (i == MAX_FUNCS) fail(fn, "too many functions");
    if (i == nfuncs) nfuncs++;
    funcs[i].name = id->text;
    funcs[i].len = id->len;
    funcs[i].params = id->next;
    funcs[i].body = id->next->next;
}

/* ---- evaluation ------------------------------------------------------------
 * eval(n, m): m is the modulus in force (0 = ordinary integers). `a mod n`
 * evaluates `a` with m = n, so every + - * ^ / inside it is reduced. */

static value eval(gram_node *n, i64 m);
static int exec(gram_node *n);
static value retval;

static i64 want_int(const gram_node *at, value v)
{
    if (v.t != V_INT) fail(at, "expected a number, got %s", vtype_name(v.t));
    return v.i;
}

static i64 eval_int(gram_node *n, i64 m) { return want_int(n, eval(n, m)); }

static i64 R(i64 x, i64 m) { return m ? mod_norm(x, m) : x; }

static value number(const gram_node *n)
{
    i64 v = 0;
    size_t i = 0;
    int base = 10;
    if (n->len > 2 && n->text[0] == '0' && (n->text[1] == 'x' || n->text[1] == 'b')) {
        base = n->text[1] == 'x' ? 16 : 2;
        i = 2;
    }
    for (; i < n->len; i++) {
        int c = n->text[i], d = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
        if (__builtin_mul_overflow(v, base, &v) || __builtin_add_overflow(v, d, &v))
            fail(n, "number too large (over 63 bits)");
    }
    return v_int(v);
}

static value string(const gram_node *n)
{
    char *p = (char *)heap_alloc(n->len);
    size_t i, k = 0;
    for (i = 1; i + 1 < n->len; i++) {
        char c = n->text[i];
        if (c == '\\' && i + 2 < n->len) {
            c = n->text[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        p[k++] = c;
    }
    return v_str(p, k);
}

static value polylit(const gram_node *n)
{
    i64 f = 0;
    gram_node *t;
    for (t = n->child; t; t = t->next) {
        gram_node *c = t->child;
        i64 e;
        if (c->rule == CRIB_R_number) { f ^= number(c).i & 1; continue; }
        e = c->next ? number(c->next).i : 1;
        if (e > 62) fail(t, "polynomial degree too large (max 62)");
        f ^= (i64)1 << e;
    }
    return v_poly(f);
}

static value index_into(const gram_node *at, value v, i64 i)
{
    size_t n = v.n;
    if (v.t != V_LIST && v.t != V_TUPLE && v.t != V_STR) fail(at, "cannot index %s", vtype_name(v.t));
    if (i < 0) i += (i64)n;
    if (i < 0 || (size_t)i >= n) fail(at, "index %s out of range (length %s)", N(i), N((i64)n));
    if (v.t == V_STR) return v_int((unsigned char)v.s[i]);
    return v.a[i];
}

static value builtin(gram_node *call, gram_node *id, int argc, value *av);

static value call_fn(gram_node *call)
{
    gram_node *id = call->child, *a, *p;
    value av[MAX_ARGS];
    int argc = 0, i, f, saved_n, saved_frame;

    for (a = id->next; a; a = a->next) {
        if (argc == MAX_ARGS) fail(a, "too many arguments");
        av[argc++] = eval(a, 0);
    }
    for (f = 0; f < nfuncs; f++)
        if (same(id, funcs[f].name, funcs[f].len)) break;
    if (f == nfuncs) return builtin(call, id, argc, av);

    p = funcs[f].params;
    if ((int)p->nchildren != argc)
        fail(call, "%.*s takes %d argument%s, got %d", NAME(id), (int)p->nchildren,
             p->nchildren == 1 ? "" : "s", argc);
    if (call_depth == MAX_CALLS) fail(call, "too much recursion");
    saved_n = nvars;
    saved_frame = frame;
    frame = nvars;
    call_depth++;
    for (i = 0, a = p->child; a; a = a->next, i++) {
        vars[nvars].name = a->text;
        vars[nvars].len = a->len;
        vars[nvars].v = av[i];
        nvars++;
    }
    retval = v_nil();
    if (funcs[f].body->rule == CRIB_R_block) exec(funcs[f].body);
    else retval = eval(funcs[f].body, 0);
    call_depth--;
    nvars = saved_n;
    frame = saved_frame;
    return retval;
}

static value arith(gram_node *op, value a, value b, i64 m)
{
    char c = op->text[0];
    if (a.t == V_POLY || b.t == V_POLY) {
        if (a.t != b.t) fail(op, "cannot mix a polynomial with %s", vtype_name(a.t == V_POLY ? b.t : a.t));
        if (c == '+' || c == '-') return v_poly(a.i ^ b.i);
        if (c == '*') return v_poly(poly_mul(op, a.i, b.i));
        if (b.i == 0) fail(op, "division by the zero polynomial");
        return v_poly(c == '/' ? poly_div(a.i, b.i) : poly_mod(a.i, b.i));
    }
    if (c == '+' && (a.t == V_STR || b.t == V_STR)) {
        const char *s = fmt("%s%s", show_value(a), show_value(b));
        return v_cstr(s);
    }
    if (c == '+' && a.t == V_LIST && b.t == V_LIST) {
        value *v = (value *)heap_alloc((a.n + b.n) * sizeof(value) + 1);
        memcpy(v, a.a, a.n * sizeof(value));
        memcpy(v + a.n, b.a, b.n * sizeof(value));
        return v_list(a.n + b.n, v);
    }
    {
        i64 x = want_int(op, a), y = want_int(op, b);
        if (m) {
            x = mod_norm(x, m);
            y = mod_norm(y, m);
            switch (c) {
            case '+': return v_int(mod_norm(x - m + y, m));
            case '-': return v_int(mod_norm(x - y, m));
            case '*': return v_int(mulmod(x, y, m));
            case '/': return v_int(mulmod(x, inv_mod(op, y, m), m));
            default:
                if (!y) fail(op, "division by zero");
                return v_int(mod_norm(x % y, m));
            }
        }
        switch (c) {
        case '+': return v_int(add_ck(op, x, y));
        case '-':
            if (y == (i64)(-9223372036854775807LL - 1)) fail(op, "number too large");
            return v_int(add_ck(op, x, -y));
        case '*': return v_int(mul_ck(op, x, y));
        default:
            if (!y) fail(op, "division by zero");
            if (c == '/') { i64 q = x / y; if ((x % y != 0) && ((x < 0) != (y < 0))) q--; return v_int(q); }
            return v_int(mod_norm(x, y < 0 ? -y : y));
        }
    }
}

static value eval(gram_node *n, i64 m)
{
    gram_node *c;
    value a, b;
    i64 x, e;

    switch (n->rule) {
    case CRIB_R_number: return v_int(R(number(n).i, m));
    case CRIB_R_string: return string(n);
    case CRIB_R_poly: return polylit(n);
    case CRIB_R_ident: {
        value *v = find_var(n);
        if (!v) {
            if (same(n, "true", 4)) return v_int(1);
            if (same(n, "false", 5)) return v_int(0);
            if (same(n, "O", 1) || same(n, "nil", 3)) return v_nil();
            fail(n, "undefined variable '%.*s'", NAME(n));
        }
        return v->t == V_INT ? v_int(R(v->i, m)) : *v;
    }
    case CRIB_R_paren: {
        value items[64];
        size_t k = 0;
        if (n->nchildren == 1) return eval(n->child, m);
        for (c = n->child; c; c = c->next) {
            if (k == 64) fail(c, "tuple too long");
            items[k++] = eval(c, 0);
        }
        return v_tuple(k, items);
    }
    case CRIB_R_list: {
        value *items = (value *)heap_alloc(n->nchildren * sizeof(value) + 1);
        size_t k = 0;
        for (c = n->child; c; c = c->next) items[k++] = eval(c, 0);
        return v_list(k, items);
    }
    case CRIB_R_call:
        a = call_fn(n);
        return a.t == V_INT ? v_int(R(a.i, m)) : a;
    case CRIB_R_postfix:
        a = eval(n->child, 0);
        for (c = n->child->next; c; c = c->next) a = index_into(c, a, eval_int(c->child, 0));
        return a.t == V_INT ? v_int(R(a.i, m)) : a;
    case CRIB_R_pow:
        a = eval(n->child, m);
        e = eval_int(n->child->next, 0); /* exponents are never reduced mod m */
        if (a.t == V_POLY) {
            i64 r = 1;
            if (e < 0) fail(n, "negative power of a polynomial");
            while (e-- > 0) r = poly_mul(n, r, a.i);
            return v_poly(r);
        }
        x = want_int(n, a);
        if (m) {
            if (e < 0) { x = inv_mod(n, x, m); e = -e; }
            return v_int(powmod(x, e, m));
        }
        if (e < 0) fail(n, "negative power needs a modulus, e.g. a^-1 mod n");
        {
            i64 r = 1;
            while (e-- > 0) r = mul_ck(n, r, x);
            return v_int(r);
        }
    case CRIB_R_neg:
        a = eval(n->child, m);
        if (a.t == V_POLY) return a;
        x = want_int(n, a);
        return v_int(m ? mod_norm(-x, m) : mul_ck(n, x, -1));
    case CRIB_R_prod:
    case CRIB_R_sum:
        a = eval(n->child, m);
        for (c = n->child->next; c; c = c->next->next) {
            b = eval(c->next, m);
            a = arith(c, a, b, m);
        }
        return a;
    case CRIB_R_xorx:
        x = eval_int(n->child, 0);
        for (c = n->child->next; c; c = c->next) x ^= eval_int(c, 0);
        return v_int(R(x, m));
    case CRIB_R_modx:
        b = eval(n->child->next, 0);
        if (b.t == V_POLY) {
            a = eval(n->child, 0);
            if (a.t != V_POLY) fail(n, "polynomial mod needs a polynomial on the left");
            if (!b.i) fail(n, "mod the zero polynomial");
            return v_poly(poly_mod(a.i, b.i));
        }
        x = want_int(n->child->next, b);
        if (x <= 0) fail(n->child->next, "modulus must be positive, got %s", N(x));
        a = eval(n->child, x);
        if (a.t == V_TUPLE || a.t == V_LIST) { /* reduce a point/vector */
            size_t i;
            value *it = (value *)heap_alloc(a.n * sizeof(value) + 1);
            for (i = 0; i < a.n; i++) it[i] = v_int(mod_norm(want_int(n, a.a[i]), x));
            return a.t == V_TUPLE ? v_tuple(a.n, it) : v_list(a.n, it);
        }
        return v_int(mod_norm(want_int(n, a), x));
    case CRIB_R_cmp: {
        gram_node *op = n->child->next;
        a = eval(n->child, 0);
        b = eval(op->next, 0);
        if (op->text[0] == '=' ) return v_int(equal(a, b));
        if (op->text[0] == '!') return v_int(!equal(a, b));
        x = want_int(n->child, a);
        e = want_int(op->next, b);
        if (op->len == 2) return v_int(op->text[0] == '<' ? x <= e : x >= e);
        return v_int(op->text[0] == '<' ? x < e : x > e);
    }
    case CRIB_R_not: return v_int(!truthy(eval(n->child, 0)));
    case CRIB_R_and:
        for (c = n->child; c; c = c->next) if (!truthy(eval(c, 0))) return v_int(0);
        return v_int(1);
    case CRIB_R_or:
        for (c = n->child; c; c = c->next) if (truthy(eval(c, 0))) return v_int(1);
        return v_int(0);
    default:
        fail(n, "cannot evaluate '%s'", n->tag);
        return v_nil();
    }
}

/* ---- built-in functions ------------------------------------------------------ */

static void argn(gram_node *call, gram_node *id, int argc, int lo, int hi)
{
    if (argc < lo || argc > hi)
        fail(call, "%.*s takes %d%s argument%s", NAME(id), lo,
             hi > lo ? fmt(" to %d", hi) : "", hi == 1 ? "" : "s");
}

static value builtin(gram_node *call, gram_node *id, int argc, value *av)
{
#define IS(s) same(id, s, sizeof s - 1)
#define I(k) want_int(call, av[k])
    if (IS("gcd")) { argn(call, id, argc, 2, 2); return v_int(gcd_ll(I(0), I(1))); }
    if (IS("egcd")) {
        value r[3];
        i64 x, y, g;
        argn(call, id, argc, 2, 2);
        g = egcd_ll(I(0), I(1), &x, &y);
        r[0] = v_int(g); r[1] = v_int(x); r[2] = v_int(y);
        return v_tuple(3, r);
    }
    if (IS("inv")) { argn(call, id, argc, 2, 2); return v_int(inv_mod(call, I(0), I(1))); }
    if (IS("modpow")) {
        argn(call, id, argc, 3, 3);
        if (I(2) <= 0) fail(call, "modulus must be positive");
        if (I(1) < 0) return v_int(powmod(inv_mod(call, I(0), I(2)), -I(1), I(2)));
        return v_int(powmod(I(0), I(1), I(2)));
    }
    if (IS("phi")) { argn(call, id, argc, 1, 1); if (I(0) < 1) fail(call, "phi needs n >= 1"); return v_int(phi_ll(I(0))); }
    if (IS("isprime")) { argn(call, id, argc, 1, 1); return v_int(is_prime(I(0))); }
    if (IS("factor")) {
        i64 p[64];
        int e[64], k, i, j, cnt = 0;
        value items[64];
        argn(call, id, argc, 1, 1);
        if (I(0) < 2) return v_list(0, items);
        k = factorize(I(0), p, e);
        for (i = 0; i < k; i++) for (j = 0; j < e[i]; j++) items[cnt++] = v_int(p[i]);
        return v_list((size_t)cnt, items);
    }
    if (IS("ord")) { /* multiplicative order of a mod n */
        i64 a, n, k = 1, x;
        argn(call, id, argc, 2, 2);
        a = I(0); n = I(1);
        if (gcd_ll(a, n) != 1) fail(call, "ord needs gcd(a, n) = 1");
        for (x = mod_norm(a, n); x != 1 % n; x = mulmod(x, a, n)) k++;
        return v_int(k);
    }
    if (IS("root")) { argn(call, id, argc, 2, 2); if (I(0) < 0 || I(1) < 1) fail(call, "root(c, e) needs c >= 0, e >= 1"); return v_int(iroot(I(0), (int)I(1))); }
    if (IS("crt")) { /* crt([r1, r2, ...], [m1, m2, ...]) */
        i64 r = 0, M = 1;
        size_t i;
        argn(call, id, argc, 2, 2);
        if (av[0].t != V_LIST || av[1].t != V_LIST || av[0].n != av[1].n) fail(call, "crt([remainders], [moduli])");
        for (i = 0; i < av[0].n; i++) {
            i64 ri = want_int(call, av[0].a[i]), mi = want_int(call, av[1].a[i]), t;
            if (gcd_ll(M, mi) != 1) fail(call, "crt needs pairwise coprime moduli");
            t = mulmod(mod_norm(ri - r, mi), inv_mod(call, M % mi, mi), mi);
            r = r + mul_ck(call, M, t);
            M = mul_ck(call, M, mi);
            r = mod_norm(r, M);
        }
        return v_int(r);
    }
    if (IS("bin") || IS("hex")) {
        i64 x, w;
        char buf[80];
        int k = 0, i, base = IS("bin") ? 2 : 16;
        argn(call, id, argc, 1, 2);
        x = I(0);
        w = argc > 1 ? I(1) : 1;
        if (x < 0) fail(call, "%.*s needs a non-negative number", NAME(id));
        do { buf[k++] = "0123456789abcdef"[x % base]; x /= base; } while (x && k < 70);
        while (k < w && k < 70) buf[k++] = '0';
        for (i = 0; i < k / 2; i++) { char t = buf[i]; buf[i] = buf[k - 1 - i]; buf[k - 1 - i] = t; }
        buf[k] = 0;
        return v_cstr(fmt("%s", buf));
    }
    if (IS("len")) {
        argn(call, id, argc, 1, 1);
        if (av[0].t != V_STR && av[0].t != V_LIST && av[0].t != V_TUPLE) fail(call, "len of %s", vtype_name(av[0].t));
        return v_int((i64)av[0].n);
    }
    if (IS("str")) { argn(call, id, argc, 1, 1); return v_cstr(show_value(av[0])); }
    if (IS("abs")) { argn(call, id, argc, 1, 1); return v_int(I(0) < 0 ? mul_ck(call, I(0), -1) : I(0)); }
    if (IS("min") || IS("max")) {
        int k, best = 0;
        argn(call, id, argc, 1, MAX_ARGS);
        for (k = 1; k < argc; k++)
            if (IS("min") ? I(k) < I(best) : I(k) > I(best)) best = k;
        return av[best];
    }
    if (IS("deg")) { argn(call, id, argc, 1, 1); if (av[0].t != V_POLY) fail(call, "deg needs a polynomial"); return v_int(poly_deg(av[0].i)); }
    if (IS("ecadd")) { argn(call, id, argc, 4, 4); return ec_add_v(call, av[0], av[1], I(2), I(3)); }
    if (IS("ecmul")) { /* ecmul(k, P, a, p) */
        value r = v_nil(), P = av[1];
        i64 k;
        argn(call, id, argc, 4, 4);
        k = I(0);
        if (k < 0) fail(call, "ecmul needs k >= 0");
        while (k) {
            if (k & 1) r = ec_add_v(call, r, P, I(2), I(3));
            P = ec_add_v(call, P, P, I(2), I(3));
            k >>= 1;
        }
        return r;
    }
    if (IS("lfsr")) { /* lfsr(poly, seed, n): the first n output bits */
        i64 f, s, cnt, k;
        int L, j;
        value *bits;
        argn(call, id, argc, 3, 3);
        if (av[0].t != V_POLY) fail(call, "lfsr needs a polynomial, e.g. poly(x^4 + x + 1)");
        f = av[0].i; s = I(1); cnt = I(2);
        L = poly_deg(f);
        if (L < 1 || L > 62 || cnt < 0 || cnt > 100000) fail(call, "lfsr: bad arguments");
        bits = (value *)heap_alloc((size_t)cnt * sizeof(value) + 1);
        for (k = 0; k < cnt; k++) {
            i64 nb = 0;
            for (j = 0; j < L; j++) if ((f >> j) & 1) nb ^= (s >> (L - 1 - j)) & 1;
            bits[k] = v_int((s >> (L - 1)) & 1);
            s = ((s << 1) & (((i64)1 << L) - 1)) | nb;
        }
        return v_list((size_t)cnt, bits);
    }
    fail(id, "unknown function '%.*s'", NAME(id));
    return v_nil();
#undef IS
#undef I
}

/* ---- statements -------------------------------------------------------------- */

static const char *src_of(const gram_node *n)
{
    size_t len = n->len;
    while (len && (n->text[len - 1] == ' ' || n->text[len - 1] == '\n' ||
                   n->text[len - 1] == '\r' || n->text[len - 1] == '\t' || n->text[len - 1] == ';'))
        len--;
    return fmt("%.*s", (int)len, n->text);
}

static void stmt_context(const gram_node *n)
{
    const char *code = src_of(n);
    out_set_stmt(code, strlen(code), line_of(n));
}

static void do_check(gram_node *n)
{
    gram_node *e = n->child;
    value v = eval(e, 0);
    const char *code = src_of(e);
    if (truthy(v)) { out_line(fmt("[ok]   %s", code)); return; }
    if (e->rule == CRIB_R_cmp) {
        value a = eval(e->child, 0), b = eval(e->child->next->next, 0);
        out_line(fmt("[FAIL] %s   (left side = %s, right side = %s)", code, show_value(a), show_value(b)));
    } else {
        out_line(fmt("[FAIL] %s", code));
    }
}

static int exec(gram_node *n)
{
    gram_node *c;
    value v;
    switch (n->rule) {
    case CRIB_R_fn: define(n); return 0;
    case CRIB_R_assign: {
        gram_node *last = n->child;
        int k = 0, i;
        while (last->next) { last = last->next; k++; }
        v = eval(last, 0);
        if (k == 1) { set_var(n->child, v); return 0; }
        if ((v.t != V_TUPLE && v.t != V_LIST) || (int)v.n != k)
            fail(n, "cannot unpack %s into %d names", show_value(v), k);
        for (i = 0, c = n->child; c != last; c = c->next, i++) set_var(c, v.a[i]);
        return 0;
    }
    case CRIB_R_if:
        if (truthy(eval(n->child, 0))) return exec(n->child->next);
        if (n->child->next->next) return exec(n->child->next->next);
        return 0;
    case CRIB_R_while: {
        long guard = 0;
        while (truthy(eval(n->child, 0))) {
            if (exec(n->child->next)) return 1;
            if (++guard > 100000000L) fail(n, "loop ran 100 million times; stopping");
        }
        return 0;
    }
    case CRIB_R_for: {
        gram_node *id = n->child, *body = id->next->next->next;
        i64 i = eval_int(id->next, 0), hi = eval_int(id->next->next, 0);
        for (; i <= hi; i++) {
            set_var(id, v_int(i));
            if (exec(body)) return 1;
        }
        return 0;
    }
    case CRIB_R_return:
        if (!call_depth) fail(n, "return outside a function");
        retval = n->child ? eval(n->child, 0) : v_nil();
        return 1;
    case CRIB_R_block:
        for (c = n->child; c; c = c->next) if (exec(c)) return 1;
        return 0;
    case CRIB_R_show:
        stmt_context(n);
        for (c = n->child; c; c = c->next) {
            const char *code = src_of(c), *val;
            v = eval(c, 0);
            val = show_value(v);
            if (code[0] == '"' || strcmp(code, val) == 0) out_line(val);
            else out_line(fmt("%s = %s", code, val));
        }
        return 0;
    case CRIB_R_print: {
        const char *s = "";
        stmt_context(n);
        for (c = n->child; c; c = c->next) s = fmt("%s%s%s", s, c == n->child ? "" : " ", show_value(eval(c, 0)));
        out_line(s);
        return 0;
    }
    case CRIB_R_check: stmt_context(n); do_check(n); return 0;
    case CRIB_R_sim: {
        simargs a;
        gram_node *id = n->child;
        stmt_context(n);
        a.at = n;
        a.n = 0;
        for (c = id->next; c; c = c->next) {
            if (a.n == 16) fail(c, "too many arguments");
            if (c->rule == CRIB_R_named) {
                a.name[a.n] = fmt("%.*s", NAME(c->child));
                a.v[a.n] = eval(c->child->next, 0);
            } else {
                a.name[a.n] = NULL;
                a.v[a.n] = eval(c, 0);
            }
            a.n++;
        }
        run_sim(n, id->text, id->len, &a);
        return 0;
    }
    case CRIB_R_exprstmt:
        v = eval(n->child, 0);
        if (!call_depth && v.t != V_NIL) {
            stmt_context(n);
            out_line(fmt("%s = %s", src_of(n->child), show_value(v)));
        }
        return 0;
    default:
        fail(n, "cannot run '%s'", n->tag);
        return 0;
    }
}

/* ---- main ---------------------------------------------------------------------- */

static void put(int c, void *ctx) { fputc(c, (FILE *)ctx); }

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t cap = 0, n = 0, got;
    if (!f) return NULL;
    do {
        cap = cap ? cap * 2 : 4096;
        buf = (char *)realloc(buf, cap + 1);
        if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
        got = fread(buf + n, 1, cap - n, f);
        n += got;
    } while (got > 0);
    fclose(f);
    buf[n] = 0;
    *len = n;
    return buf;
}

int main(int argc, char **argv)
{
    static gram_arena arena;
    gram_error err;
    gram_grammar *g;
    gram_node *prog, *c;
    unsigned char *mem = (unsigned char *)malloc(TREE_MEM);
    const char *html = NULL;
    int tree = 0, i;
    static int ok = 1;
    char *text;
    size_t len;

    if (argc == 2 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))) {
        printf("crib %s (gram %s)\n", CRIB_VERSION, GRAM_VERSION);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("crib %s - a language for the maths of a cryptography course\n\n"
               "usage: crib [-t] [--html out.html] file.crib\n\n"
               "  file.crib         run a program and print the results\n"
               "  --html out.html   also write an interactive page (LFSRs, curves, ...)\n"
               "  -t                print the syntax tree instead of running\n"
               "  --version         print the version\n", CRIB_VERSION);
        return 0;
    }

    heap = (char *)malloc(HEAP_MEM);
    if (!mem || !heap) { fprintf(stderr, "out of memory\n"); return 1; }

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-t")) tree = 1;
        else if (!strcmp(argv[i], "--html") && i + 1 < argc) html = argv[++i];
        else break;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: crib [-t] [--html out.html] file.crib   (crib --help)\n");
        return 2;
    }
    src_name = argv[i];
    text = read_file(src_name, &len);
    if (!text) { perror(src_name); return 1; }
    src_text = text;
    src_len = len;

    gram_arena_init(&arena, mem, TREE_MEM);
    g = gram_compile(&arena, CRIB_GRAMMAR, &err);
    if (!g) { fputs("grammar:", stderr); gram_error_print(&err, put, stderr); return 1; }
    prog = gram_parse(g, NULL, text, len, &arena, &err);
    if (!prog) { fprintf(stderr, "%s:", src_name); gram_error_print(&err, put, stderr); return 1; }
    if (tree) { gram_dump(prog, put, stdout); return 0; }

    out_set_source(src_name, text, len);
    if (html) out_html_begin();

    if (setjmp(trap)) {
        ok = 0;
    } else {
        for (c = prog->child; c; c = c->next) /* functions can be used before they appear */
            if (c->rule == CRIB_R_fn) define(c);
        for (c = prog->child; c; c = c->next) exec(c);
    }
    fflush(stdout);
    if (html) {
        if (!out_html_write(html)) { perror(html); return 1; }
        fprintf(stderr, "wrote %s\n", html);
    }
    return ok ? 0 : 1;
}
