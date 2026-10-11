/* crib_sim.c - the `sim name(...)` statements of crib. Each simulation
 * runs one textbook procedure step by step, printing every intermediate
 * value, and records data for the animated HTML view.
 *
 *   sim lfsr(poly(x^4 + x + 1), seed = 0b0001)    state diagram, cycles
 *   sim modpow(4, 100003, 33)                     square-and-multiply
 *   sim egcd(7, 192)   sim inverse(7, 192)        extended Euclid
 *   sim phi(221)                                  Euler's totient
 *   sim rsa(p = 13, q = 17, e = 7, m = 36)        keys, encrypt, decrypt
 *   sim dh(p = 23, g = 5, x = 6, y = 15)          Diffie-Hellman
 *   sim elgamal(p = 17, alpha = 3, a = 6, r = 7, t = 6)
 *   sim elgamal_sign(p = 19, alpha = 2, a = 5, k = 7, m = 10)
 *   sim dsa(p = 23, q = 11, h = 2, x = 3, k = 7, H = 5)
 *   sim ecc(a = 0, b = -2, p = 7, P = (3, 2), Q = (5, 5))
 *   sim modes(text = "ATTACK AT DAWN", flip = (2, 3))
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crib.h"

/* ---- arguments ---------------------------------------------------------- */

static const gram_node *AT;

/* named argument `name`, or else the pos-th positional one */
static value *arg(simargs *a, const char *name, int pos)
{
    int i, k = 0;
    for (i = 0; i < a->n; i++)
        if (a->name[i] && !strcmp(a->name[i], name)) return &a->v[i];
    for (i = 0; i < a->n; i++)
        if (!a->name[i] && k++ == pos) return &a->v[i];
    return NULL;
}

static int has(simargs *a, const char *name, int pos) { return arg(a, name, pos) != NULL; }

static i64 get(simargs *a, const char *name, int pos)
{
    value *v = arg(a, name, pos);
    if (!v) fail(AT, "missing argument '%s'", name);
    if (v->t != V_INT) fail(AT, "argument '%s' should be a number, got %s", name, vtype_name(v->t));
    return v->i;
}

static i64 get_or(simargs *a, const char *name, int pos, i64 def)
{
    return has(a, name, pos) ? get(a, name, pos) : def;
}

static void need_mod(const char *what, i64 m)
{
    if (m < 2) fail(AT, "%s must be at least 2, got %s", what, N(m));
}

static const char *bits(i64 x, int w)
{
    char *s = (char *)heap_alloc((size_t)w + 1);
    int i;
    for (i = 0; i < w; i++) s[i] = (char)('0' + ((x >> (w - 1 - i)) & 1));
    s[w] = 0;
    return s;
}

static const char *hex4(unsigned x) { return fmt("%04x", x & 0xffff); }

/* a list of numbers as "6, 6, 3, 1" */
static const char *join(const i64 *v, int n)
{
    char *s = "";
    int i;
    for (i = 0; i < n; i++) s = fmt("%s%s%s", s, i ? ", " : "", N(v[i]));
    return s;
}

static const char *factor_text(i64 n)
{
    i64 p[64];
    int e[64], k = factorize(n, p, e), i;
    char *s = "";
    for (i = 0; i < k; i++)
        s = fmt("%s%s%s%s", s, i ? " * " : "", N(p[i]), e[i] > 1 ? fmt("^%d", e[i]) : "");
    return s;
}

/* a^e mod n, with the square-and-multiply count in words */
static const char *pow_text(i64 a, i64 e, i64 n)
{
    return fmt("%s^%s mod %s = %s", N(a), N(e), N(n), N(powmod(a, e, n)));
}

/* ---- LFSR ------------------------------------------------------------------
 * f(x) = x^L + c(L-1) x^(L-1) + ... + c1 x + c0 gives
 *     s[n+L] = c(L-1) s[n+L-1] + ... + c1 s[n+1] + c0 s[n]   (mod 2).
 * A state is written s[n] s[n+1] ... s[n+L-1]; one step shifts left, s[n]
 * leaves as the output bit and the new bit enters on the right. In the
 * integer form of a state, s[n] is the most significant bit. */

static i64 lfsr_next(i64 f, int L, i64 s)
{
    i64 nb = 0;
    int j;
    for (j = 0; j < L; j++)
        if ((f >> j) & 1) nb ^= (s >> (L - 1 - j)) & 1;
    return ((s << 1) & (((i64)1 << L) - 1)) | nb;
}

static const char *sname(int i) { return i == 0 ? "s[n]" : fmt("s[n+%d]", i); }

