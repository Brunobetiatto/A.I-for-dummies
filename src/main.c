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

/* ==== Metal theme (global) ================================================ */
static const char *METAL_CSS =
"/* Base window background (Metal-like brushed gray) */\n"
"window, dialog, .background {"
"  background-image: linear-gradient(to bottom, #d0d0d0, rgba(189, 189, 189, 1));"
"}\n"
"\n"
"/* Panels / frames with light bevel */\n"
".metal-panel {"
"  background-image: linear-gradient(to bottom, #cfcfcf, #b9b9b9);"
"  border: 1px solid #7f7f7f;"
"  box-shadow: inset 1px 1px 0px 0px #ffffff, inset -1px -1px 0px 0px #808080;"
"  padding: 10px;"
"  border-radius: 2px;"
"}\n"
"\n"
"/* Buttons: flat metal gradient with beveled edges */\n"
"button {"
"  background-image: linear-gradient(to bottom, #e7e7e7, #c9c9c9);"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #ffffff, inset -1px -1px 0 0 #808080;"
"  padding: 4px 10px;"
"}\n"
"button:hover {"
"  background-image: linear-gradient(to bottom, #f0f0f0, #d5d5d5);"
"}\n"
"button:active {"
"  background-image: linear-gradient(to bottom, #c9c9c9, #b8b8b8);"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"}\n"
"button:disabled {"
"  opacity: 0.6;"
"}\n"
"\n"
"/* Text entries: sunken look */\n"
"entry, spinbutton, combobox, combobox entry {"
"  background: #ffffff;"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"  padding: 4px;"
"}\n"
"entry:focus {"
"  border-color: #2a5db0;"
"}\n"
"\n"
"/* Notebook (tabs) */\n"
"notebook > header {"
"  background-image: linear-gradient(to bottom, #d2d2d2, #c2c2c2);"
"  border-bottom: 1px solid #7a7a7a;"
"}\n"
"notebook tab {"
"  background-image: linear-gradient(to bottom, #e2e2e2, #cdcdcd);"
"  border: 1px solid #7a7a7a;"
"  margin: 2px;"
"  padding: 4px 8px;"
"}\n"
"notebook tab:checked {"
"  background-image: linear-gradient(to bottom, #f2f2f2, #dbdbdb);"
"}\n"
"\n"
"/* TreeView header */\n"
"treeview header button {"
"  background-image: linear-gradient(to bottom, #e3e3e3, #cfcfcf);"
"  border: 1px solid #7a7a7a;"
"  padding: 4px 6px;"
"  font-weight: bold;"
"}\n"
"\n"
"/* Progressbar */\n"
"progressbar trough {"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"}\n"
"progressbar progress {"
"  background-image: linear-gradient(to bottom, #9fbef7, #5582d0);"
"}\n"
"\n"
"/* Scrollbars a bit chunkier for desktop */\n"
"scrollbar slider {"
"  background-image: linear-gradient(to bottom, #dcdcdc, #c4c4c4);"
"  border: 1px solid #7a7a7a;"
"}\n";

static void apply_metal_theme(void) {
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, METAL_CSS, -1, NULL);
#if GTK_CHECK_VERSION(3,0,0)
    GdkScreen *scr = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(scr,
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
#else
    /* Fallback: add to default context (older GTKs) */
    GtkWidgetPath *path = gtk_widget_path_new(); (void)path;
#endif
    g_object_unref(prov);
}

/* Small helper to wrap a child in a beveled metal panel */
static GtkWidget* metal_wrap(GtkWidget *child, const char *name_opt) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "metal-panel");
    if (name_opt && *name_opt) gtk_widget_set_name(box, name_opt);
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0);
    return box;
}
/* ==== end Metal theme ===================================================== */


