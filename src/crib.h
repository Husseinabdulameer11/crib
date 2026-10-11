/* crib.h - shared between crib.c (the language) and crib_sim.c (the
 * step-by-step simulations behind `sim name(...)`). */
#ifndef CRIB_H
#define CRIB_H

#include <stddef.h>
#include "gram.h"

typedef long long i64;

/* ---- values ------------------------------------------------------------ */

typedef enum { V_NIL, V_INT, V_STR, V_LIST, V_TUPLE, V_POLY } vtype;

typedef struct value {
    vtype t;
    i64 i;             /* V_INT; V_POLY: bit k = coefficient of x^k */
    size_t n;          /* V_STR length, V_LIST/V_TUPLE item count */
    const char *s;     /* V_STR, not NUL-terminated */
    struct value *a;   /* V_LIST/V_TUPLE items */
} value;

value v_nil(void);
value v_int(i64 i);
value v_poly(i64 bits);
value v_str(const char *s, size_t n);
value v_cstr(const char *s);
value v_tuple(size_t n, const value *items);
value v_list(size_t n, const value *items);
const char *vtype_name(vtype t);
const char *show_value(value v);      /* text form, NUL-terminated */

/* ---- errors and memory ------------------------------------------------- */

void fail(const gram_node *at, const char *fmt, ...);
void *heap_alloc(size_t n);
char *fmt(const char *f, ...);        /* printf into the heap */
const char *N(i64 x);                 /* number -> text (rotating buffers) */

/* ---- number theory (crib.c) ---------------------------------------------- */

i64 mod_norm(i64 a, i64 m);
i64 mulmod(i64 a, i64 b, i64 m);
i64 powmod(i64 a, i64 e, i64 m);
i64 gcd_ll(i64 a, i64 b);
i64 egcd_ll(i64 a, i64 b, i64 *x, i64 *y);
i64 inv_mod(const gram_node *at, i64 a, i64 m); /* fails if no inverse */
int is_prime(i64 n);
int factorize(i64 n, i64 *p, int *e);           /* returns #distinct primes */
i64 phi_ll(i64 n);
i64 add_ck(const gram_node *at, i64 a, i64 b);
i64 mul_ck(const gram_node *at, i64 a, i64 b);

/* ---- GF(2) polynomials (crib.c) --------------------------------------- */

int poly_deg(i64 f);
const char *poly_text(i64 f);
const char *poly_factor_text(i64 f, int *irreducible);

/* ---- output: terminal text plus, with --html, a JSON record per item ---- */

void out_line(const char *s);         /* a line of ordinary program output */

/* A simulation writes a title, then lines (steps, notes, tables), then
 * optionally a type-specific data object for the HTML viewer. */
void sim_begin(const char *type, const char *title);
void sim_step(const char *label, const char *text);
void sim_note(const char *text);
void sim_result(const char *text);
void sim_table(int ncols, const char *const *cols);
void sim_row(const char *const *cells);
void sim_table_end(void);
void sim_data(void);                  /* begin the data object */
void sim_end(void);

/* JSON writer for the data object (only does anything with --html) */
void j_key(const char *k);
void j_int(i64 x);
void j_str(const char *s);
void j_obj(void);
void j_obj_end(void);
void j_arr(void);
void j_arr_end(void);

/* ---- simulations (crib_sim.c) ------------------------------------------ */

typedef struct {
    const gram_node *at;              /* for error messages */
    int n;
    const char *name[16];             /* NULL for positional arguments */
    value v[16];
} simargs;

void run_sim(const gram_node *at, const char *name, size_t len, simargs *a);

#endif