static void sim_lfsr(simargs *a)
{
    value *fv = arg(a, "f", 0);
    i64 f, size, i, seed, steps, *cyc_start, *cyc_len, *order;
    int L, ncyc = 0, irr, j, primitive, ontail;
    int *next, *mark;
    const char *rec = "", *taps = "";

    if (!fv) fail(AT, "lfsr needs a polynomial: sim lfsr(poly(x^4 + x + 1))");
    if (fv->t != V_POLY && fv->t != V_INT) fail(AT, "lfsr needs a polynomial, got %s", vtype_name(fv->t));
    f = fv->i;
    L = poly_deg(f);
    if (L < 1 || L > 16) fail(AT, "lfsr: degree must be 1..16 (got %d)", L);
    size = (i64)1 << L;

    sim_begin("lfsr", fmt("LFSR   f(x) = %s", poly_text(f)));
    for (j = L - 1; j >= 0; j--)
        if ((f >> j) & 1) {
            rec = fmt("%s%s%s", rec, *rec ? " + " : "", sname(j));
            taps = fmt("%s%s%s", taps, *taps ? ", " : "", sname(j));
        }
    sim_step("recurrence", fmt("s[n+%d] = %s   (mod 2: + is XOR)", L, *rec ? rec : "0"));
    sim_step("register", fmt("%d cells s[n] .. s[n+%d]; XOR the tapped cells %s into the new bit", L, L - 1,
                             *taps ? taps : "(none)"));
    {
        const char *ft = poly_factor_text(f, &irr);
        sim_step("factors", irr ? fmt("%s is irreducible over GF(2)", poly_text(f))
                                : fmt("%s = %s", poly_text(f), ft));
    }

    /* the whole state graph: every state has exactly one successor */
    next = (int *)heap_alloc((size_t)size * sizeof(int));
    mark = (int *)heap_alloc((size_t)size * sizeof(int));
    cyc_start = (i64 *)heap_alloc((size_t)size * sizeof(i64));
    cyc_len = (i64 *)heap_alloc((size_t)size * sizeof(i64));
    for (i = 0; i < size; i++) { next[i] = (int)lfsr_next(f, L, i); mark[i] = 0; }
    for (i = 0; i < size; i++) {
        i64 s = i, len = 0, lo;
        if (mark[i]) continue;
        while (!mark[s]) { mark[s] = (int)i + 1; s = next[s]; }
        if (mark[s] != (int)i + 1) continue; /* ran into an older walk */
        lo = s;                              /* s is on a new cycle */
        {
            i64 t = s;
            do { if (t < lo) lo = t; t = next[t]; len++; } while (t != s);
        }
        cyc_start[ncyc] = lo;
        cyc_len[ncyc] = len;
        ncyc++;
    }
    /* longest cycles first, the all-zero cycle last (as drawn on paper) */
    order = (i64 *)heap_alloc((size_t)ncyc * sizeof(i64));
    for (j = 0; j < ncyc; j++) order[j] = j;
    for (j = 1; j < ncyc; j++) {
        int k = j;
        while (k > 0) {
            i64 x = order[k - 1], y = order[k];
            int swap = cyc_len[y] > cyc_len[x] || (cyc_len[y] == cyc_len[x] && cyc_start[y] && cyc_start[y] < cyc_start[x]) ||
                       (cyc_len[y] == cyc_len[x] && cyc_start[x] == 0);
            if (!swap) break;
            order[k - 1] = y; order[k] = x; k--;
        }
    }
    ontail = 0;
    {
        i64 on = 0;
        for (j = 0; j < ncyc; j++) on += cyc_len[j];
        ontail = (int)(size - on);
    }
    primitive = (f & 1) && ncyc == 2 && cyc_len[order[0]] == size - 1;

    if (L <= 6) {
        for (j = 0; j < ncyc; j++) {
            i64 c = order[j], s = cyc_start[c];
            const char *t = bits(s, L);
            do { s = next[s]; t = fmt("%s -> %s", t, bits(s, L)); } while (s != cyc_start[c]);
            sim_step(fmt("cycle %d", j + 1), fmt("length %s:  %s", N(cyc_len[c]), t));
        }
    } else {
        sim_note(fmt("%s states: too many to list; showing cycle lengths only", N(size)));
    }
    if (!(f & 1))
        sim_note(fmt("c0 = 0, so f is divisible by x: the step is not invertible and %d state%s lie%s on no "
                     "cycle (they run into one)", ontail, ontail == 1 ? "" : "s", ontail == 1 ? "s" : ""));
    {
        i64 lens[64], maxp = 0;
        int k = ncyc < 64 ? ncyc : 64;
        for (j = 0; j < k; j++) { lens[j] = cyc_len[order[j]]; if (lens[j] > maxp) maxp = lens[j]; }
        sim_result(fmt("cycle lengths %s%s  =>  %d cycle%s, max period %s", join(lens, k), ncyc > 64 ? ", ..." : "",
                       ncyc, ncyc == 1 ? "" : "s", N(maxp)));
    }
    if (primitive)
        sim_result(fmt("primitive: one cycle of 2^%d - 1 = %s states, plus {%s}", L, N(size - 1), bits(0, L)));
    else if (f & 1)
        sim_note(fmt("not primitive (a primitive f would give one cycle of 2^%d - 1 = %s)", L, N(size - 1)));

    /* a run from one seed */
    seed = get_or(a, "seed", 1, -1);
    steps = get_or(a, "steps", 2, -1);
    if (seed >= size) fail(AT, "seed %s does not fit in %d bits", N(seed), L);
    if (seed >= 0) {
        i64 s = seed, k;
        const char *out = "";
        const char *cols[4] = { "n", "state", "new bit", "out" };
        if (steps < 0) { /* until the state repeats */
            for (k = 0; k < size; k++) mark[k] = 0;
            for (steps = 0, k = seed; !mark[k]; k = next[k]) { mark[k] = 1; steps++; }
            if (steps > 64) steps = 64;
        }
        if (steps > 256) steps = 256;
        sim_note(fmt("run from seed %s (%s steps):", bits(seed, L), N(steps)));
        sim_table(4, cols);
        for (k = 0; k < steps; k++) {
            i64 nx = next[s];
            const char *row[4];
            row[0] = N(k); row[1] = bits(s, L); row[2] = N(nx & 1); row[3] = N((s >> (L - 1)) & 1);
            sim_row(row);
            out = fmt("%s%s", out, N((s >> (L - 1)) & 1));
            s = nx;
        }
        sim_table_end();
        sim_result(fmt("keystream: %s", out));
    }

    sim_data();
    j_key("L"); j_int(L);
    j_key("poly"); j_str(poly_text(f));
    j_key("rec"); j_str(fmt("s[n+%d] = %s", L, *rec ? rec : "0"));
    j_key("taps"); j_arr(); for (j = 0; j < L; j++) j_int((f >> j) & 1); j_arr_end();
    j_key("primitive"); j_int(primitive);
    j_key("cycles"); j_arr();
    if (L <= 8)
        for (j = 0; j < ncyc; j++) {
            i64 c = order[j], s = cyc_start[c];
            j_arr();
            do { j_int(s); s = next[s]; } while (s != cyc_start[c]);
            j_arr_end();
        }
    j_arr_end();
    j_key("lengths"); j_arr(); for (j = 0; j < ncyc && j < 256; j++) j_int(cyc_len[order[j]]); j_arr_end();
    j_key("tails"); j_int(ontail);
    {   /* states for the animation: the requested run, or a default from 0..01 */
        i64 s = seed >= 0 ? seed : 1, k, cnt = seed >= 0 ? steps : (L <= 8 ? size : 64);
        j_key("seed"); j_int(s);
        j_key("trace"); j_arr();
        for (k = 0; k <= cnt && k <= 256; k++) { j_int(s); s = next[s]; }
        j_arr_end();
    }
    sim_end();
}

/* ---- square-and-multiply ------------------------------------------------- */