typedef struct {
    /* left controls */
    GtkComboBoxText *ds_combo;
    GtkComboBoxText *model_combo;
    GtkComboBoxText *algo_combo;
    GtkSpinButton   *train_spn;
    GtkSpinButton   *val_spn;
    GtkSpinButton   *test_spn;
    GtkEntry        *x_feat;
    GtkEntry        *y_feat;
    GtkCheckButton  *scale_chk;
    GtkCheckButton  *impute_chk;

    GtkButton       *btn_train;
    GtkButton       *btn_validate;
    GtkButton       *btn_test;
    GtkButton       *btn_refresh_ds;

    /* right notebook */
    GtkNotebook     *right_nb;
    GtkTreeView     *preview_view;
    GtkImage        *plot_img;
    GtkTextView     *logs_view;
    GtkListStore    *preview_store;

    /* footer */
    GtkProgressBar  *progress;
    GtkLabel        *status;

    /* stack */
    GtkStack        *stack;

    /* login widgets */
    GtkEntry    *login_email;
    GtkEntry    *login_pass;
    GtkButton   *btn_login;
    gboolean     logged_in;

} EnvCtx;

typedef struct {
    GtkWidget *login_window;
    GtkWidget *main_window;
    GtkEntry *email_entry;
    GtkEntry *pass_entry;
    GtkLabel *status_label;
    EnvCtx *env_ctx;
} LoginData;

/* ---- forward decls ---- */
static gboolean refresh_datasets_tab(gpointer data);
static void     on_destroy(GtkWidget *w, gpointer data);
static void     on_entry_changed(GtkEditable *e, gpointer data);
static void     on_refresh_clicked(GtkButton *b, gpointer data);
static GtkWidget* make_refresh_button(TabCtx *ctx);
static TabCtx*  add_datasets_tab(GtkNotebook *nb);
static void     add_environment_tab(GtkNotebook *nb, EnvCtx *env);
static void on_login_clicked(GtkButton *b, gpointer user_data);
static void on_toggle_password_visibility(GtkButton *button, gpointer user_data);
static gboolean on_login_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
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
    /* Outer with padding */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 6);

    /* Top bar: refresh + filter */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filter (matches any column)...");
    GtkWidget *top_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    /* Context FIRST */
    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry   = GTK_ENTRY(entry);
    GtkWidget *tree = gtk_tree_view_new();
    ctx->view    = GTK_TREE_VIEW(tree);
    ctx->store   = NULL;
    ctx->dump_cmd= g_strdup_printf("DUMP %s\n", TABLE_NAME);
    ctx->n_cols  = 0;
    ctx->id_col  = -1;

    GtkWidget *btn = make_refresh_button(ctx);
    gtk_box_pack_start(GTK_BOX(top_left), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), top_left, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), entry, TRUE, TRUE, 0);

    /* Tree in scroller */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    /* Wrap both in panels for depth */
    GtkWidget *top_panel   = metal_wrap(top,   "ds-top-bar");
    GtkWidget *table_panel = metal_wrap(scroll,"ds-table");

    gtk_box_pack_start(GTK_BOX(outer), top_panel,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), table_panel, TRUE,  TRUE,  0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, outer, lbl);

    /* signals */
    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), ctx);

    gtk_widget_show_all(outer);

    /* initial fill + periodic refresh */
    refresh_datasets_tab_core(ctx, TRUE);
    g_timeout_add(2000, refresh_timer_cb, ctx);

    return ctx;
}


/* Environment */


// Environment | Datasets

// Pre-processing | Regress | View

// ┌──────┬───────────┬────┐
// │ File │ Import DB |    |
// └──────┴───────────┴────┘



//communicate with Python backend
//| open/create dropdown menu "trainees"
//| attempt to fetch already existing models (python/models/*)
//| |   if doesn't exist, form an empty list of "trainees"
//| |   if exists, retrieve and form a list of available "trainees"
//| 

/* --- small helpers --- */

