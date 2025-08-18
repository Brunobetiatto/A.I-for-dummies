// src/main.c

#include "interface/interface.h"
#ifdef _WIN32
#include <Windows.h>
#ifndef _countof
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
#endif
BOOL  backend_start(const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*, int);
char* backend_request(const char *req_utf8, size_t *out_len_opt);
void  backend_stop(void);
#endif

/* ---- globals expected by interface.h ---- */
GtkEntry     *entry_global = NULL;
GtkTreeView  *view_global  = NULL;
GtkListStore *store_global = NULL;

#define TABLE_NAME "dataset"

/* ---- Datasets tab context ---- */
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

/* ---- forward decls ---- */
static gboolean refresh_datasets_tab(gpointer data);
static void     on_destroy(GtkWidget *w, gpointer data);
static void     on_entry_changed(GtkEditable *e, gpointer data);
static void     on_refresh_clicked(GtkButton *b, gpointer data);
static GtkWidget* make_refresh_button(TabCtx *ctx);
static TabCtx*  add_datasets_tab(GtkNotebook *nb);
static void     add_environment_tab(GtkNotebook *nb);

/* ---- selection preservation helpers ---- */

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

/* ---- Datasets refresh (no flicker, preserves selection) ---- */
/* Force-aware core refresh */
static gboolean refresh_datasets_tab_core(TabCtx *ctx, gboolean force) {
    if (!ctx) return TRUE;

    size_t nbytes = 0;
    char *raw = backend_request(ctx->dump_cmd, &nbytes);
    if (!raw || nbytes == 0) { g_free(raw); return TRUE; }

    /* Safe, NUL-terminated copy for string APIs */
    gchar *csv = g_strndup(raw, nbytes);
    g_free(raw);

    /* Skip if identical data unless forced */
    guint h_data = g_str_hash(csv);
    if (!force && ctx->last_data_hash == h_data) { g_free(csv); return TRUE; }

    /* Parse CSV */
    char *buf = csv;
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    if (!line) { g_free(csv); return TRUE; }

    /* header */
    GPtrArray *headers = csv_parse_line_all(line);
    if (!headers || headers->len == 0) {
        if (headers) g_ptr_array_free(headers, TRUE);
        g_free(csv);
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

        /* probe for an "id" column to key selection */
        ctx->id_col = -1;
        for (guint i = 0; i < headers->len; ++i) {
            const char *hn = (const char*)headers->pdata[i];
            if (g_ascii_strcasecmp(hn, "id") == 0) { ctx->id_col = (int)i; break; }
        }
        ctx->last_header_hash = h_head;
    }

    /* Preserve selection key & refill rows */
    gchar *sel_key = get_selected_key(ctx);
    gtk_list_store_clear(ctx->store);
    const char *needle = gtk_entry_get_text(ctx->entry);

    for (line = strtok_r(NULL, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == '\0') continue;
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

    if (sel_key) { restore_selection_by_key(ctx, sel_key); g_free(sel_key); }
    g_ptr_array_free(headers, TRUE);
    g_free(csv);

    ctx->last_data_hash = h_data;
    return TRUE;
}

/* Wrappers keep your existing signatures/uses clean */
static gboolean refresh_timer_cb(gpointer data) {
    return refresh_datasets_tab_core((TabCtx*)data, FALSE);
}

static void on_entry_changed(GtkEditable *e, gpointer data) {
    (void)e;
    refresh_datasets_tab_core((TabCtx*)data, FALSE);
}

static void on_refresh_clicked(GtkButton *b, gpointer data) {
    (void)b;
    /* Force a repaint regardless of last hash */
    refresh_datasets_tab_core((TabCtx*)data, TRUE);
}

static gboolean refresh_datasets_tab(gpointer data) {
    if (!data) return TRUE;

    TabCtx *ctx = (TabCtx*)data;

    size_t nbytes = 0;
    char *csv = backend_request(ctx->dump_cmd, &nbytes);
    if (!csv || nbytes == 0) { g_free(csv); return TRUE; }

    /* Skip if identical data */
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

        /* probe for an "id" column to key selection */
        ctx->id_col = -1;
        for (guint i = 0; i < headers->len; ++i) {
            const char *hn = (const char*)headers->pdata[i];
            if (g_ascii_strcasecmp(hn, "id") == 0) { ctx->id_col = (int)i; break; }
        }
        ctx->last_header_hash = h_head;
    }

    /* Preserve selection key & refill rows */
    gchar *sel_key = get_selected_key(ctx);

    gtk_list_store_clear(ctx->store);
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

    if (sel_key) { restore_selection_by_key(ctx, sel_key); g_free(sel_key); }
    g_ptr_array_free(headers, TRUE);
    g_free(buf);

    ctx->last_data_hash = h_data;
    return TRUE;
}