static void sim_modpow(simargs *a)
{
    i64 b = get(a, "a", 0), e = get(a, "e", 1), n = get(a, "n", 2), r = 1, ph;
    int nb = 0, i, sq = 0, mu = 0;
    const char *cols[5] = { "bit", "", "square r^2", "times a", "r" };

    need_mod("n", n);
    sim_begin("modpow", fmt("Square-and-multiply   %s^%s mod %s", N(b), N(e), N(n)));
    if (e < 0) {
        i64 iv = inv_mod(AT, b, n);
        sim_step("inverse", fmt("%s^-1 mod %s = %s, so %s^%s = %s^%s (mod %s)", N(b), N(n), N(iv), N(b), N(e), N(iv), N(-e), N(n)));
        b = iv;
        e = -e;
    }
    b = mod_norm(b, n);
    if (gcd_ll(b, n) == 1) {
        ph = phi_ll(n);
        sim_step("phi(n)", fmt("phi(%s) = %s   (%s = %s)", N(n), N(ph), N(n), factor_text(n)));
        if (e >= ph) {
            sim_step("Euler", fmt("gcd(%s, %s) = 1, so %s^%s = 1 (mod %s)", N(b), N(n), N(b), N(ph), N(n)));
            sim_step("reduce", fmt("%s = %s * %s + %s   =>   %s^%s = (%s^%s)^%s * %s^%s = %s^%s (mod %s)",
                                   N(e), N(e / ph), N(ph), N(e % ph), N(b), N(e), N(b), N(ph), N(e / ph), N(b),
                                   N(e % ph), N(b), N(e % ph), N(n)));
            sim_step("shortcut", pow_text(b, e % ph, n));
        }
    }
    for (i = 62; i >= 0; i--) if ((e >> i) & 1) { nb = i + 1; break; }
    sim_step("exponent", fmt("%s = %s in binary (%d bits); read left to right: square, and multiply by %s on a 1",
                             N(e), nb ? bits(e, nb) : "0", nb, N(b)));
    sim_table(5, cols);
    for (i = nb - 1; i >= 0; i--) {
        const char *row[5];
        int bit = (int)((e >> i) & 1);
        i64 s = mulmod(r, r, n);
        row[0] = N(bit);
        row[1] = fmt("2^%d", i);
        row[2] = fmt("%s^2 = %s", N(r), N(s));
        sq++;
        r = s;
        if (bit) { r = mulmod(r, b, n); mu++; row[3] = fmt("%s * %s = %s", N(s), N(b), N(r)); }
        else row[3] = "";
        row[4] = N(r);
        sim_row(row);
    }
    sim_table_end();
    sim_result(fmt("%s^%s mod %s = %s   (%d squarings + %d multiplications instead of %s)",
                   N(b), N(e), N(n), N(r), sq, mu, N(e > 0 ? e - 1 : 0)));

    sim_data();
    j_key("a"); j_int(b); j_key("e"); j_int(e); j_key("n"); j_int(n);
    j_key("bits"); j_str(nb ? bits(e, nb) : "");
    j_key("steps"); j_arr();
    r = 1;
    for (i = nb - 1; i >= 0; i--) {
        int bit = (int)((e >> i) & 1);
        i64 s = mulmod(r, r, n);
        j_obj();
        j_key("bit"); j_int(bit);
        j_key("from"); j_int(r);
        j_key("sq"); j_int(s);
        r = bit ? mulmod(s, b, n) : s;
        j_key("r"); j_int(r);
        j_obj_end();
    }
    j_arr_end();
    sim_end();
}

/* ---- extended Euclid -------------------------------------------------------- */

/* prints the table, returns gcd and the coefficients: a*x + b*y = g */
static i64 egcd_table(i64 a, i64 b, i64 *x, i64 *y)
{
    i64 r0 = a, r1 = b, s0 = 1, s1 = 0, t0 = 0, t1 = 1, q;
    const char *cols[5] = { "i", "q", "r", "s", "t" };
    const char *row[5];
    int i = 2;
    sim_table(5, cols);
    row[0] = "0"; row[1] = ""; row[2] = N(r0); row[3] = N(s0); row[4] = N(t0); sim_row(row);
    row[0] = "1"; row[1] = ""; row[2] = N(r1); row[3] = N(s1); row[4] = N(t1); sim_row(row);
    while (r1 != 0) {
        i64 t;
        q = r0 / r1;
        t = r0 - q * r1; r0 = r1; r1 = t;
        t = s0 - q * s1; s0 = s1; s1 = t;
        t = t0 - q * t1; t0 = t1; t1 = t;
        row[0] = N(i++); row[1] = N(q); row[2] = N(r1); row[3] = N(s1); row[4] = N(t1);
        sim_row(row);
    }
    sim_table_end();
    sim_note("each row: r = r(i-2) - q * r(i-1), same for s and t; stop at r = 0");
    *x = s0;
    *y = t0;
    return r0;
}

static void sim_egcd(simargs *a, int inverse)
{
    i64 x0 = get(a, inverse ? "a" : "a", 0), y0 = get(a, inverse ? "n" : "b", 1), g, x, y;
    if (x0 < 0 || y0 <= 0) fail(AT, "egcd needs a >= 0 and b > 0");
    if (inverse) {
        sim_begin("egcd", fmt("Inverse of %s mod %s (extended Euclid)", N(x0), N(y0)));
        sim_step("goal", fmt("find d with %s*d + %s*y = 1; then d = %s^-1 mod %s", N(x0), N(y0), N(x0), N(y0)));
    } else {
        sim_begin("egcd", fmt("Extended Euclid   gcd(%s, %s)", N(x0), N(y0)));
        sim_step("goal", fmt("find x, y with %s*x + %s*y = gcd(%s, %s)", N(x0), N(y0), N(x0), N(y0)));
    }
    g = egcd_table(x0, y0, &x, &y);
    sim_step("result", fmt("gcd = %s,  x = %s,  y = %s", N(g), N(x), N(y)));
    sim_step("check", fmt("%s*%s + %s*%s = %s", N(x0), x < 0 ? fmt("(%s)", N(x)) : N(x), N(y0),
                          y < 0 ? fmt("(%s)", N(y)) : N(y), N(x0 * x + y0 * y)));
    if (g == 1) {
        i64 d = mod_norm(x, y0);
        sim_result(fmt("%s^-1 mod %s = %s%s   (check: %s*%s mod %s = %s)", N(x0), N(y0), N(x),
                       x < 0 ? fmt(" + %s = %s", N(y0), N(d)) : "", N(x0), N(d), N(y0), N(mulmod(x0, d, y0))));
    } else if (inverse) {
        sim_result(fmt("gcd = %s, not 1: %s has no inverse mod %s", N(g), N(x0), N(y0)));
    }
    sim_end();
}

static void sim_phi(simargs *a)
{
    i64 n = get(a, "n", 0), p[64], r = 1;
    int e[64], k, i;
    char *parts = "", *vals = "";
    if (n < 1) fail(AT, "phi needs n >= 1");
    k = factorize(n, p, e);
    sim_begin("phi", fmt("Euler's totient   phi(%s)", N(n)));
    sim_step("factor", fmt("%s = %s", N(n), n == 1 ? "1" : factor_text(n)));
    for (i = 0; i < k; i++) {
        i64 pe = 1, v;
        int j;
        for (j = 0; j < e[i]; j++) pe *= p[i];
        v = pe / p[i] * (p[i] - 1);
        r *= v;
        parts = fmt("%s%sphi(%s)", parts, i ? " * " : "", N(pe));
        vals = fmt("%s%s%s", vals, i ? " * " : "", N(v));
        if (e[i] > 1) sim_note(fmt("phi(%s^%d) = %s^%d * (%s - 1) = %s", N(p[i]), e[i], N(p[i]), e[i] - 1, N(p[i]), N(v)));
    }
    if (k) sim_step("multiply", fmt("phi(%s) = %s = %s", N(n), parts, vals));
    sim_note("phi(p) = p - 1 for a prime; phi(mn) = phi(m) phi(n) when gcd(m, n) = 1");
    sim_result(fmt("phi(%s) = %s", N(n), N(r)));
    sim_end();
}