static void log_append(EnvCtx *ctx, const char *line) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(ctx->logs_view);
    GtkTextIter end; gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, line, -1);
    gtk_text_buffer_insert(buf, &end, "\n", -1);
}

static void set_status(EnvCtx *ctx, const char *s) {
    gtk_label_set_text(ctx->status, s);
}

/* render PNG (base64) string into GtkImage */
static void set_plot_png_b64(EnvCtx *ctx, const char *b64) {
    if (!b64 || !*b64) { gtk_image_clear(ctx->plot_img); return; }
    gsize out_len = 0;
    guchar *bytes = g_base64_decode(b64, &out_len);
    if (!bytes) { gtk_image_clear(ctx->plot_img); return; }
    GInputStream *ms = g_memory_input_stream_new_from_data(bytes, out_len, g_free);
    GdkPixbuf *pix = gdk_pixbuf_new_from_stream(ms, NULL, NULL);
    g_object_unref(ms);
    if (pix) {
        gtk_image_set_from_pixbuf(ctx->plot_img, pix);
        g_object_unref(pix);
    } else {
        gtk_image_clear(ctx->plot_img);
    }
}

/* Minimal CSV->TreeView (head + up to 200 rows) */
static void preview_from_csv_into(GtkTreeView *view, const char *csv) {
    if (!csv) return;
    char *dup = g_strdup(csv), *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    if (!line) { g_free(dup); return; }

    // parse header
    GPtrArray *hdr = csv_parse_line_all(line);
    if (!hdr || hdr->len == 0) { if (hdr) g_ptr_array_free(hdr, TRUE); g_free(dup); return; }

    // model
    gint n = (gint)hdr->len;
    GType *types = g_new0(GType, n); for (gint i=0;i<n;++i) types[i]=G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(n, types);
    g_free(types);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
    g_object_unref(store);

    // columns
    GList *cols = gtk_tree_view_get_columns(view);
    for (GList *l=cols;l;l=l->next) gtk_tree_view_remove_column(view, GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(cols);
    for (guint i=0;i<hdr->len;++i) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes((const char*)hdr->pdata[i], r, "text", i, NULL);
        gtk_tree_view_append_column(view, c);
    }

    // rows
    int count = 0;
    while ((line = strtok_r(NULL, "\n", &save)) && count < 200) {
        GPtrArray *row = csv_parse_line_all(line);
        if (row && row->len >= hdr->len) {
            GtkTreeIter it; gtk_list_store_append(store, &it);
            for (guint i=0;i<hdr->len;++i) {
                gtk_list_store_set(store, &it, (gint)i, (const char*)row->pdata[i], -1);
            }
        }
        if (row) g_ptr_array_free(row, TRUE);
        ++count;
    }
    g_ptr_array_free(hdr, TRUE);
    g_free(dup);
}

/* --- backend roundtrips (UI-safe) --------------------------------------- */

static char* req(const char *cmd) {
    size_t n=0; return backend_request(cmd, &n); /* returns malloc'ed; free with g_free */
}

static void on_refresh_datasets(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ctx = user;
    char *js = req("LIST_DATASETS\n");
    if (!js) { set_status(ctx, "No datasets"); return; }
    // naive parse: look for "datasets":["a","b",...]
    gtk_combo_box_text_remove_all(ctx->ds_combo);
    const char *p = strstr(js, "\"datasets\"");
    if (p) {
        const char *lb = strchr(p, '['), *rb = lb ? strchr(lb, ']') : NULL;
        if (lb && rb && rb>lb) {
            char *arr = g_strndup(lb+1, (gsize)(rb-lb-1));
            char *tok = strtok(arr, ",");
            while (tok) {
                while (*tok==' '||*tok=='\"') ++tok;
                char *end = tok + strlen(tok);
                while (end>tok && (end[-1]=='\"'||end[-1]==' ')) *--end = 0;
                if (*tok) gtk_combo_box_text_append_text(ctx->ds_combo, tok);
                tok = strtok(NULL, ",");
            }
            g_free(arr);
        }
    }
    g_free(js);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), 0);
    set_status(ctx, "Datasets refreshed");
}