/* ---- helpers: load png from the EXE folder (Windows) or CWD (others) ---- */

static gchar* build_asset_path_utf8(const char *basename) {
#ifdef _WIN32
    WCHAR exePath[MAX_PATH], exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, _countof(exePath));
    wcscpy(exeDir, exePath);
    WCHAR *slash = wcsrchr(exeDir, L'\\'); if (slash) *slash = 0;

    int need = WideCharToMultiByte(CP_UTF8, 0, exeDir, -1, NULL, 0, NULL, NULL);
    char *dir_utf8 = (char*)g_malloc(need);
    WideCharToMultiByte(CP_UTF8, 0, exeDir, -1, dir_utf8, need, NULL, NULL);
    gchar *full = g_build_filename(dir_utf8, basename, NULL);
    g_free(dir_utf8);
    return full;
#else
    gchar *cwd = g_get_current_dir();
    gchar *full = g_build_filename(cwd, basename, NULL);
    g_free(cwd);
    return full;
#endif
}

/* ---- Refresh button and callbacks (pure C) ---- */


static GtkWidget* make_refresh_button(TabCtx *ctx) {
    GtkWidget *btn = NULL;

    gchar *imgpath = build_asset_path_utf8("assets/refresh_button.png");
    GError *err = NULL;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file(imgpath, &err);
    if (pix) {
        /* scale to a comfortable size */
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, 20, 20, GDK_INTERP_BILINEAR);
        g_object_unref(pix);
        GtkWidget *image = gtk_image_new_from_pixbuf(scaled ? scaled : pix);
        if (scaled) g_object_unref(scaled);
        btn = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(btn), image);
        gtk_widget_set_tooltip_text(btn, "Refresh datasets");
    } else {
        /* fallback: text button */
        btn = gtk_button_new_with_label("Refresh");
    }
    if (err) g_error_free(err);
    g_free(imgpath);

    g_signal_connect(btn, "clicked", G_CALLBACK(on_refresh_clicked), ctx);
    return btn;
}

/* ---- UI building ---- */

static TabCtx* add_datasets_tab(GtkNotebook *nb) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* top row: refresh button (LEFT) + filter entry */
    GtkWidget *hrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filter (matches any column)...");

    /* tree view */
    GtkWidget *tree = gtk_tree_view_new();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    /* context FIRST */
    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry   = GTK_ENTRY(entry);
    ctx->view    = GTK_TREE_VIEW(tree);
    ctx->store   = NULL;
    ctx->dump_cmd= g_strdup_printf("DUMP %s\n", TABLE_NAME);
    ctx->n_cols  = 0;
    ctx->id_col  = -1;

    /* real refresh button bound to ctx (LEFT) */
    GtkWidget *btn = make_refresh_button(ctx);
    gtk_box_pack_start(GTK_BOX(hrow), btn, FALSE, FALSE, 0);   /* left-most */
    gtk_box_pack_start(GTK_BOX(hrow), entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hrow,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE,  TRUE,  0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, vbox, lbl);

    /* signals */
    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), ctx);

    gtk_widget_show_all(vbox);

    /* initial fill + periodic refresh */
    refresh_datasets_tab_core(ctx, TRUE);          /* force once on startup */
    g_timeout_add(2000, refresh_timer_cb, ctx);    /* gentle background updates */

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

/* ---- destroy ---- */
static void on_destroy(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
#ifdef _WIN32
    backend_stop();
#endif
    gtk_main_quit();
}

/* ---- main ---- */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* EDIT credentials (or load from env) */
    const WCHAR *DB   = L"aifordummies";
    const WCHAR *USER = L"root";
    const WCHAR *PASS = L"pepsi@123";
    const WCHAR *HOST = L"localhost";
    const int    PORT = 3306;

#ifdef _WIN32
    if (!backend_start(DB, USER, PASS, HOST, PORT)) {
        /* Show a message and keep GUI up; manual refresh will keep failing until fixed */
        MessageBoxW(NULL,
            L"Failed to start the Python backend.\n\n"
            L"Check python\\venv and DB connectivity.",
            L"AI-for-dummies", MB_OK | MB_ICONERROR);
    }
#endif

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AI For Dummies");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), notebook);

    add_environment_tab(GTK_NOTEBOOK(notebook));
    add_datasets_tab(GTK_NOTEBOOK(notebook));

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
