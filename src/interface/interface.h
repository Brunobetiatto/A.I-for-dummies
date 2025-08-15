#include <gtk/gtk.h>
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "communicator.h"

/* --- forward declarations for helpers defined later in this header --- */
static GPtrArray*   csv_parse_line_all(const char *line);
static void         clear_treeview_columns(GtkTreeView *view);
static GtkListStore* create_model_with_n_cols(gint n_visible_cols);
static void         build_columns_from_headers(GtkTreeView *view, GPtrArray *headers);
static gboolean     row_matches_filter(GPtrArray *fields, const char *needle);
/* --------------------------------------------------------------------- */


/* ===== Globals you already had (add the view) ===== */
GtkListStore *store_global = NULL;  // model
GtkEntry     *entry_global = NULL;  // filter entry
GtkTreeView  *view_global  = NULL;  // <-- set this once after you create your TreeView

/* ===== Helpers ===== */

/* ===== New helpers to build Python commands (Windows; capture STDERR) ===== */

static WCHAR* build_cmd_dump_table(const WCHAR *db, const WCHAR *table,
                                   const WCHAR *user, const WCHAR *pass) {
    WCHAR cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);

    // python "<cwd>\python\connectors\db_connector.py" --user ... --password ... --db ... --table ... 
    size_t cap = 2048;
    WCHAR *cmd = (WCHAR*)calloc(cap, sizeof(WCHAR));
    _snwprintf(cmd, cap-1,
        L"python \"%ls\\python\\connectors\\db_connector.py\" "
        L"--user \"%ls\" --password \"%ls\" --db \"%ls\" --table \"%ls\" ",
        cwd, user, pass, db, table);
    return cmd;
}

static WCHAR* build_cmd_list_tables(const WCHAR *db, const WCHAR *user, const WCHAR *pass) {
    WCHAR cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    size_t cap = 1024;
    WCHAR *cmd = (WCHAR*)calloc(cap, sizeof(WCHAR));
    _snwprintf(cmd, cap-1,
        L"python \"%ls\\python\\connectors\\db_connector.py\" "
        L"--user \"%ls\" --password \"%ls\" --db \"%ls\" --list-tables ",
        cwd, user, pass, db);
    return cmd;
}