/* ---- RSA ------------------------------------------------------------------ */

static void sim_rsa(simargs *a)
{
    i64 p = get(a, "p", 0), q = get(a, "q", 1), e = get(a, "e", 2), n, ph, d, x, y, g;

    sim_begin("rsa", fmt("RSA   p = %s, q = %s, e = %s", N(p), N(q), N(e)));
    if (!is_prime(p) || !is_prime(q)) sim_note("warning: p and q should both be prime");
    n = mul_ck(AT, p, q);
    ph = mul_ck(AT, p - 1, q - 1);
    sim_step("n", fmt("n = p*q = %s*%s = %s", N(p), N(q), N(n)));
    sim_step("phi(n)", fmt("phi(n) = (p-1)(q-1) = %s*%s = %s", N(p - 1), N(q - 1), N(ph)));
    g = gcd_ll(e, ph);
    sim_step("gcd", fmt("gcd(e, phi(n)) = gcd(%s, %s) = %s%s", N(e), N(ph), N(g), g == 1 ? "  ok" : "  -> e is not valid"));
    if (g != 1) { sim_result("choose an e with gcd(e, phi(n)) = 1"); sim_end(); return; }
    sim_step("d", fmt("solve %s*d + %s*y = 1 with extended Euclid:", N(e), N(ph)));
    egcd_table(e, ph, &x, &y);
    d = mod_norm(x, ph);
    sim_step("d", fmt("d = %s%s   (check: e*d mod phi(n) = %s*%s mod %s = %s)", N(x),
                      x < 0 ? fmt(" + %s = %s", N(ph), N(d)) : "", N(e), N(d), N(ph), N(mulmod(e, d, ph))));
    sim_result(fmt("public key (e, n) = (%s, %s);  private key d = %s", N(e), N(n), N(d)));

    if (has(a, "m", 3)) {
        i64 m = get(a, "m", 3), c, m2;
        if (m < 0 || m >= n) sim_note(fmt("warning: M must satisfy 0 <= M < n = %s", N(n)));
        c = powmod(m, e, n);
        m2 = powmod(c, d, n);
        sim_step("encrypt", fmt("C = M^e mod n = %s", pow_text(m, e, n)));
        sim_step("decrypt", fmt("M = C^d mod n = %s", pow_text(c, d, n)));
        sim_result(fmt("decrypted %s %s the message %s", N(m2), m2 == mod_norm(m, n) ? "matches" : "DOES NOT match", N(m)));
    }
    if (has(a, "c", -1)) {
        i64 c = get(a, "c", -1);
        sim_step("decrypt", fmt("M = C^d mod n = %s", pow_text(c, d, n)));
    }
    if (has(a, "h", -1)) {
        i64 h = get(a, "h", -1), s = powmod(h, d, n);
        sim_step("sign", fmt("s = H(M)^d mod n = %s", pow_text(h, d, n)));
        sim_step("verify", fmt("s^e mod n = %s  %s H(M) = %s", pow_text(s, e, n), powmod(s, e, n) == mod_norm(h, n) ? "==" : "!=", N(h)));
    }
    sim_note("proof: e*d = k*phi(n) + 1, so M^(ed) = (M^phi(n))^k * M = M (mod n)");
    sim_end();
}

/* ---- Diffie-Hellman --------------------------------------------------------- */

static void sim_dh(simargs *a)
{
    i64 p = get(a, "p", 0), g = get(a, "g", 1), x = get(a, "x", 2), y = get(a, "y", 3), A, B, ka, kb;
    need_mod("p", p);
    sim_begin("dh", fmt("Diffie-Hellman   p = %s, g = %s", N(p), N(g)));
    if (!is_prime(p)) sim_note("warning: p should be prime");
    A = powmod(g, x, p);
    B = powmod(g, y, p);
    sim_step("Alice", fmt("secret x = %s, sends A = g^x mod p = %s", N(x), pow_text(g, x, p)));
    sim_step("Bob", fmt("secret y = %s, sends B = g^y mod p = %s", N(y), pow_text(g, y, p)));
    ka = powmod(B, x, p);
    kb = powmod(A, y, p);
    sim_step("Alice", fmt("K = B^x mod p = %s", pow_text(B, x, p)));
    sim_step("Bob", fmt("K = A^y mod p = %s", pow_text(A, y, p)));
    sim_result(fmt("shared key K = g^(xy) mod p = %s %s", N(ka), ka == kb ? "(both sides agree)" : "(MISMATCH)"));
    sim_note(fmt("Eve sees p = %s, g = %s, A = %s, B = %s; getting K needs a discrete log", N(p), N(g), N(A), N(B)));
    sim_note("plain DH is unauthenticated, so a man in the middle can run one exchange with each side");
    sim_data();
    j_key("A"); j_int(A); j_key("B"); j_int(B); j_key("K"); j_int(ka);
    sim_end();
}

/* ---- ElGamal ------------------------------------------------------------------ */

