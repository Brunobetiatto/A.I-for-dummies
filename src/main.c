// src/main.c — single-table, two-tab GUI (pure C, no lambdas)

#include "interface/interface.h"

/* ---- backend API from communicator.h (single Python worker) ---- */
#ifdef _WIN32
BOOL  backend_start(const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*, int);
char* backend_request(const char *req_utf8, size_t *out_len_opt);
void  backend_stop(void);
#endif

/* ---- globals expected by interface.h (defined exactly once here) ---- */
GtkEntry     *entry_global = NULL;
GtkTreeView  *view_global  = NULL;
GtkListStore *store_global = NULL;

/* ---- CONFIG: lock to one table (switch to "datasets" later) ---- */
#define TABLE_NAME "usuarios"

/* ---- Tab context for the Datasets tab ---- */
typedef struct {
    GtkEntry     *entry;        /* free-text filter */
    GtkTreeView  *view;         /* data view */
    GtkListStore *store;        /* model owned here */
    gchar        *dump_cmd;     /* "DUMP <table>\n" */
    guint         last_data_hash;
    guint         last_header_hash;
    int           n_cols;
    int           id_col;       /* -1 if none found */
} TabCtx;

/* -------- forward declarations -------- */
static gboolean refresh_datasets_tab(gpointer data);
static void     on_destroy(GtkWidget *w, gpointer data);
static void     on_entry_changed(GtkEditable *e, gpointer data);
static TabCtx*  add_datasets_tab(GtkNotebook *nb);
static void     add_environment_tab(GtkNotebook *nb);

/* -------- selection preservation helpers -------- */

static gchar* make_row_key_from_iter(GtkTreeModel *model, GtkTreeIter *it, int n_cols, int id_col) {
    if (id_col >= 0) {
        gchar *v = NULL;
        gtk_tree_model_get(model, it, id_col, &v, -1);
        return v ? v : g_strdup("");
    }
    GString *acc = g_string_new(NULL);
    for (int i = 0; i < n_cols; ++i) {
        gchar *v = NULL;
        gtk_tree_model_get(model, it, i, &v, -1);
        g_string_append(acc, v ? v : "");
        g_free(v);
        if (i + 1 < n_cols) g_string_append_c(acc, 0x1f); /* unit separator */
    }
    return g_string_free(acc, FALSE);
}

static gchar* get_selected_key(TabCtx *ctx) {
    if (!ctx->store) return NULL;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(ctx->view);
    GtkTreeModel *model = GTK_TREE_MODEL(ctx->store);
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(sel, &model, &it)) {
        return make_row_key_from_iter(model, &it, ctx->n_cols, ctx->id_col);
    }
    return NULL;
}

static void restore_selection_by_key(TabCtx *ctx, const gchar *key) {
    if (!ctx->store || !key || !*key) return;
    GtkTreeModel *model = GTK_TREE_MODEL(ctx->store);
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(model, &it);
    while (valid) {
        gchar *rk = make_row_key_from_iter(model, &it, ctx->n_cols, ctx->id_col);
        gboolean match = (g_strcmp0(rk, key) == 0);
        g_free(rk);
        if (match) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &it);
            gtk_tree_view_set_cursor(ctx->view, path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(ctx->view, path, NULL, FALSE, 0.0, 0.0);
            gtk_tree_path_free(path);
            return;
        }
        valid = gtk_tree_model_iter_next(model, &it);
    }
}

/* -------- refresh logic (no flicker, preserves selection) -------- */