static void on_load_dataset(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ctx = user;
    const char *ds = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    if (!ds) { set_status(ctx, "Select a dataset"); return; }
    GString *cmd = g_string_new("LOAD_DATASET ");
    g_string_append(cmd, ds);
    g_string_append_c(cmd, '\n');
    char *js = req(cmd->str);
    g_string_free(cmd, TRUE);
    if (js) { set_status(ctx, "Dataset loaded"); g_free(js); }
    char *csv = req("PREVIEW 200\n");
    if (csv) { preview_from_csv_into(ctx->preview_view, csv); g_free(csv); }
}

static void on_plot_update(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ctx = user;
    const char *x = gtk_entry_get_text(ctx->x_feat);
    const char *y = gtk_entry_get_text(ctx->y_feat);
    if (!*x || !*y) { set_status(ctx, "Enter X and Y"); return; }
    GString *j = g_string_new("{\"type\":\"scatter\",\"x\":\"");
    g_string_append(j, x); g_string_append(j, "\",\"y\":\"");
    g_string_append(j, y); g_string_append(j, "\"}");
    GString *cmd = g_string_new("PLOT ");
    g_string_append(cmd, j->str); g_string_append_c(cmd, '\n');
    char *resp = req(cmd->str);
    g_string_free(cmd, TRUE); g_string_free(j, TRUE);
    if (!resp) { set_status(ctx, "Plot error"); return; }
    const char *k = strstr(resp, "\"data\":\"");
    if (k) {
        k += 8; const char *end = strchr(k, '"');
        if (end) { char *b64 = g_strndup(k, (gsize)(end-k)); set_plot_png_b64(ctx, b64); g_free(b64); }
    }
    g_free(resp);
    gtk_notebook_set_current_page(ctx->right_nb, 1); // Plot tab
}

static void on_train_clicked(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ctx = user;
    const char *ds = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    const char *algo = gtk_combo_box_text_get_active_text(ctx->algo_combo);
    if (!ds || !algo) { set_status(ctx, "Pick dataset & algo"); return; }
    int tr = (int)gtk_spin_button_get_value(ctx->train_spn);
    int va = (int)gtk_spin_button_get_value(ctx->val_spn);
    int te = (int)gtk_spin_button_get_value(ctx->test_spn);

    GString *cfg = g_string_new("{\"algo\":\"");
    g_string_append(cfg, algo);
    g_string_append(cfg, "\",\"dataset\":\"");
    g_string_append(cfg, ds);
    g_string_append(cfg, "\",\"split\":{\"train\":");
    g_string_append_printf(cfg, "%.2f,\"val\":%.2f,\"test\":%.2f",
                           tr/100.0, va/100.0, te/100.0);
    g_string_append(cfg, "},\"preproc\":{");
    g_string_append(cfg, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->impute_chk)) ? "\"impute\":\"median\"" : "\"impute\":null");
    g_string_append(cfg, ",");
    g_string_append(cfg, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->scale_chk)) ? "\"scale\":\"standard\"" : "\"scale\":null");
    g_string_append(cfg, "}}");

    GString *cmd = g_string_new("TRAIN ");
    g_string_append(cmd, cfg->str);
    g_string_append_c(cmd, '\n');

    set_status(ctx, "Training…");
    gtk_progress_bar_pulse(ctx->progress);

    char *resp = req(cmd->str);  // simple: wait for final JSON; for streaming progress, switch to incremental read
    g_string_free(cfg, TRUE); g_string_free(cmd, TRUE);

    if (resp) {
        log_append(ctx, resp);
        // naive metrics scrape
        const char *r2 = strstr(resp, "\"r2\":");
        if (r2) set_status(ctx, "Training complete");
        else set_status(ctx, "Training done (no metrics?)");
        g_free(resp);
    } else {
        set_status(ctx, "Training failed");
    }
    gtk_progress_bar_set_fraction(ctx->progress, 0.0);
}