static void sim_elgamal(simargs *a)
{
    i64 p = get(a, "p", 0), al = get(a, "alpha", 1), x = get(a, "a", 2), beta, r, t;
    need_mod("p", p);
    beta = powmod(al, x, p);
    sim_begin("elgamal", fmt("ElGamal   p = %s, alpha = %s, a = %s", N(p), N(al), N(x)));
    sim_step("keys", fmt("private a = %s; public beta = alpha^a mod p = %s", N(x), pow_text(al, x, p)));
    if (has(a, "k", -1) && has(a, "m", -1)) {
        i64 k = get(a, "k", -1), m = get(a, "m", -1);
        r = powmod(al, k, p);
        t = mulmod(powmod(beta, k, p), m, p);
        sim_step("encrypt", fmt("random k = %s, message m = %s", N(k), N(m)));
        sim_step("r", fmt("r = alpha^k mod p = %s", pow_text(al, k, p)));
        sim_step("t", fmt("t = beta^k * m mod p = %s * %s mod %s = %s", N(powmod(beta, k, p)), N(m), N(p), N(t)));
        sim_result(fmt("ciphertext (r, t) = (%s, %s)", N(r), N(t)));
    } else {
        r = get(a, "r", -1);
        t = get(a, "t", -1);
    }
    {
        i64 ra = powmod(r, x, p), inv = inv_mod(AT, ra, p), m = mulmod(t, inv, p), k;
        sim_step("decrypt", fmt("m = t * r^-a mod p, with (r, t) = (%s, %s)", N(r), N(t)));
        sim_step("r^a", fmt("r^a mod p = %s", pow_text(r, x, p)));
        sim_step("inverse", fmt("%s^-1 mod %s = %s   (%s*%s mod %s = 1)", N(ra), N(p), N(inv), N(ra), N(inv), N(p)));
        sim_result(fmt("m = %s * %s mod %s = %s", N(t), N(inv), N(p), N(m)));
        if (p < 1000000) { /* check with the k that produced r */
            i64 pk = 1 % p;
            for (k = 0; k < p && pk != mod_norm(r, p); k++) pk = mulmod(pk, al, p);
            if (k < p)
                sim_note(fmt("check: alpha^%s = r, and beta^k * m = %s^%s * %s mod %s = %s %s t", N(k), N(beta), N(k), N(m), N(p),
                             N(mulmod(powmod(beta, k, p), m, p)), mulmod(powmod(beta, k, p), m, p) == mod_norm(t, p) ? "==" : "!="));
        }
    }
    sim_note("k must be fresh and secret for every message: reusing it, or k = a, reveals the key");
    sim_end();
}

static void sim_elgamal_sign(simargs *a)
{
    i64 p = get(a, "p", 0), al = get(a, "alpha", 1), x = get(a, "a", 2), k = get(a, "k", 3), m = get(a, "m", 4);
    i64 beta, r, kinv, s, lhs, rhs;
    need_mod("p", p);
    beta = powmod(al, x, p);
    sim_begin("elgamal", fmt("ElGamal signature   p = %s, alpha = %s, a = %s", N(p), N(al), N(x)));
    sim_step("keys", fmt("beta = alpha^a mod p = %s", pow_text(al, x, p)));
    r = powmod(al, k, p);
    sim_step("r", fmt("r = alpha^k mod p = %s", pow_text(al, k, p)));
    if (gcd_ll(k, p - 1) != 1) {
        sim_result(fmt("gcd(k, p-1) = gcd(%s, %s) = %s: k has no inverse mod p-1, choose another k", N(k), N(p - 1), N(gcd_ll(k, p - 1))));
        sim_end();
        return;
    }
    kinv = inv_mod(AT, k, p - 1);
    s = mulmod(kinv, mod_norm(m - mulmod(x, r, p - 1), p - 1), p - 1);
    sim_step("k^-1", fmt("k^-1 mod (p-1) = %s^-1 mod %s = %s", N(k), N(p - 1), N(kinv)));
    sim_step("s", fmt("s = k^-1 (m - a r) mod (p-1) = %s * (%s - %s*%s) mod %s = %s", N(kinv), N(m), N(x), N(r), N(p - 1), N(s)));
    sim_result(fmt("signature (r, s) = (%s, %s)", N(r), N(s)));
    lhs = mulmod(powmod(beta, r, p), powmod(r, s, p), p);
    rhs = powmod(al, m, p);
    sim_step("verify", fmt("beta^r * r^s mod p = %s^%s * %s^%s mod %s = %s", N(beta), N(r), N(r), N(s), N(p), N(lhs)));
    sim_step("", fmt("alpha^m mod p = %s", pow_text(al, m, p)));
    sim_result(lhs == rhs ? "valid: both sides agree" : "INVALID: the two sides differ");
    sim_end();
}

/* ---- DSA -------------------------------------------------------------------- */

static void sim_dsa(simargs *a)
{
    i64 p = get(a, "p", 0), q = get(a, "q", 1), x = get(a, "x", -1), k = get(a, "k", -1), H = get(a, "H", -1);
    i64 g, y, r, kinv, s, w, u1, u2, v;
    need_mod("p", p);
    need_mod("q", q);
    sim_begin("dsa", fmt("DSA   p = %s, q = %s", N(p), N(q)));
    if ((p - 1) % q) sim_note(fmt("warning: q should divide p - 1 = %s", N(p - 1)));
    if (has(a, "g", -1)) {
        g = get(a, "g", -1);
    } else {
        i64 h = get_or(a, "h", -1, 2);
        g = powmod(h, (p - 1) / q, p);
        sim_step("g", fmt("g = h^((p-1)/q) mod p = %s^%s mod %s = %s", N(h), N((p - 1) / q), N(p), N(g)));
    }
    y = powmod(g, x, p);
    sim_step("keys", fmt("private x = %s; public y = g^x mod p = %s", N(x), pow_text(g, x, p)));
    r = mod_norm(powmod(g, k, p), q);
    sim_step("r", fmt("r = (g^k mod p) mod q = (%s) mod %s = %s", pow_text(g, k, p), N(q), N(r)));
    kinv = inv_mod(AT, k, q);
    s = mulmod(kinv, mod_norm(H + mulmod(x, r, q), q), q);
    sim_step("s", fmt("s = k^-1 (H + x r) mod q = %s * (%s + %s*%s) mod %s = %s", N(kinv), N(H), N(x), N(r), N(q), N(s)));
    sim_result(fmt("signature (r, s) = (%s, %s)", N(r), N(s)));
    if (s == 0) { sim_note("s = 0: verification needs s^-1, pick a new k"); sim_end(); return; }
    w = inv_mod(AT, s, q);
    u1 = mulmod(H, w, q);
    u2 = mulmod(r, w, q);
    v = mod_norm(mulmod(powmod(g, u1, p), powmod(y, u2, p), p), q);
    sim_step("w", fmt("w = s^-1 mod q = %s", N(w)));
    sim_step("u1, u2", fmt("u1 = H w mod q = %s,  u2 = r w mod q = %s", N(u1), N(u2)));
    sim_step("v", fmt("v = (g^u1 * y^u2 mod p) mod q = %s", N(v)));
    sim_result(v == r ? "valid: v == r" : "INVALID: v != r");
    sim_step("k leaked", fmt("x = (s k - H) r^-1 mod q = (%s*%s - %s) * %s mod %s = %s", N(s), N(k), N(H), N(inv_mod(AT, r, q)), N(q),
                             N(mulmod(mod_norm(mulmod(s, k, q) - H, q), inv_mod(AT, r, q), q))));
    sim_note("so one leaked or reused k gives away the private key");
    sim_end();
}

/* ---- elliptic curves ------------------------------------------------------------- */

static i64 EA, EB, EP;