/* ===== New public entry point: run an arbitrary Python command into a view ===== */
/* Reuses your existing CSV parser, build_columns_from_headers, etc. */
gboolean update_table_cmd(const WCHAR *cmdw, GtkEntry *entry, GtkTreeView *view) {
    // Execute and capture UTF-16 (stdout + stderr)
    WCHAR *saida = run_cmd_out(L"", (WCHAR*)cmdw);

    if (!saida) return TRUE;

    // Convert to UTF-8 once
    char *saida_utf8 = g_utf16_to_utf8((gunichar2*)saida, -1, NULL, NULL, NULL);
    free(saida);

    const char *filter_text = entry ? gtk_entry_get_text(entry) : NULL;
    char *saveptr = NULL;
    char *line = strtok_r(saida_utf8, "\n", &saveptr);

    // Find header (skip banners)
    GPtrArray *headers = NULL;
    while (line) {
        if (line[0] != 'I' && line[0] != '-' && line[0] != '\0') {
            headers = csv_parse_line_all(line);
            break;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    if (!headers || headers->len == 0) {
        // Optional: surface error in GUI by creating a single "error" column
        clear_treeview_columns(view);
        GtkListStore *empty = gtk_list_store_new(1, G_TYPE_STRING);
        gtk_tree_view_set_model(view, GTK_TREE_MODEL(empty));
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes("error", r, "text", 0, NULL);
        gtk_tree_view_append_column(view, c);
        if (headers) g_ptr_array_free(headers, TRUE);
        g_free(saida_utf8);
        return TRUE;
    }

    // Rebuild model + columns
    GtkListStore *new_store = create_model_with_n_cols((gint)headers->len);
    build_columns_from_headers(view, headers);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(new_store));
    if (store_global) g_object_unref(store_global);
    store_global = new_store;

    const gint bg_idx = (gint)headers->len;
    gint row_count = 0;

    // Rows
    line = strtok_r(NULL, "\n", &saveptr);
    while (line) {
        if (line[0] != 'I' && line[0] != '-') {
            GPtrArray *fields = csv_parse_line_all(line);
            if (fields && fields->len >= headers->len) {
                if (row_matches_filter(fields, filter_text)) {
                    GtkTreeIter it;
                    gtk_list_store_append(store_global, &it);
                    for (guint i = 0; i < headers->len; ++i) {
                        const char *val = (const char*)fields->pdata[i];
                        gtk_list_store_set(store_global, &it, (gint)i, val ? val : "", -1);
                    }
                    const char *bg = (row_count % 2 == 0) ? "#f0f0f0" : "#ffffff";
                    gtk_list_store_set(store_global, &it, bg_idx, bg, -1);
                    row_count++;
                }
            }
            if (fields) g_ptr_array_free(fields, TRUE);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    g_ptr_array_free(headers, TRUE);
    g_free(saida_utf8);
    return TRUE;
}

static void trim_ws(char *s) {
    if (!s) return;
    char *a = s, *b = s + strlen(s);
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    size_t n = (size_t)(b - a);
    if (a != s) memmove(s, a, n);
    s[n] = '\0';
}

/* Parse a CSV line into an array of freshly-allocated strings (fields).
 * Handles quotes, commas, doubled quotes "" -> ".
 * Returns a GPtrArray* with g_free as free-func for each element.
 */
static GPtrArray* csv_parse_line_all(const char *line) {
    if (!line) return NULL;
    GPtrArray *fields = g_ptr_array_new_with_free_func(g_free);

    const char *p = line;
    gboolean in_q = FALSE;
    GString *buf = g_string_new(NULL);

    while (*p && *p != '\r' && *p != '\n') {
        char c = *p++;

        if (in_q) {
            if (c == '"') {
                if (*p == '"') {  // escaped quote
                    g_string_append_c(buf, '"');
                    p++;
                } else {
                    in_q = FALSE;
                }
            } else {
                g_string_append_c(buf, c);
            }
        } else {
            if (c == '"') {
                in_q = TRUE;
            } else if (c == ',') {
                char *field = g_strdup(buf->str);
                trim_ws(field);
                g_ptr_array_add(fields, field);
                g_string_set_size(buf, 0);
            } else {
                g_string_append_c(buf, c);
            }
        }
    }

    // last field
    {
        char *field = g_strdup(buf->str);
        trim_ws(field);
        g_ptr_array_add(fields, field);
    }
    g_string_free(buf, TRUE);
    return fields;
}

/* Remove all columns from the view */
static void clear_treeview_columns(GtkTreeView *view) {
    GList *cols = gtk_tree_view_get_columns(view);
    for (GList *l = cols; l; l = l->next) {
        gtk_tree_view_remove_column(view, GTK_TREE_VIEW_COLUMN(l->data));
    }
    g_list_free(cols);
}

/* Create model (all columns as strings) with an extra hidden "bg" color column at the end. */
static GtkListStore* create_model_with_n_cols(gint n_visible_cols) {
    gint total = n_visible_cols + 1; // +1 for bg color
    GType *types = g_new0(GType, total);
    for (gint i = 0; i < total; ++i) types[i] = G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(total, types);
    g_free(types);
    return store;
}

/* Build columns from header names. Each column uses "text" = i and "cell-background" = bg_col_index. */
static void build_columns_from_headers(GtkTreeView *view, GPtrArray *headers) {
    clear_treeview_columns(view);
    const gint n = (gint)headers->len;
    const gint bg_idx = n; // last hidden column

    for (gint i = 0; i < n; ++i) {
        const char *title = (const char *)headers->pdata[i];
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(
            title, r,
            "text", i,
            "cell-background", bg_idx,
            NULL
        );
        gtk_tree_view_append_column(view, col);
    }
}

/* Case-sensitive substring search across all fields (like your original strstr).
 * Return TRUE if any field contains 'needle'.
 */
static gboolean row_matches_filter(GPtrArray *fields, const char *needle) {
    if (!needle || !*needle) return TRUE;
    for (guint i = 0; i < fields->len; ++i) {
        const char *f = (const char*)fields->pdata[i];
        if (f && strstr(f, needle)) return TRUE;
    }
    return FALSE;
}

/* ===== Main updater (dynamic CSV) ===== */

gboolean update_table(gpointer user_data) {
    // Fallback single-source refresh (optional)
    WCHAR *cmd = build_cmd_dump_table(L"commtratta", L"usuarios", L"root", L"pepsi@123");
    gboolean ok = update_table_cmd(cmd, entry_global, view_global);
    free(cmd);
    return ok;
}