// Adicione esta função para criar a janela de login
// Adicione/REPLACE a função create_login_window:
static GtkWidget* create_login_window(LoginData *login_data) {
    /* Window */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AI For Dummies – Login");
    gtk_window_set_default_size(GTK_WINDOW(window), 460, 340);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    /* Outer center box so content stays centered while scalable */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), outer);
    gtk_widget_set_halign(outer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(outer, GTK_ALIGN_CENTER);

    /* Form container with metal depth */
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(form), 16);
    GtkWidget *panel = metal_wrap(form, "login-form");
    gtk_box_pack_start(GTK_BOX(outer), panel, FALSE, FALSE, 0);

    /* Title */
    GtkWidget *title = gtk_label_new("AI For Dummies");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_size_new(14 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(form), title, FALSE, FALSE, 0);

    /* Grid for labels/inputs */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(form), grid, TRUE, TRUE, 0);

    /* Username */
    GtkWidget *lbl_user = gtk_label_new("Usuário:");
    gtk_widget_set_halign(lbl_user, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl_user, 0, 0, 1, 1);

    login_data->email_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(login_data->email_entry, "Digite seu usuário");
    gtk_widget_set_hexpand(GTK_WIDGET(login_data->email_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(login_data->email_entry), 1, 0, 2, 1);

    /* Password with reveal */
    GtkWidget *lbl_pass = gtk_label_new("Senha:");
    gtk_widget_set_halign(lbl_pass, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl_pass, 0, 1, 1, 1);

    login_data->pass_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(login_data->pass_entry, "Digite sua senha");
    gtk_entry_set_visibility(login_data->pass_entry, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(login_data->pass_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(login_data->pass_entry), 1, 1, 1, 1);

    GtkWidget *reveal_btn = gtk_toggle_button_new_with_label("Mostrar");
    gtk_widget_set_tooltip_text(reveal_btn, "Mostrar/ocultar senha");
    g_signal_connect(reveal_btn, "toggled",
        G_CALLBACK(on_toggle_password_visibility), login_data->pass_entry);
    gtk_grid_attach(GTK_GRID(grid), reveal_btn, 2, 1, 1, 1);

    /* Actions row */
    GtkWidget *btn_login = gtk_button_new_with_label("Entrar");
    gtk_widget_set_hexpand(btn_login, TRUE);
    gtk_grid_attach(GTK_GRID(grid), btn_login, 1, 2, 2, 1);

    /* Status + footer */
    login_data->status_label = GTK_LABEL(gtk_label_new(""));
    gtk_box_pack_start(GTK_BOX(form), GTK_WIDGET(login_data->status_label), FALSE, FALSE, 0);

    GtkWidget *footer = gtk_label_new("Enter para entrar • ESC para sair");
    gtk_box_pack_start(GTK_BOX(form), footer, FALSE, FALSE, 0);

    /* Signals */
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login_clicked), login_data);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_login_key_press), login_data);

    return window;
}


// Função para alternar a visibilidade da senha
static void on_toggle_password_visibility(GtkButton *button, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    gboolean visible = gtk_entry_get_visibility(entry);
    gtk_entry_set_visibility(entry, !visible);
    
    // Alterar o ícone baseado no estado
    const gchar *icon_name = visible ? "view-conceal-symbolic" : "view-reveal-symbolic";
    GtkWidget *new_icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(button), new_icon);
}

// Função para lidar com eventos de teclado
static gboolean on_login_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    LoginData *login_data = (LoginData*)user_data;
    
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(login_data->login_window);
        return TRUE;
    }
    
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        on_login_clicked(NULL, login_data);
        return TRUE;
    }
    
    return FALSE;
}