static gboolean refresh_datasets_tab(gpointer data) {
    TabCtx *ctx = (TabCtx*)data;

    size_t nbytes = 0;
    char *csv = backend_request(ctx->dump_cmd, &nbytes);
    if (!csv || nbytes == 0) { g_free(csv); return TRUE; }

    /* Skip work if data identical */
    guint h_data = g_str_hash(csv);
    if (ctx->last_data_hash == h_data) { g_free(csv); return TRUE; }

    /* Parse CSV */
    char *buf = g_strdup(csv);
    g_free(csv);
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    if (!line) { g_free(buf); return TRUE; }

    /* header */
    GPtrArray *headers = csv_parse_line_all(line);
    if (!headers || headers->len == 0) {
        if (headers) g_ptr_array_free(headers, TRUE);
        g_free(buf);
        return TRUE;
    }

    /* Rebuild columns ONLY if header changed */
    GString *hcat = g_string_new(NULL);
    for (guint i = 0; i < headers->len; ++i) {
        g_string_append(hcat, (const char*)headers->pdata[i]);
        g_string_append_c(hcat, '\n');
    }
    guint h_head = g_str_hash(hcat->str);
    g_string_free(hcat, TRUE);

    if (!ctx->store || ctx->last_header_hash != h_head) {
        if (ctx->store) g_object_unref(ctx->store);
        ctx->store = create_model_with_n_cols((gint)headers->len);
        gtk_tree_view_set_model(ctx->view, GTK_TREE_MODEL(ctx->store));
        build_columns_from_headers(ctx->view, headers);
        ctx->n_cols = (int)headers->len;

        /* heuristics for id column: exact "id" (case-insensitive) */
        ctx->id_col = -1;
        for (guint i = 0; i < headers->len; ++i) {
            const char *hn = (const char*)headers->pdata[i];
            if (g_ascii_strcasecmp(hn, "id") == 0) { ctx->id_col = (int)i; break; }
        }
        ctx->last_header_hash = h_head;
    }

    /* Preserve selection key & fill rows */
    gchar *sel_key = get_selected_key(ctx);

    /* wipe rows */
    gtk_list_store_clear(ctx->store);

    /* add rows (respect free-text filter across all fields) */
    const char *needle = gtk_entry_get_text(ctx->entry);
    line = strtok_r(NULL, "\n", &save);
    while (line) {
        if (line[0] != 'I' && line[0] != '-') {
            GPtrArray *fields = csv_parse_line_all(line);
            if (fields && fields->len >= headers->len) {
                if (row_matches_filter(fields, needle)) {
                    GtkTreeIter it;
                    gtk_list_store_append(ctx->store, &it);
                    for (guint i = 0; i < headers->len; ++i) {
                        const char *v = (const char*)fields->pdata[i];
                        gtk_list_store_set(ctx->store, &it, (gint)i, v ? v : "", -1);
                    }
                }
            }
            if (fields) g_ptr_array_free(fields, TRUE);
        }
        line = strtok_r(NULL, "\n", &save);
    }

    /* restore selection & cleanup */
    if (sel_key) { restore_selection_by_key(ctx, sel_key); g_free(sel_key); }
    g_ptr_array_free(headers, TRUE);
    g_free(buf);

    ctx->last_data_hash = h_data;
    return TRUE;
}

/* -------- UI building -------- */

static TabCtx* add_datasets_tab(GtkNotebook *nb) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filter (matches any column)...");
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

    GtkWidget *tree = gtk_tree_view_new();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, vbox, lbl);
    gtk_widget_show_all(vbox);

    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry = GTK_ENTRY(entry);
    ctx->view  = GTK_TREE_VIEW(tree);
    ctx->store = NULL;
    ctx->dump_cmd = g_strdup_printf("DUMP %s\n", TABLE_NAME);
    ctx->n_cols = 0;
    ctx->id_col = -1;

    /* initial fill + periodic refresh */
    refresh_datasets_tab(ctx);
    g_timeout_add(2000, refresh_datasets_tab, ctx);

    /* live filtering without lambdas */
    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), ctx);

    return ctx;
}

static void add_environment_tab(GtkNotebook *nb) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *placeholder = gtk_label_new("Environment (coming soon)");
    gtk_box_pack_start(GTK_BOX(vbox), placeholder, TRUE, TRUE, 0);
    GtkWidget *lbl = gtk_label_new("Environment");
    gtk_notebook_append_page(nb, vbox, lbl);
    gtk_widget_show_all(vbox);
}

/* -------- callbacks (pure C) -------- */

static void on_entry_changed(GtkEditable *e, gpointer data) {
    (void)e;
    refresh_datasets_tab(data);
}

static void on_destroy(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
#ifdef _WIN32
    backend_stop();
#endif
    gtk_main_quit();
}

/* -------- main -------- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* EDIT credentials (or load from env) */
    const WCHAR *DB   = L"commtratta";
    const WCHAR *USER = L"root";
    const WCHAR *PASS = L"pepsi@123";
    const WCHAR *HOST = L"localhost";
    const int    PORT = 3306;

#ifdef _WIN32
    if (!backend_start(DB, USER, PASS, HOST, PORT)) {
        g_printerr("Failed to start Python backend.\n");
        return 1;
    }
#endif

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AI For Dummies - Datasets");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), notebook);

    /* Tabs: Environment (blank) + Datasets (single table) */
    add_environment_tab(GTK_NOTEBOOK(notebook));
    add_datasets_tab(GTK_NOTEBOOK(notebook));

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