static const char *curve_text(void)
{
    const char *s = "y^2 = x^3";
    if (EA) s = fmt("%s %s %sx", s, EA < 0 ? "-" : "+", (EA == 1 || EA == -1) ? "" : N(EA < 0 ? -EA : EA));
    if (EB) s = fmt("%s %s %s", s, EB < 0 ? "-" : "+", N(EB < 0 ? -EB : EB));
    return fmt("%s (mod %s)", s, N(EP));
}

static const char *pt(int inf, i64 x, i64 y) { return inf ? "O" : fmt("(%s, %s)", N(x), N(y)); }

static int on_curve(i64 x, i64 y)
{
    return mulmod(y, y, EP) == mod_norm(mulmod(mulmod(x, x, EP), x, EP) + mulmod(EA, x, EP) + EB, EP);
}

/* R = P + Q, printing the steps when verbose; returns 1 if R = O */
static int ec_add_steps(int pinf, i64 x1, i64 y1, int qinf, i64 x2, i64 y2, i64 *x3, i64 *y3, int verbose)
{
    i64 l, num, den, inv;
    if (pinf) { *x3 = x2; *y3 = y2; return qinf; }
    if (qinf) { *x3 = x1; *y3 = y1; return 0; }
    if (x1 == x2 && mod_norm(y1 + y2, EP) == 0) {
        if (verbose) sim_step("result", fmt("x1 = x2 and y1 = -y2 (mod %s), so P + Q = O (point at infinity)", N(EP)));
        return 1;
    }
    if (x1 == x2) {
        num = mod_norm(3 * mulmod(x1, x1, EP) + EA, EP);
        den = mod_norm(2 * y1, EP);
        inv = inv_mod(AT, den, EP);
        l = mulmod(num, inv, EP);
        if (verbose)
            sim_step("lambda", fmt("(3 x1^2 + a) / (2 y1) = (3*%s^2 + %s) / (2*%s) = %s * %s^-1 = %s * %s = %s (mod %s)",
                                   N(x1), N(EA), N(y1), N(num), N(den), N(num), N(inv), N(l), N(EP)));
    } else {
        num = mod_norm(y2 - y1, EP);
        den = mod_norm(x2 - x1, EP);
        inv = inv_mod(AT, den, EP);
        l = mulmod(num, inv, EP);
        if (verbose)
            sim_step("lambda", fmt("(y2 - y1) / (x2 - x1) = (%s - %s) / (%s - %s) = %s * %s^-1 = %s * %s = %s (mod %s)",
                                   N(y2), N(y1), N(x2), N(x1), N(num), N(den), N(num), N(inv), N(l), N(EP)));
    }
    *x3 = mod_norm(mulmod(l, l, EP) - x1 - x2, EP);
    *y3 = mod_norm(mulmod(l, x1 - *x3, EP) - y1, EP);
    if (verbose) {
        sim_step("x3", fmt("lambda^2 - x1 - x2 = %s^2 - %s - %s = %s (mod %s)", N(l), N(x1), N(x2), N(*x3), N(EP)));
        sim_step("y3", fmt("lambda (x1 - x3) - y1 = %s*(%s - %s) - %s = %s (mod %s)", N(l), N(x1), N(*x3), N(y1), N(*y3), N(EP)));
    }
    return 0;
}

static int read_pt(simargs *a, const char *name, i64 *x, i64 *y)
{
    value *v = arg(a, name, -1);
    if (!v) return 0;
    if ((v->t != V_TUPLE && v->t != V_LIST) || v->n != 2 || v->a[0].t != V_INT || v->a[1].t != V_INT)
        fail(AT, "%s should be a point like (3, 2)", name);
    *x = mod_norm(v->a[0].i, EP);
    *y = mod_norm(v->a[1].i, EP);
    return 1;
}

static void sim_ecc(simargs *a)
{
    i64 x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, disc, x, y, npts = 0;
    int hasP, hasQ, rinf = 1, dbl = 0;
    EA = get(a, "a", 0);
    EB = get(a, "b", 1);
    EP = get(a, "p", 2);
    need_mod("p", EP);
    if (EP > 100000) fail(AT, "ecc: keep p below 100000 for the simulation");
    sim_begin("ecc", fmt("Elliptic curve   %s", curve_text()));
    disc = mod_norm(4 * mulmod(mulmod(EA, EA, EP), EA, EP) + 27 * mulmod(EB, EB, EP), EP);
    sim_step("check", fmt("4a^3 + 27b^2 = %s (mod %s)%s", N(disc), N(EP), disc ? ", not 0: a valid curve" : ": 0, the curve is singular!"));
    hasP = read_pt(a, "P", &x1, &y1);
    hasQ = read_pt(a, "Q", &x2, &y2);
    if (hasP) sim_step("P on curve", fmt("y^2 = %s,  x^3 + ax + b = %s   %s", N(mulmod(y1, y1, EP)),
                                         N(mod_norm(mulmod(mulmod(x1, x1, EP), x1, EP) + mulmod(EA, x1, EP) + EB, EP)),
                                         on_curve(x1, y1) ? "ok" : "NOT ON THE CURVE"));
    if (hasQ) sim_step("Q on curve", on_curve(x2, y2) ? "ok" : "NOT ON THE CURVE");
    if (hasP && !has(a, "k", -1)) {
        if (!hasQ || (x1 == x2 && y1 == y2)) {
            dbl = 1;
            sim_note(fmt("doubling: 2P with P = %s", pt(0, x1, y1)));
            rinf = ec_add_steps(0, x1, y1, 0, x1, y1, &x3, &y3, 1);
        } else {
            sim_note(fmt("adding: P + Q with P = %s, Q = %s", pt(0, x1, y1), pt(0, x2, y2)));
            rinf = ec_add_steps(0, x1, y1, 0, x2, y2, &x3, &y3, 1);
        }
        sim_result(fmt("%s = %s%s", dbl ? "2P" : "P + Q", pt(rinf, x3, y3), rinf ? "" : (on_curve(x3, y3) ? "   (on the curve: ok)" : "   (NOT on the curve!)")));
    }
    if (hasP && has(a, "k", -1)) {
        i64 k = get(a, "k", -1), i, cx = x1, cy = y1;
        int cinf = 0;
        const char *cols[2] = { "i", "i * P" };
        if (k < 1 || k > 1000) fail(AT, "ecc: k must be 1..1000");
        sim_note(fmt("multiples of P = %s by repeated addition", pt(0, x1, y1)));
        sim_table(2, cols);
        for (i = 1; i <= k; i++) {
            const char *row[2];
            row[0] = N(i); row[1] = pt(cinf, cx, cy);
            sim_row(row);
            if (cinf && i > 1) break;
            cinf = ec_add_steps(cinf, cx, cy, 0, x1, y1, &cx, &cy, 0);
        }
        sim_table_end();
        if (i <= k) sim_result(fmt("%s * P = O, so P has order %s", N(i), N(i)));
        else sim_result(fmt("%s * P = %s", N(k), pt(cinf, cx, cy)));
        rinf = cinf; x3 = cx; y3 = cy;
    }
    /* all points, for small p */
    if (EP <= 400) {
        const char *list = "";
        for (x = 0; x < EP; x++)
            for (y = 0; y < EP; y++)
                if (on_curve(x, y)) { npts++; if (npts <= 40) list = fmt("%s%s%s", list, npts > 1 ? " " : "", pt(0, x, y)); }
        sim_step("points", fmt("%s%s", list, npts > 40 ? " ..." : ""));
        sim_result(fmt("#E = %s points + O = %s", N(npts), N(npts + 1)));
    }

    sim_data();
    j_key("a"); j_int(EA); j_key("b"); j_int(EB); j_key("p"); j_int(EP);
    j_key("curve"); j_str(curve_text());
    if (EP <= 400) {
        j_key("points"); j_arr();
        for (x = 0; x < EP; x++)
            for (y = 0; y < EP; y++)
                if (on_curve(x, y)) { j_arr(); j_int(x); j_int(y); j_arr_end(); }
        j_arr_end();
    }
    if (hasP) { j_key("P"); j_arr(); j_int(x1); j_int(y1); j_arr_end(); }
    if (hasQ && !dbl) { j_key("Q"); j_arr(); j_int(x2); j_int(y2); j_arr_end(); }
    if (hasP) { j_key("R"); if (rinf) j_str("O"); else { j_arr(); j_int(x3); j_int(y3); j_arr_end(); } }
    j_key("op"); j_str(has(a, "k", -1) ? "mul" : dbl ? "double" : "add");
    if (hasP && has(a, "k", -1)) {
        i64 k = get(a, "k", -1), i, cx = x1, cy = y1;
        int cinf = 0;
        j_key("multiples"); j_arr();
        for (i = 1; i <= k && i <= 400; i++) {
            if (cinf) { j_str("O"); break; }
            j_arr(); j_int(cx); j_int(cy); j_arr_end();
            cinf = ec_add_steps(cinf, cx, cy, 0, x1, y1, &cx, &cy, 0);
        }
        j_arr_end();
    }
    sim_end();
}