static void on_login_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    LoginData *login_data = (LoginData*)user_data;
    const char *email = gtk_entry_get_text(login_data->email_entry);
    const char *pass = gtk_entry_get_text(login_data->pass_entry);

    if (!email || !*email || !pass || !*pass) {
        gtk_label_set_text(login_data->status_label, "Digite email e senha");
        return;
    }

    /* Montar JSON de autenticação */
    GString *j = g_string_new("{\"email\":\"");
    g_string_append(j, email);
    g_string_append(j, "\",\"password\":\"");
    g_string_append(j, pass);
    g_string_append(j, "\"}");
    
    GString *cmd = g_string_new("LOGIN ");
    g_string_append(cmd, j->str);
    g_string_append_c(cmd, '\n');

    char *resp = req(cmd->str);
    g_string_free(j, TRUE);
    g_string_free(cmd, TRUE);

    if (!resp) {
        gtk_label_set_text(login_data->status_label, "Sem resposta do backend");
        return;
    }

    /* Processar resposta */
    if (strncmp(resp, "OK ", 3) == 0) {
        /* Login bem-sucedido - extrair informações do usuário */
        const char *payload = resp + 3;
        char *dup = g_strdup(payload);
        char *tok = strtok(dup, "|");
        const char *uid = tok ? tok : "";
        tok = strtok(NULL, "|");
        const char *name = tok ? tok : "";

        /* Fechar janela de login e mostrar janela principal */
        gtk_widget_destroy(login_data->login_window);
        gtk_widget_show_all(login_data->main_window);

        /* Habilitar controles na interface principal */
        EnvCtx *ctx = login_data->env_ctx;
        if (ctx) {
            set_status(ctx, name && *name ? name : "Logado com sucesso");
            ctx->logged_in = TRUE;

            /* Habilitar componentes da interface */
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->ds_combo), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->model_combo), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->algo_combo), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->btn_refresh_ds), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->btn_train), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->btn_validate), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->btn_test), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->x_feat), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->y_feat), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->scale_chk), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->impute_chk), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->train_spn), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->val_spn), TRUE);
            gtk_widget_set_sensitive(GTK_WIDGET(ctx->test_spn), TRUE);
        }

        g_free(dup);
    } else {
        /* Exibir mensagem de erro */
        const char *error_msg = strstr(resp, "ERR ") ? resp + 4 : resp;
        gtk_label_set_text(login_data->status_label, error_msg);
    }

    g_free(resp);
}

