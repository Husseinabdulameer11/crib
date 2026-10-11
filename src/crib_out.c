/* crib_out.c - everything crib prints goes through here. Text goes to
 * stdout; with --html every line and simulation is also recorded as JSON
 * and written into a copy of the viewer page (crib_view.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crib.h"
#include "crib_view.h"

/* ---- growable byte buffer for the JSON ------------------------------------ */

static char *J;
static size_t jlen, jcap;
static int html_on;

static void jput(const char *s, size_t n)
{
    if (!html_on) return;
    if (jlen + n + 1 > jcap) {
        jcap = (jlen + n + 1) * 2 + 4096;
        J = (char *)realloc(J, jcap);
        if (!J) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memcpy(J + jlen, s, n);
    jlen += n;
}

static void jraw(const char *s) { jput(s, strlen(s)); }

static void jstring(const char *s)
{
    char esc[8];
    jraw("\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; jput(esc, 2); }
        else if (c == '\n') jraw("\\n");
        else if (c == '<') jraw("\\u003c"); /* keeps "</script>" out of the page */
        else if (c < 32) { sprintf(esc, "\\u%04x", c); jraw(esc); }
        else jput((const char *)&c, 1);
    }
    jraw("\"");
}

/* ---- items: runs of text lines, and simulations ------------------------------ */

static int nitems, in_text, text_first;
static const char *stmt_code = "";
static int stmt_line;
static const char *src_name = "", *src_text = "";

void out_set_source(const char *name, const char *text, size_t len)
{
    src_name = name;
    src_text = fmt("%.*s", (int)len, text);
}

void out_set_stmt(const char *code, size_t len, int line)
{
    (void)len;
    stmt_code = code;
    stmt_line = line;
}

static void item_start(void)
{
    if (nitems++) jraw(",");
}

static void close_text(void)
{
    if (in_text) { jraw("]}"); in_text = 0; }
}

void out_line(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    if (!html_on) return;
    if (!in_text) {
        item_start();
        jraw("{\"type\":\"text\",\"lines\":[");
        in_text = 1;
        text_first = 1;
    }
    if (!text_first) jraw(",");
    text_first = 0;
    jraw("{\"line\":");
    jraw(N(stmt_line));
    jraw(",\"code\":");
    jstring(stmt_code);
    jraw(",\"text\":");
    jstring(s);
    jraw("}");
}

/* ---- simulations ---------------------------------------------------------------- */

static int line_first, data_open;

/* tables are buffered so the terminal columns can be aligned */
static int tcols, trows;
static const char *tcell[512][12];

void sim_begin(const char *type, const char *title)
{
    printf("\n=== %s ===\n", title);
    if (!html_on) return;
    close_text();
    item_start();
    jraw("{\"type\":");
    jstring(type);
    jraw(",\"title\":");
    jstring(title);
    jraw(",\"line\":");
    jraw(N(stmt_line));
    jraw(",\"code\":");
    jstring(stmt_code);
    jraw(",\"lines\":[");
    line_first = 1;
    data_open = 0;
}

static void line_rec(const char *kind, const char *label, const char *text)
{
    if (!html_on) return;
    if (!line_first) jraw(",");
    line_first = 0;
    jraw("{\"k\":");
    jstring(kind);
    if (label) { jraw(",\"label\":"); jstring(label); }
    jraw(",\"text\":");
    jstring(text);
    jraw("}");
}

void sim_step(const char *label, const char *text)
{
    printf("  %-10s %s\n", label, text);
    line_rec("step", label, text);
}

void sim_note(const char *text)
{
    printf("  - %s\n", text);
    line_rec("note", NULL, text);
}

void sim_result(const char *text)
{
    printf("  => %s\n", text);
    line_rec("result", NULL, text);
}

void sim_table(int ncols, const char *const *cols)
{
    int i;
    tcols = ncols > 12 ? 12 : ncols;
    trows = 0;
    for (i = 0; i < tcols; i++) tcell[0][i] = fmt("%s", cols[i]);
    trows = 1;
}

void sim_row(const char *const *cells)
{
    int i;
    if (trows == 512) return;
    for (i = 0; i < tcols; i++) tcell[trows][i] = fmt("%s", cells[i]); /* N() buffers rotate */
    trows++;
}

void sim_table_end(void)
{
    int w[12], i, r;
    for (i = 0; i < tcols; i++) {
        w[i] = 0;
        for (r = 0; r < trows; r++) {
            int l = (int)strlen(tcell[r][i]);
            if (l > w[i]) w[i] = l;
        }
    }
    for (r = 0; r < trows; r++) {
        fputs("   ", stdout);
        for (i = 0; i < tcols; i++) printf(" %*s ", w[i], tcell[r][i]);
        fputc('\n', stdout);
        if (r == 0) {
            fputs("   ", stdout);
            for (i = 0; i < tcols; i++) { int k; fputc(' ', stdout); for (k = 0; k < w[i]; k++) fputc('-', stdout); fputc(' ', stdout); }
            fputc('\n', stdout);
        }
    }
    if (!html_on) return;
    if (!line_first) jraw(",");
    line_first = 0;
    jraw("{\"k\":\"table\",\"cols\":[");
    for (i = 0; i < tcols; i++) { if (i) jraw(","); jstring(tcell[0][i]); }
    jraw("],\"rows\":[");
    for (r = 1; r < trows; r++) {
        jraw(r > 1 ? ",[" : "[");
        for (i = 0; i < tcols; i++) { if (i) jraw(","); jstring(tcell[r][i]); }
        jraw("]");
    }
    jraw("]}");
}

/* ---- JSON writer for the data object --------------------------------------------- */

static int jdepth, jfirst[64], after_key;

static void before_value(void)
{
    if (after_key) { after_key = 0; return; }
    if (!jfirst[jdepth]) jraw(",");
    jfirst[jdepth] = 0;
}

void sim_data(void)
{
    jraw("],\"data\":{");
    data_open = 1;
    jdepth = 1;
    jfirst[1] = 1;
    after_key = 0;
}

void sim_end(void)
{
    if (!html_on) return;
    jraw(data_open ? "}}" : "]}");
    data_open = 0;
}

void j_key(const char *k) { if (!html_on) return; before_value(); jstring(k); jraw(":"); after_key = 1; }
void j_int(i64 x) { if (!html_on) return; before_value(); jraw(N(x)); }
void j_str(const char *s) { if (!html_on) return; before_value(); jstring(s); }
void j_obj(void) { if (!html_on) return; before_value(); jraw("{"); jfirst[++jdepth] = 1; }
void j_obj_end(void) { if (!html_on) return; jraw("}"); jdepth--; }
void j_arr(void) { if (!html_on) return; before_value(); jraw("["); jfirst[++jdepth] = 1; }
void j_arr_end(void) { if (!html_on) return; jraw("]"); jdepth--; }

/* ---- the page ------------------------------------------------------------------------ */

int out_html_begin(void)
{
    html_on = 1;
    return 1;
}

int out_html_write(const char *path)
{
    FILE *f = fopen(path, "wb");
    const char *mark = "/*CRIB_DATA*/";
    const char *at = strstr(crib_view, mark);
    if (!f) return 0;
    close_text();
    if (!at) at = crib_view + strlen(crib_view);
    fwrite(crib_view, 1, (size_t)(at - crib_view), f);
    fputs("{\"file\":", f);
    html_on = 1;
    {   /* reuse jstring by writing into the buffer and copying out */
        size_t keep = jlen;
        jstring(src_name);
        jraw(",\"source\":");
        jstring(src_text);
        fwrite(J + keep, 1, jlen - keep, f);
        jlen = keep;
    }
    fputs(",\"items\":[", f);
    if (jlen) fwrite(J, 1, jlen, f);
    fputs("]}", f);
    if (*at) fputs(at + strlen(mark), f);
    return fclose(f) == 0;
}