/* ---- block cipher modes ---------------------------------------------------------
 * A toy 16-bit block cipher (a 4-round Feistel network with 8-bit halves)
 * stands in for AES, so whole blocks fit on screen. It is NOT secure; it
 * only has to be a permutation that scrambles all 16 bits. */

static unsigned F8(unsigned r, unsigned k)
{
    unsigned x = ((r ^ k) * 167u + 13u) & 255u;
    return ((x << 3) | (x >> 5)) & 255u;
}

static unsigned rk(unsigned key, int i) { return ((key >> ((i & 1) * 8)) ^ (unsigned)(i * 0x5B)) & 255u; }

static unsigned E16(unsigned key, unsigned p)
{
    unsigned L = (p >> 8) & 255u, R = p & 255u, t;
    int i;
    for (i = 0; i < 4; i++) { t = L ^ F8(R, rk(key, i)); L = R; R = t; }
    return (L << 8) | R;
}

static unsigned D16(unsigned key, unsigned c)
{
    unsigned L = (c >> 8) & 255u, R = c & 255u, t;
    int i;
    for (i = 3; i >= 0; i--) { t = R ^ F8(L, rk(key, i)); R = L; L = t; }
    return (L << 8) | R;
}

enum { ECB, CBC, CFB, OFB, CTR };
static const char *mode_name[5] = { "ECB", "CBC", "CFB", "OFB", "CTR" };
static const char *mode_formula[5] = {
    "C[j] = E(K, P[j])                         P[j] = D(K, C[j])",
    "C[j] = E(K, P[j] xor C[j-1]), C[0] = IV   P[j] = D(K, C[j]) xor C[j-1]",
    "O[j] = E(K, C[j-1]), C[0] = IV; C[j] = P[j] xor O[j]   (full-block CFB)",
    "O[j] = E(K, O[j-1]), O[0] = IV; C[j] = P[j] xor O[j]",
    "C[j] = P[j] xor E(K, IV + j)   (counter T[j] = IV + j)",
};

/* fault: flip `mask` in the block-cipher output of block `fb` (at Alice) */
static void mode_encrypt(int m, unsigned key, unsigned iv, int n, const unsigned *P, unsigned *C, int fb, unsigned mask)
{
    unsigned fbk = iv, o = iv;
    int j;
    for (j = 0; j < n; j++) {
        unsigned e;
        switch (m) {
        case ECB: e = E16(key, P[j]); if (j == fb) e ^= mask; C[j] = e; break;
        case CBC: e = E16(key, P[j] ^ fbk); if (j == fb) e ^= mask; C[j] = e; fbk = e; break;
        case CFB: e = E16(key, fbk); if (j == fb) e ^= mask; C[j] = P[j] ^ e; fbk = C[j]; break;
        case OFB: o = E16(key, o); if (j == fb) o ^= mask; C[j] = P[j] ^ o; break;
        default: e = E16(key, (iv + (unsigned)j) & 0xffff); if (j == fb) e ^= mask; C[j] = P[j] ^ e; break;
        }
    }
}

static void mode_decrypt(int m, unsigned key, unsigned iv, int n, const unsigned *C, unsigned *P)
{
    unsigned fbk = iv, o = iv;
    int j;
    for (j = 0; j < n; j++) {
        switch (m) {
        case ECB: P[j] = D16(key, C[j]); break;
        case CBC: P[j] = D16(key, C[j]) ^ fbk; fbk = C[j]; break;
        case CFB: P[j] = C[j] ^ E16(key, fbk); fbk = C[j]; break;
        case OFB: o = E16(key, o); P[j] = C[j] ^ o; break;
        default: P[j] = C[j] ^ E16(key, (iv + (unsigned)j) & 0xffff); break;
        }
    }
}

static int popcount16(unsigned x) { int c = 0; while (x) { c += (int)(x & 1); x >>= 1; } return c; }

static const char *effect(unsigned diff, unsigned mask)
{
    if (!diff) return "ok";
    if (diff == mask) return "same bit flipped";
    if (popcount16(diff) == 1) return "1 bit flipped";
    return fmt("garbled (%d bits)", popcount16(diff));
}