/* Build the Environment tab */
/* Build the Environment tab */
void add_environment_tab(GtkNotebook *nb, EnvCtx *ctx) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* stack switcher (top) */
    ctx->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(ctx->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), ctx->stack);
    gtk_box_pack_start(GTK_BOX(outer), switcher, FALSE, FALSE, 0);

    /* main split */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), paned, TRUE, TRUE, 0);

    /* LEFT: controls inside a Metal panel */
    GtkWidget *left_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    /* dataset row + refresh + load */
    GtkWidget *ds_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->ds_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    ctx->btn_refresh_ds = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
    GtkWidget *btn_load = gtk_button_new_with_label("Load");
    gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->ds_combo), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->btn_refresh_ds), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ds_row), btn_load, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), ds_row, FALSE, FALSE, 0);

    /* trainees row */
    GtkWidget *tr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->model_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(ctx->model_combo, "(new)");
    gtk_box_pack_start(GTK_BOX(tr_row), gtk_label_new("Trainee:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tr_row), GTK_WIDGET(ctx->model_combo), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), tr_row, FALSE, FALSE, 0);

    /* algo + params */
    GtkWidget *algo_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->algo_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(ctx->algo_combo, "linreg");
    gtk_combo_box_text_append_text(ctx->algo_combo, "ridge");
    gtk_combo_box_text_append_text(ctx->algo_combo, "lasso");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->algo_combo), 0);
    gtk_box_pack_start(GTK_BOX(algo_row), gtk_label_new("Regressor:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(algo_row), GTK_WIDGET(ctx->algo_combo), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), algo_row, FALSE, FALSE, 0);

    /* split sliders */
    GtkAdjustment *adj1 = gtk_adjustment_new(70, 1, 98, 1, 5, 0);
    GtkAdjustment *adj2 = gtk_adjustment_new(15, 1, 98, 1, 5, 0);
    GtkAdjustment *adj3 = gtk_adjustment_new(15, 1, 98, 1, 5, 0);
    ctx->train_spn = GTK_SPIN_BUTTON(gtk_spin_button_new(adj1, 1, 0));
    ctx->val_spn   = GTK_SPIN_BUTTON(gtk_spin_button_new(adj2, 1, 0));
    ctx->test_spn  = GTK_SPIN_BUTTON(gtk_spin_button_new(adj3, 1, 0));
    GtkWidget *split_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(split_row), gtk_label_new("Split (T/V/S %)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(split_row), GTK_WIDGET(ctx->train_spn), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(split_row), GTK_WIDGET(ctx->val_spn),   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(split_row), GTK_WIDGET(ctx->test_spn),  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), split_row, FALSE, FALSE, 0);

    /* features + preproc */
    GtkWidget *xy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->x_feat = GTK_ENTRY(gtk_entry_new());
    ctx->y_feat = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(ctx->x_feat, "X feature (e.g., sepal_length)");
    gtk_entry_set_placeholder_text(ctx->y_feat, "Y target (e.g., price)");
    gtk_box_pack_start(GTK_BOX(xy_row), GTK_WIDGET(ctx->x_feat), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(xy_row), GTK_WIDGET(ctx->y_feat), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), xy_row, FALSE, FALSE, 0);

    ctx->impute_chk = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Impute (median)"));
    ctx->scale_chk  = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Scale (standard)"));
    gtk_box_pack_start(GTK_BOX(left_content), GTK_WIDGET(ctx->impute_chk), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), GTK_WIDGET(ctx->scale_chk),  FALSE, FALSE, 0);

    /* action buttons */
    GtkWidget *act_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->btn_train    = GTK_BUTTON(gtk_button_new_with_label("Train"));
    GtkWidget *btn_plot = gtk_button_new_with_label("Plot");
    ctx->btn_validate = GTK_BUTTON(gtk_button_new_with_label("Validate"));
    ctx->btn_test     = GTK_BUTTON(gtk_button_new_with_label("Test"));
    gtk_box_pack_start(GTK_BOX(act_row), GTK_WIDGET(ctx->btn_train), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), btn_plot, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), GTK_WIDGET(ctx->btn_validate), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), GTK_WIDGET(ctx->btn_test), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), act_row, FALSE, FALSE, 0);

    /* Wrap left in metal panel and pack into paned */
    GtkWidget *left_panel = metal_wrap(left_content, "env-left-panel");
    gtk_paned_pack1(GTK_PANED(paned), left_panel, FALSE, FALSE);

    /* RIGHT: notebook content; the theme styles tabs */
    GtkWidget *right_nb = gtk_notebook_new();
    ctx->right_nb = GTK_NOTEBOOK(right_nb);

    /* Preview */
    GtkWidget *tv = gtk_tree_view_new();
    ctx->preview_view = GTK_TREE_VIEW(tv);
    GtkWidget *scroller_preview = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroller_preview), tv);
    gtk_notebook_append_page(ctx->right_nb, scroller_preview, gtk_label_new("Preview"));

    /* Logs */
    ctx->logs_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(ctx->logs_view, FALSE);
    GtkWidget *scroller_logs = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroller_logs), GTK_WIDGET(ctx->logs_view));
    gtk_notebook_append_page(ctx->right_nb, scroller_logs, gtk_label_new("Logs"));

    /* Plot */
    ctx->plot_img = GTK_IMAGE(gtk_image_new());
    GtkWidget *plot_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(plot_box), GTK_WIDGET(ctx->plot_img), TRUE, TRUE, 0);
    gtk_notebook_append_page(ctx->right_nb, plot_box, gtk_label_new("Plot"));

    /* Metrics */
    GtkWidget *metrics_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(metrics_view), FALSE);
    gtk_notebook_append_page(ctx->right_nb, metrics_view, gtk_label_new("Metrics"));

    /* Wrap right in panel for consistent depth */
    GtkWidget *right_panel = metal_wrap(right_nb, "env-right-panel");
    gtk_paned_pack2(GTK_PANED(paned), right_panel, TRUE, FALSE);

    /* footer */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    ctx->status   = GTK_LABEL(gtk_label_new("Idle"));
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->progress), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->status), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), metal_wrap(footer, "env-footer"), FALSE, FALSE, 0);

    /* stack pages */
    gtk_stack_add_titled(ctx->stack, paned, "preproc",   "Pre-processing");
    gtk_stack_add_titled(ctx->stack, paned, "regressor", "Regression");
    gtk_stack_add_titled(ctx->stack, paned, "view",      "View");

    /* signals */
    g_signal_connect(ctx->btn_refresh_ds, "clicked", G_CALLBACK(on_refresh_datasets), ctx);
    g_signal_connect(btn_load,            "clicked", G_CALLBACK(on_load_dataset),     ctx);
    g_signal_connect(btn_plot,            "clicked", G_CALLBACK(on_plot_update),      ctx);
    g_signal_connect(ctx->btn_train,      "clicked", G_CALLBACK(on_train_clicked),    ctx);

    /* mount into notebook */
    GtkWidget *tab_lbl = gtk_label_new("Environment");
    gtk_notebook_append_page(nb, outer, tab_lbl);
    gtk_widget_show_all(outer);

    /* initial population */
    on_refresh_datasets(GTK_BUTTON(ctx->btn_refresh_ds), ctx);
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
// Modifique a função main para usar o sistema de login
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    apply_metal_theme();

    /* EDIT credentials (or load from env) */
    const WCHAR *DB   = L"AIForDummies";
    const WCHAR *USER = L"root";
    const WCHAR *PASS = L"hhzpIxzAuLBiDBPLELofDfZzDklgpVHD";
    const WCHAR *HOST = L"hopper.proxy.rlwy.net";
    const int    PORT = 39703;

#ifdef _WIN32
    if (!backend_start(DB, USER, PASS, HOST, PORT)) {
        /* Show a message and keep GUI up; manual refresh will keep failing until fixed */
        MessageBoxW(NULL,
            L"Failed to start the Python backend.\n\n"
            L"Check python\\venv and DB connectivity.",
            L"AI-for-dummies", MB_OK | MB_ICONERROR);
    }
#endif

    GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "AI For Dummies");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1100, 700);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(main_window), notebook);

    // Criar contexto da interface
    EnvCtx *env_ctx = g_new0(EnvCtx, 1);
    add_environment_tab(GTK_NOTEBOOK(notebook), env_ctx);
    add_datasets_tab(GTK_NOTEBOOK(notebook));

    // Configurar sistema de login
    LoginData login_data = {0};
    login_data.main_window = main_window;
    login_data.env_ctx = env_ctx;
    
    // Criar e mostrar janela de login
    login_data.login_window = create_login_window(&login_data);
    gtk_widget_show_all(login_data.login_window);

    // Inicialmente desabilitar controles da interface principal
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->ds_combo), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->model_combo), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->algo_combo), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->btn_refresh_ds), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->btn_train), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->btn_validate), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->btn_test), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->x_feat), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->y_feat), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->scale_chk), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->impute_chk), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->train_spn), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->val_spn), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(env_ctx->test_spn), FALSE);

    gtk_main();
    return 0;
}