static void sim_modes(simargs *a)
{
    value *bv = arg(a, "text", 0);
    unsigned P[32], key, iv, mask;
    const char *label[32];
    int n = 0, j, m, fb, bit, transit = 1;
    value *fv, *wv;

    if (!bv) bv = arg(a, "blocks", -1);
    if (!bv) fail(AT, "modes needs text = \"...\" or blocks = [0x1234, ...]");
    if (bv->t == V_STR) {
        size_t i;
        for (i = 0; i < bv->n && n < 32; i += 2) {
            unsigned hi = (unsigned char)bv->s[i], lo = i + 1 < bv->n ? (unsigned char)bv->s[i + 1] : 0;
            P[n] = (hi << 8) | lo;
            label[n] = fmt("\"%c%c\"", hi, lo ? (int)lo : ' ');
            n++;
        }
    } else if (bv->t == V_LIST || bv->t == V_TUPLE) {
        size_t i;
        for (i = 0; i < bv->n && n < 32; i++) {
            if (bv->a[i].t != V_INT) fail(AT, "blocks must be numbers");
            P[n] = (unsigned)(bv->a[i].i & 0xffff);
            label[n] = "";
            n++;
        }
    } else fail(AT, "modes: text should be a string or a list of 16-bit blocks");
    if (n == 0) fail(AT, "modes: no blocks");
    key = (unsigned)(get_or(a, "key", -1, 0x2b7e) & 0xffff);
    iv = (unsigned)(get_or(a, "iv", -1, 0x1a2b) & 0xffff);
    fb = n >= 2 ? 1 : 0;
    bit = 3;
    if ((fv = arg(a, "flip", -1)) != NULL) {
        if ((fv->t != V_TUPLE && fv->t != V_LIST) || fv->n != 2 || fv->a[0].t != V_INT || fv->a[1].t != V_INT)
            fail(AT, "flip = (block, bit), block counted from 1, bit 0..15 (0 = rightmost)");
        fb = (int)fv->a[0].i - 1;
        bit = (int)fv->a[1].i;
        if (fb < 0 || fb >= n || bit < 0 || bit > 15) fail(AT, "flip: block must be 1..%d and bit 0..15", n);
    }
    if ((wv = arg(a, "at", -1)) != NULL) {
        if (wv->t != V_STR) fail(AT, "at = \"transit\" or at = \"encrypt\"");
        transit = !(wv->n == 7 && !memcmp(wv->s, "encrypt", 7));
    }
    mask = 1u << bit;

    sim_begin("modes", fmt("Modes of operation   %d blocks, bit %d of block %d flipped %s", n, bit, fb + 1,
                           transit ? "in transit" : "while Alice computes it"));
    sim_note(fmt("toy 16-bit block cipher (4-round Feistel), key %s, IV/nonce %s; not secure, just small", hex4(key), hex4(iv)));
    sim_note(transit ? fmt("the error: ciphertext block C%d has bit %d flipped on the way to Bob", fb + 1, bit)
                     : fmt("the error: the block-cipher output for block %d has bit %d flipped inside Alice's machine, and she keeps using it",
                           fb + 1, bit));

    {
        static unsigned CC[5][32], CF[5][32], PP[5][32];
        int same_c = 0, i2;
        for (m = 0; m < 5; m++) {
            const char *cols[5] = { "j", "P", "C sent", "Bob gets P", "effect" };
            mode_encrypt(m, key, iv, n, P, CC[m], -1, 0);
            mode_encrypt(m, key, iv, n, P, CF[m], transit ? -1 : fb, mask);
            if (transit) CF[m][fb] ^= mask;
            mode_decrypt(m, key, iv, n, CF[m], PP[m]);
            sim_step(mode_name[m], mode_formula[m]);
            sim_table(5, cols);
            for (j = 0; j < n; j++) {
                const char *row[5];
                row[0] = N(j + 1);
                row[1] = fmt("%s %s", hex4(P[j]), label[j]);
                row[2] = hex4(CF[m][j]);
                row[3] = hex4(PP[m][j]);
                row[4] = effect(PP[m][j] ^ P[j], mask);
                sim_row(row);
            }
            sim_table_end();
        }
        for (j = 0; j < n; j++)
            for (i2 = j + 1; i2 < n; i2++)
                if (P[j] == P[i2] && CC[ECB][j] == CC[ECB][i2]) same_c = 1;
        if (same_c) sim_note("ECB: equal plaintext blocks gave equal ciphertext blocks, so the pattern leaks");

        sim_data();
        j_key("n"); j_int(n);
        j_key("flip"); j_arr(); j_int(fb); j_int(bit); j_arr_end();
        j_key("transit"); j_int(transit);
        j_key("key"); j_int(key);
        j_key("iv"); j_int(iv);
        j_key("P"); j_arr(); for (j = 0; j < n; j++) j_int(P[j]); j_arr_end();
        j_key("labels"); j_arr(); for (j = 0; j < n; j++) j_str(label[j]); j_arr_end();
        j_key("modes"); j_arr();
        for (m = 0; m < 5; m++) {
            j_obj();
            j_key("name"); j_str(mode_name[m]);
            j_key("formula"); j_str(mode_formula[m]);
            j_key("C"); j_arr(); for (j = 0; j < n; j++) j_int(CC[m][j]); j_arr_end();
            j_key("Cf"); j_arr(); for (j = 0; j < n; j++) j_int(CF[m][j]); j_arr_end();
            j_key("P2"); j_arr(); for (j = 0; j < n; j++) j_int(PP[m][j]); j_arr_end();
            j_key("effect"); j_arr(); for (j = 0; j < n; j++) j_str(effect(PP[m][j] ^ P[j], mask)); j_arr_end();
            j_obj_end();
        }
        j_arr_end();
    }
    sim_end();
}

/* ---- dispatch ---------------------------------------------------------------------- */

void run_sim(const gram_node *at, const char *name, size_t len, simargs *a)
{
#define IS(s) (len == sizeof s - 1 && !memcmp(name, s, len))
    AT = at;
    if (IS("lfsr")) sim_lfsr(a);
    else if (IS("modpow")) sim_modpow(a);
    else if (IS("egcd")) sim_egcd(a, 0);
    else if (IS("inverse")) sim_egcd(a, 1);
    else if (IS("phi")) sim_phi(a);
    else if (IS("rsa")) sim_rsa(a);
    else if (IS("dh")) sim_dh(a);
    else if (IS("elgamal")) sim_elgamal(a);
    else if (IS("elgamal_sign")) sim_elgamal_sign(a);
    else if (IS("dsa")) sim_dsa(a);
    else if (IS("ecc")) sim_ecc(a);
    else if (IS("modes")) sim_modes(a);
    else
        fail(at, "unknown simulation '%.*s' (have: lfsr, modpow, egcd, inverse, phi, rsa, dh, elgamal, "
                 "elgamal_sign, dsa, ecc, modes)", (int)len, name);
#undef IS
}
