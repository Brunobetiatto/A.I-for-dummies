#include "../css/css.h"
#include "debug_window.h"
#include "../backend/communicator.h"
#include "context.h"
#include <pango/pangocairo.h>
#include "profile.h"

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef DATASETS_H
#define DATASETS_H

static const GtkTargetEntry DND_TARGETS[] = {
    { "text/plain", 0, 1 },
};

#ifndef gdk_atom_equal
#  define gdk_atom_equal(a,b) ((a) == (b))
#endif /* DATASETS_H */


typedef struct {
    GtkStack   *stack;
    GtkListBox *cards;

    GtkWidget  *title_label; /* top title in details */

    GtkLabel   *lbl_user;
    GtkLabel   *lbl_size;
    GtkLabel   *lbl_rows;
    GtkLabel   *lbl_link;
    GtkLabel   *lbl_desc;

    GtkWidget  *user_event;
} DatasetsUI;

static void trim_spaces(char *s) {
    if (!s) return;
    char *p = s, *q = s;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
    while (*q) *p++ = *q++;
    *p = 0;
    for (int i=(int)strlen(s)-1; i>=0 && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n'); --i) s[i]=0;
}

/* If text looks like AA (e.g. "diagnosisdiagnosis"), collapse to A. */
static char* collapse_double(const char *txt) {
    if (!txt) return g_strdup("");
    size_t n = strlen(txt);
    if (n >= 2 && (n % 2) == 0) {
        size_t h = n/2;
        if (strncmp(txt, txt + h, h) == 0) return g_strndup(txt, h);
    }
    return g_strdup(txt);
}

/* Single place to get our private DnD atom (lazy init, thread-safe in GTK main thread) */
static GdkAtom get_col_atom(void) {
    static GdkAtom a = GDK_NONE;
    if (a == GDK_NONE) a = gdk_atom_intern_static_string("AIFD/COLUMN-NAME");
    return a;
}

/* Best-effort string cleaner: trims, strips quotes and collapses doubled/overlapped payloads */
static char* normalize_drag_text_strict(const char *raw) {
    if (!raw) return g_strdup("");
    char *tmp = g_strdup(raw);
    trim_spaces(tmp);

    size_t n = strlen(tmp);
    if (n >= 2 && tmp[0] == '"' && tmp[n-1] == '"') { tmp[n-1] = 0; memmove(tmp, tmp+1, n-1); }

    /* exact double e.g. "foofoo" */
    if (n % 2 == 0 && n > 0 && strncmp(tmp, tmp + n/2, n/2) == 0) {
        char *o = g_strndup(tmp, n/2); g_free(tmp); trim_spaces(o); return o;
    }

    /* try to collapse common overlapped dup like "diagnosisgnosis" */
    for (size_t k = 1; k < n; ++k) {
        size_t keep = n - k;
        if (keep > 0 && strncmp(tmp, tmp + k, keep) == 0) {
            char *o = g_strndup(tmp, keep);
            g_free(tmp);
            trim_spaces(o);
            return o;
        }
    }

    trim_spaces(tmp);
    return tmp;
}

/* Canonicalize a comma-separated list (trim + de-dup) */
static char* canonicalize_token_list(const char *txt) {
    GPtrArray *uniq = g_ptr_array_new();                 /* no free func */
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (txt && *txt) {
        gchar **parts = g_strsplit(txt, ",", -1);
        for (int i = 0; parts && parts[i]; ++i) {
            char *t = g_strdup(parts[i]); trim_spaces(t);
            if (*t) {
                if (!g_hash_table_contains(seen, t)) {
                    g_hash_table_add(seen, g_strdup(t));
                    g_ptr_array_add(uniq, t);            /* keep; freed below */
                } else {
                    g_free(t);
                }
            } else {
                g_free(t);
            }
        }
        g_strfreev(parts);
    }

    GString *out = g_string_new(NULL);
    for (guint i=0; i<uniq->len; ++i) {
        if (i) g_string_append(out, ", ");
        g_string_append(out, (char*)uniq->pdata[i]);
    }

    for (guint i=0; i<uniq->len; ++i) g_free(uniq->pdata[i]);
    g_ptr_array_free(uniq, TRUE);
    g_hash_table_destroy(seen);

    return g_string_free(out, FALSE);
}

/* Return TRUE if entry already contains token (comma-separated list) */
static gboolean entry_contains_token(GtkEntry *e, const char *name) {
    if (!e || !name || !*name) return FALSE;
    const char *txt = gtk_entry_get_text(e);
    if (!txt) return FALSE;

    gboolean found = FALSE;
    gchar **parts = g_strsplit(txt, ",", -1);
    for (int i=0; parts && parts[i]; ++i) {
        char *t = g_strdup(parts[i]); trim_spaces(t);
        if (*t && g_strcmp0(t, name) == 0) { found = TRUE; g_free(t); break; }
        g_free(t);
    }
    g_strfreev(parts);
    return found;
}

static GdkPixbuf* render_drag_badge(const char *text) {
    const int pad_x = 12, pad_y = 6, radius = 10;
    cairo_surface_t *dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(dummy);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Segoe UI 10");
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text ? text : "", -1);
    int tw=0, th=0; pango_layout_get_pixel_size(layout, &tw, &th);
    int W = tw + pad_x*2, H = th + pad_y*2;
    g_object_unref(layout); cairo_destroy(cr); cairo_surface_destroy(dummy);

    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cr = cairo_create(sf);
    /* rounded bg */
    double x=0, y=0, w=W, h=H, r=radius;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r, r, -M_PI/2, 0);
    cairo_arc(cr, x+w-r, y+h-r, r, 0, M_PI/2);
    cairo_arc(cr, x+r,   y+h-r, r, M_PI/2, M_PI);
    cairo_arc(cr, x+r,   y+r,   r, M_PI, 3*M_PI/2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.20, 0.36, 0.80, 0.92);
    cairo_fill(cr);

    /* text */
    layout = pango_cairo_create_layout(cr);
    fd = pango_font_description_from_string("Segoe UI 10");
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text ? text : "", -1);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, pad_x, pad_y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    GdkPixbuf *pix = gdk_pixbuf_get_from_surface(sf, 0, 0, W, H);
    cairo_destroy(cr); cairo_surface_destroy(sf);
    return pix;
}

static void on_header_drag_begin(GtkWidget *widget, GdkDragContext *ctx, gpointer user_data) {
    (void)ctx;
    const char *colname = (const char*)user_data;
    GdkPixbuf *icon = render_drag_badge(colname ? colname : "column");
    if (icon) {
        gtk_drag_source_set_icon_pixbuf(widget, icon);
        g_object_unref(icon);
    }
    GtkStyleContext *sc = gtk_widget_get_style_context(widget);
    gtk_style_context_add_class(sc, "dragging");
}
static void on_header_drag_end(GtkWidget *widget, GdkDragContext *ctx, gpointer user_data) {
    (void)ctx; (void)user_data;
    GtkStyleContext *sc = gtk_widget_get_style_context(widget);
    gtk_style_context_remove_class(sc, "dragging");
}

static void on_header_drag_data_get(GtkWidget *widget,
                                    GdkDragContext *ctx,
                                    GtkSelectionData *sel,
                                    guint info,
                                    guint time_,
                                    gpointer user_data)
{
    (void)widget; (void)ctx; (void)info; (void)time_;
    const char *colname = (const char*)user_data;
    if (!colname) colname = "";

    /* Use the requested target; prefer our private atom, else UTF-8 text */
    GdkAtom want = gtk_selection_data_get_target(sel);
    if (gdk_atom_equal(want, get_col_atom())) {
        const guchar *bytes = (const guchar*)colname;
        gtk_selection_data_set(sel, want, 8 /* 8-bit */, bytes, (gint)strlen(colname));
    } else {
        gtk_selection_data_set_text(sel, colname, -1);
    }
}

static gboolean skip_duplicate_drop(GtkWidget *dest, const char *txt, guint time_) {
    const char *last_txt = (const char*)g_object_get_data(G_OBJECT(dest), "last-drop-txt");
    guint last_t = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dest), "last-drop-time"));
    if (last_txt && g_strcmp0(last_txt, txt) == 0 && last_t == time_) return TRUE;
    g_object_set_data_full(G_OBJECT(dest), "last-drop-txt", g_strdup(txt), g_free);
    g_object_set_data(G_OBJECT(dest), "last-drop-time", GUINT_TO_POINTER(time_));
    return FALSE;
}

/* Hover visuals */
static gboolean on_drop_drag_motion(GtkWidget *w, GdkDragContext *c, gint x, gint y, guint time_, gpointer u) {
    (void)x;(void)y;(void)u;
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_style_context_add_class(sc, "drop-hover");
    /* Tell the source we accept a COPY here */
    gdk_drag_status(c, GDK_ACTION_COPY, time_);
    return TRUE;
}

static void on_drop_drag_leave(GtkWidget *w, GdkDragContext *c, guint time_, gpointer u) {
    (void)c;(void)time_;(void)u;
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_style_context_remove_class(sc, "drop-hover");
}

static gboolean on_x_drag_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, guint time_, gpointer user_data) {
    (void)x; (void)y; (void)user_data;
    /* NULL => GTK uses the widget’s target list + what the source advertised */
    GdkAtom target = gtk_drag_dest_find_target(w, ctx, NULL);
    if (target == GDK_NONE) { gtk_drag_finish(ctx, FALSE, FALSE, time_); return TRUE; }
    gtk_drag_get_data(w, ctx, target, time_);
    return TRUE;
}
static gboolean on_y_drag_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, guint time_, gpointer user_data) {
    (void)x; (void)y; (void)user_data;
    GdkAtom target = gtk_drag_dest_find_target(w, ctx, NULL);
    if (target == GDK_NONE) { gtk_drag_finish(ctx, FALSE, FALSE, time_); return TRUE; }
    gtk_drag_get_data(w, ctx, target, time_);
    return TRUE;
}


static char* extract_payload(GtkSelectionData *sel) {
    if (gdk_atom_equal(gtk_selection_data_get_data_type(sel), get_col_atom()) ||
        gdk_atom_equal(gtk_selection_data_get_target(sel),    get_col_atom())) {
        const guchar *data = gtk_selection_data_get_data(sel);
        gint len = gtk_selection_data_get_length(sel);
        return (len > 0 && data) ? g_strndup((const char*)data, len) : g_strdup("");
    }
    return gtk_selection_data_get_text(sel); /* UTF-8 */
}

static void on_x_drag_data_received(GtkWidget *w, GdkDragContext *ctx,
                                    gint x, gint y, GtkSelectionData *sel,
                                    guint info, guint time_, gpointer user_data)
{
    (void)x; (void)y; (void)info; (void)user_data;
    gboolean success = FALSE;

    if (GTK_IS_ENTRY(w)) {
        gchar *payload = extract_payload(sel);
        if (payload) {
            char *clean = normalize_drag_text_strict(payload);
            g_free(payload);

            if (!skip_duplicate_drop(w, clean, time_)) {
                const char *before = gtk_entry_get_text(GTK_ENTRY(w));
                GString *tmp = g_string_new(before ? before : "");
                if (tmp->len == 0) g_string_assign(tmp, clean);
                else               g_string_append_printf(tmp, ", %s", clean);

                /* trim+dedup the comma list */
                char *canon = canonicalize_token_list(tmp->str);
                gtk_entry_set_text(GTK_ENTRY(w), canon);
                g_free(canon);
                g_string_free(tmp, TRUE);
            }
            g_free(clean);
            success = TRUE;
        }
    }

    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_style_context_remove_class(sc, "drop-hover");
    gtk_drag_finish(ctx, success, FALSE, time_);
}

static void on_y_drag_data_received(GtkWidget *w, GdkDragContext *ctx,
                                    gint x, gint y, GtkSelectionData *sel,
                                    guint info, guint time_, gpointer user_data)
{
    (void)x; (void)y; (void)info; (void)user_data;
    gboolean success = FALSE;

    if (GTK_IS_ENTRY(w)) {
        gchar *payload = extract_payload(sel);
        if (payload) {
            char *clean = normalize_drag_text_strict(payload);
            g_free(payload);

            if (!skip_duplicate_drop(w, clean, time_)) {
                gtk_entry_set_text(GTK_ENTRY(w), clean ? clean : "");
            }
            g_free(clean);
            success = TRUE;
        }
    }

    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_style_context_remove_class(sc, "drop-hover");
    gtk_drag_finish(ctx, success, FALSE, time_);
}
gboolean on_user_clicked(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
    gpointer p = g_object_get_data(G_OBJECT(w), "uploader-id");
    if (!p) return TRUE;
    int uploader_id = GPOINTER_TO_INT(p);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "GET_USER_JSON %d", uploader_id);

    // converter comando para WCHAR
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
    WCHAR *wcmd = malloc(wide_len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wide_len);

    // chama communicator (run_api_command) — recebe WCHAR* alocado
    WCHAR *wres = run_api_command(wcmd);
    free(wcmd);
    if (!wres) return TRUE;

    // converte resposta WCHAR* -> UTF-8
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wres, -1, NULL, 0, NULL, NULL);
    char *resp_utf8 = malloc(utf8_size);
    WideCharToMultiByte(CP_UTF8, 0, wres, -1, resp_utf8, utf8_size, NULL, NULL);
    free(wres); // liberar o WCHAR* retornado por run_api_command

    if (resp_utf8) {
        // resp_utf8 contém o JSON cru retornado pela API (ex.: {"status":"OK","user":{...}})
        extern void profile_create_and_show_from_json(const char *user_json, GtkWindow *parent);

        // pega a janela pai (opcional)
        GtkWindow *parent_win = GTK_WINDOW(gtk_widget_get_ancestor(w, GTK_TYPE_WINDOW));
        profile_create_and_show_from_json(resp_utf8, parent_win);

        free(resp_utf8);
    }

    return TRUE;
}


static void enable_drop_on_env_entries(EnvCtx *ctx) {
    if (!ctx) return;

    GtkTargetList *tl = gtk_target_list_new(NULL, 0);
    gtk_target_list_add(tl, get_col_atom(), 0, 99);   /* prefer our atom */
    gtk_target_list_add_text_targets(tl, 0);          /* fallback(s): text/plain, utf8 */

    if (ctx->x_feat && GTK_IS_ENTRY(ctx->x_feat)) {
        GtkWidget *w = GTK_WIDGET(ctx->x_feat);
        gtk_drag_dest_set(w, 0 /* no defaults */, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_dest_set_target_list(w, tl);
        g_signal_connect(w, "drag-motion",        G_CALLBACK(on_drop_drag_motion),       NULL);
        g_signal_connect(w, "drag-leave",         G_CALLBACK(on_drop_drag_leave),        NULL);
        g_signal_connect(w, "drag-drop",          G_CALLBACK(on_x_drag_drop),            ctx);
        g_signal_connect(w, "drag-data-received", G_CALLBACK(on_x_drag_data_received),   ctx);
        gtk_widget_set_tooltip_text(w, "Drag column headers here to add X features");
    }

    if (ctx->y_feat && GTK_IS_ENTRY(ctx->y_feat)) {
        GtkWidget *w = GTK_WIDGET(ctx->y_feat);
        gtk_drag_dest_set(w, 0 /* no defaults */, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_dest_set_target_list(w, tl);
        g_signal_connect(w, "drag-motion",        G_CALLBACK(on_drop_drag_motion),       NULL);
        g_signal_connect(w, "drag-leave",         G_CALLBACK(on_drop_drag_leave),        NULL);
        g_signal_connect(w, "drag-drop",          G_CALLBACK(on_y_drag_drop),            ctx);
        g_signal_connect(w, "drag-data-received", G_CALLBACK(on_y_drag_data_received),   ctx);
        gtk_widget_set_tooltip_text(w, "Drag a column header here to set Y (target)");
    }

    gtk_target_list_unref(tl);
}

static void wire_treeview_headers_for_dnd(EnvCtx *ctx, GtkTreeView *tv) {
    (void)ctx;
    if (!tv || !GTK_IS_TREE_VIEW(tv)) return;

    gtk_tree_view_set_rules_hint(tv, TRUE);

    GList *cols = gtk_tree_view_get_columns(tv);
    for (GList *l = cols; l; l = l->next) {
        GtkTreeViewColumn *col = GTK_TREE_VIEW_COLUMN(l->data);
        const gchar *title = gtk_tree_view_column_get_title(col);
        if (!title) title = "col";

        GtkWidget *eb  = gtk_event_box_new();
        GtkWidget *lab = gtk_label_new(title);
        gtk_container_add(GTK_CONTAINER(eb), lab);
        gtk_widget_set_tooltip_text(eb, "Drag to X or Y");
        gtk_widget_show_all(eb);
        gtk_tree_view_column_set_widget(col, eb);

        GtkTargetList *tl = gtk_target_list_new(NULL, 0);
        gtk_target_list_add(tl, get_col_atom(), 0, 99);
        gtk_target_list_add_text_targets(tl, 0);

        gtk_drag_source_set(eb, GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_source_set_target_list(eb, tl);
        gtk_target_list_unref(tl);

        gchar *name_copy = g_strdup(title);
        g_signal_connect(eb, "drag-data-get", G_CALLBACK(on_header_drag_data_get), name_copy);
        g_signal_connect(eb, "drag-begin",    G_CALLBACK(on_header_drag_begin),    name_copy);
        g_signal_connect(eb, "drag-end",      G_CALLBACK(on_header_drag_end),      name_copy);
        g_object_set_data_full(G_OBJECT(eb), "colname-free", name_copy, g_free);
    }
    g_list_free(cols);
}


static char* norm_key(const char *s) {
    if (!s) return g_strdup("");
    size_t n = strlen(s);
    char *o = g_malloc(n + 1);
    for (size_t i=0; i<n; ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        o[i] = c;
    }
    o[n] = 0;
    return o;
}

/* Make a small K/V map (char*->char*) for one dataset row */
static GHashTable* make_row_meta(cJSON *columns, cJSON *row) {
    GHashTable *ht = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(row)) return ht;

    int ncols = cJSON_GetArraySize(columns);
    for (int i=0; i<ncols; ++i) {
        cJSON *col = cJSON_GetArrayItem(columns, i);
        cJSON *val = cJSON_GetArrayItem(row, i);

        const char *k = (cJSON_IsString(col) && col->valuestring) ? col->valuestring : "";
        char *nk = norm_key(k);

        char buf[1024] = "";
        if (cJSON_IsString(val) && val->valuestring) {
            snprintf(buf, sizeof(buf), "%s", val->valuestring);
        } else if (cJSON_IsNumber(val)) {
            snprintf(buf, sizeof(buf), "%g", val->valuedouble);
        } else if (cJSON_IsBool(val)) {
            snprintf(buf, sizeof(buf), "%s", cJSON_IsTrue(val) ? "1" : "0");
        } else {
            buf[0] = 0;
        }
        g_hash_table_insert(ht, nk, g_strdup(buf));  /* nk freed by table */
    }
    return ht;
}

/* Pull a value by any of the aliases; returns "" if none found (never NULL). */
static const char* meta_get_any(GHashTable *ht, const char **aliases, int n_alias) {
    for (int i=0; i<n_alias; ++i) {
        const char *k = aliases[i];
        char *nk = norm_key(k);
        const char *v = (const char*)g_hash_table_lookup(ht, nk);
        g_free(nk);
        if (v && *v) return v;
    }
    return "";
}

/* Compact human-readable size if given as bytes (string). Fallback: original. */
static char* pretty_size(const char *sbytes) {
    if (!sbytes || !*sbytes) return g_strdup("");
    char *end = NULL;
    double v = g_ascii_strtod(sbytes, &end);
    if (end == sbytes) return g_strdup(sbytes);
    const char *units[] = {"B","KB","MB","GB","TB"};
    int ui = 0;
    while (v >= 1024.0 && ui < 4) { v /= 1024.0; ui++; }
    return g_strdup_printf("%.1f %s", v, units[ui]);
}

/* Card click context and handler — PURE C */
typedef struct {
    GtkStack   *stack;        /* the list/details stack */
    GtkWidget  *title_label;  /* label in details header */
    GtkTreeView *details_tv;  /* details table (if you need later) */
} CardClickCtx;

/* Simple UI bundle we store on the search entry via g_object_set_data */


/* BACK button -> return to list page */
static void on_back_to_list_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkStack *stack = GTK_STACK(user_data);
    if (GTK_IS_STACK(stack)) gtk_stack_set_visible_child_name(stack, "list");
}

/* Click on a card: fill details from its metadata and switch to details page */
static gboolean on_card_button(GtkWidget *widget, GdkEventButton *ev, gpointer user_data) {
    if (!widget || !user_data) return FALSE;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 1) return FALSE;

    DatasetsUI *dui = (DatasetsUI*)user_data;

    /* Read metadata map attached to the card and any precomputed title */
    GHashTable *meta = (GHashTable*)g_object_get_data(G_OBJECT(widget), "dataset-meta");
    const char *title_mk = (const char*)g_object_get_data(G_OBJECT(widget), "card-title");

    /* Set header/title */
    if (title_mk && GTK_IS_LABEL(dui->title_label)) {
        gtk_label_set_markup(GTK_LABEL(dui->title_label), title_mk);
    }

    /* Aliases for typical fields (used only when meta is present) */
    const char *A_TITLE[] = {"nome","name","title"};
    const char *A_USER[]  = {"usuario","user","owner","author","autor","usuario_nome","enviado_por_nome"};
    const char *A_SIZE[]  = {"size","tamanho","bytes"};
    const char *A_ROWS[]  = {"rows","linhas","nrows","amostras","samples"};
    const char *A_LINK[]  = {"link","url","source","path"};
    const char *A_DESC[]  = {"descricao","description","desc","enviado_por_descricao"};

    /* Prefer uploader info stored directly on the eventbox (set by refresh_datasets_cb) */
    const char *ev_uploader_name  = (const char*)g_object_get_data(G_OBJECT(widget), "uploader-name");
    const char *ev_uploader_email = (const char*)g_object_get_data(G_OBJECT(widget), "uploader-email");
    gpointer     ev_uploader_id_p = g_object_get_data(G_OBJECT(widget), "uploader-id");
    gpointer     ev_dataset_id_p  = g_object_get_data(G_OBJECT(widget), "dataset-id");

    /* Fallbacks: read from meta if not present as dedicated fields */
    const char *v_user = ev_uploader_name;
    if (!v_user || !*v_user) {
        v_user = meta ? meta_get_any(meta, A_USER, G_N_ELEMENTS(A_USER)) : NULL;
    }

    const char *v_email = ev_uploader_email;
    if ((!v_email || !*v_email) && meta) {
        const char *A_EMAIL[] = {"usuario_email","enviado_por_email","email","user_email","owner_email"};
        v_email = meta_get_any(meta, A_EMAIL, G_N_ELEMENTS(A_EMAIL));
    }

    const char *v_size = meta ? meta_get_any(meta, A_SIZE, G_N_ELEMENTS(A_SIZE)) : NULL;
    const char *v_rows = meta ? meta_get_any(meta, A_ROWS, G_N_ELEMENTS(A_ROWS)) : NULL;
    const char *v_link = meta ? meta_get_any(meta, A_LINK, G_N_ELEMENTS(A_LINK)) : NULL;
    const char *v_desc = meta ? meta_get_any(meta, A_DESC, G_N_ELEMENTS(A_DESC)) : NULL;

    /* Prepare size pretty string (pretty_size returns allocated string or NULL) */
    char *size_pp = NULL;
    if (v_size && *v_size) {
        size_pp = pretty_size(v_size);
    }

    /* Compose display for user: prefer name, otherwise show email, otherwise dash */
    char user_display[256] = "—";
    if (v_user && *v_user) {
        g_snprintf(user_display, sizeof(user_display), "%s", v_user);
    }

    /* Set labels (use "—" when nothing) */
    gtk_label_set_text(dui->lbl_user, user_display);
    gtk_label_set_text(dui->lbl_rows, (v_rows && *v_rows) ? v_rows : "—");
    gtk_label_set_text(dui->lbl_desc, (v_desc && *v_desc) ? v_desc : "—");

    if (size_pp && *size_pp) {
        gtk_label_set_text(dui->lbl_size, size_pp);
    } else {
        gtk_label_set_text(dui->lbl_size, (v_size && *v_size) ? v_size : "—");
    }

    /* Link label: make clickable markup if present, otherwise dash */
    if (v_link && *v_link) {
        gchar *mk = g_markup_printf_escaped("<a href=\"%s\">%s</a>", v_link, v_link);
        gtk_label_set_markup(GTK_LABEL(dui->lbl_link), mk);
        g_free(mk);
    } else {
        gtk_label_set_text(dui->lbl_link, "—");
    }

    if (size_pp) g_free(size_pp);

    /* Show details page */
    if (GTK_IS_STACK(dui->stack)) {
        gtk_stack_set_visible_child_name(dui->stack, "details");
    }

    /* --- Configure the user-event widget so a click on the uploader opens the user detail --- */
    if (dui->user_event) {
        /* set uploader-id (if present on eventbox use that; otherwise try to parse from meta) */
        gint uid = 0;
        gboolean have_uid = FALSE;

        if (ev_uploader_id_p) {
            uid = GPOINTER_TO_INT(ev_uploader_id_p);
            if (uid > 0) have_uid = TRUE;
        } else if (meta) {
            const char *A_USER_ID[] = {
                "usuario_idusuario", "usuarioid", "user_id", "uploader_id",
                "idusuario", "id", "owner_id", NULL
            };
            const char *uid_str = meta_get_any(meta, A_USER_ID, 7);
            if (uid_str && *uid_str) {
                uid = atoi(uid_str);
                if (uid > 0) have_uid = TRUE;
            }
        }

        /* set or clear uploader-id on dui->user_event */
        if (have_uid) {
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-id", GINT_TO_POINTER(uid));
            gtk_widget_set_sensitive(dui->user_event, TRUE);
        } else {
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-id", NULL);
            gtk_widget_set_sensitive(dui->user_event, FALSE);
        }

        /* Move uploader-name/email into dui->user_event — free previous stored strings to avoid leaks. */
        const char *prev_name = (const char*)g_object_get_data(G_OBJECT(dui->user_event), "uploader-name");
       
        if (v_user && *v_user) g_object_set_data_full(G_OBJECT(dui->user_event), "uploader-name", g_strdup(v_user), g_free);
        else g_object_set_data(G_OBJECT(dui->user_event), "uploader-name", NULL);

        const char *prev_email = (const char*)g_object_get_data(G_OBJECT(dui->user_event), "uploader-email");
    
        if (v_email && *v_email) g_object_set_data_full(G_OBJECT(dui->user_event), "uploader-email", g_strdup(v_email), g_free);
        else g_object_set_data(G_OBJECT(dui->user_event), "uploader-email", NULL);

        /* Optionally carry dataset-id to dui->user_event for context (clear old if any) */
        g_object_set_data(G_OBJECT(dui->user_event), "dataset-id", NULL);

        if (ev_dataset_id_p) {
            g_object_set_data(G_OBJECT(dui->user_event), "dataset-id", ev_dataset_id_p);
        } else if (meta) {
            const char *A_DATASET_ID[] = {"iddataset","id","dataset_id", NULL};
            const char *did_str = meta_get_any(meta, A_DATASET_ID, 3);
            if (did_str && *did_str) {
                int did = atoi(did_str);
                if (did > 0) g_object_set_data(G_OBJECT(dui->user_event), "dataset-id", GINT_TO_POINTER(did));
            }
        }
    }

    debug_log("on_card_button: clicked dataset, uploader='%s' email='%s' uid=%d",
              ev_uploader_name ? ev_uploader_name : "(none)",
              ev_uploader_email ? ev_uploader_email : "(none)",
              ev_uploader_id_p ? GPOINTER_TO_INT(ev_uploader_id_p) : 0);

    return TRUE;
}

static void refresh_datasets_cb(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    TabCtx *ctx = (TabCtx*)user_data;
    if (!ctx) return;

    /* Pull UI bundle we stashed on the search entry */
    DatasetsUI *dui = (DatasetsUI*)g_object_get_data(G_OBJECT(ctx->entry), "datasets-ui");
    if (!dui || !GTK_IS_LIST_BOX(dui->cards)) return;

    /* Query API */
    char *resp = NULL;
    if (!api_dump_table("dataset", &resp) || !resp) { if (resp) free(resp); return; }

    cJSON *root = cJSON_Parse(resp); free(resp);
    if (!root) return;
    debug_log("Datasets JSON: %s", cJSON_Print(root));

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) { cJSON_Delete(root); return; }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data    = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) { cJSON_Delete(root); return; }

    /* Clear card list safely */
    {
        GList *rows = gtk_container_get_children(GTK_CONTAINER(dui->cards));
        for (GList *l = rows; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
        g_list_free(rows);
    }

    /* Determine common column indexes (name/desc/size/url/date/id) + uploader fields */
    int ncols = cJSON_GetArraySize(columns);
    int idx_nome = -1, idx_desc = -1, idx_size = -1, idx_url = -1, idx_dt = -1, idx_id = -1;
    int idx_uploader_id = -1, idx_uploader_name = -1, idx_uploader_email = -1;

    for (int i = 0; i < ncols; ++i) {
        cJSON *col = cJSON_GetArrayItem(columns, i);
        if (!cJSON_IsString(col) || !col->valuestring) continue;
        const char *nm = col->valuestring;

        if (g_ascii_strcasecmp(nm, "nome") == 0 || g_ascii_strcasecmp(nm, "name") == 0 || g_ascii_strcasecmp(nm, "title") == 0) idx_nome = i;
        if (g_ascii_strcasecmp(nm, "descricao") == 0 || g_ascii_strcasecmp(nm, "description") == 0 || g_ascii_strcasecmp(nm, "desc") == 0) idx_desc = i;
        if (g_ascii_strcasecmp(nm, "tamanho") == 0 || g_ascii_strcasecmp(nm, "size") == 0 || g_ascii_strcasecmp(nm, "bytes") == 0) idx_size = i;
        if (g_ascii_strcasecmp(nm, "url") == 0 || g_ascii_strcasecmp(nm, "link") == 0 || g_ascii_strcasecmp(nm, "source") == 0 || g_ascii_strcasecmp(nm, "path") == 0) idx_url = i;
        if (g_ascii_strcasecmp(nm, "datacadastro") == 0 || g_ascii_strcasecmp(nm, "dataCadastro") == 0 || g_ascii_strcasecmp(nm, "created_at") == 0 || g_ascii_strcasecmp(nm, "date") == 0) idx_dt = i;
        if (g_ascii_strcasecmp(nm, "iddataset") == 0 || g_ascii_strcasecmp(nm, "id") == 0) idx_id = i;

        /* uploader id/name/email detection (várias aliases) */
        if (g_ascii_strcasecmp(nm, "usuario_idusuario") == 0 || g_ascii_strcasecmp(nm, "usuarioid") == 0 ||
            g_ascii_strcasecmp(nm, "user_id") == 0 || g_ascii_strcasecmp(nm, "uploader_id") == 0 ||
            g_ascii_strcasecmp(nm, "idusuario") == 0 || g_ascii_strcasecmp(nm, "owner_id") == 0 ||
            g_ascii_strcasecmp(nm, "userid") == 0 || g_ascii_strcasecmp(nm, "user") == 0) {
            idx_uploader_id = i;
        }

        if (g_ascii_strcasecmp(nm, "usuario_nome") == 0 || g_ascii_strcasecmp(nm, "enviado_por_nome") == 0 ||
            g_ascii_strcasecmp(nm, "uploader_name") == 0 || g_ascii_strcasecmp(nm, "user_name") == 0 ||
            g_ascii_strcasecmp(nm, "owner_name") == 0) {
            idx_uploader_name = i;
        }

        if (g_ascii_strcasecmp(nm, "usuario_email") == 0 || g_ascii_strcasecmp(nm, "enviado_por_email") == 0 ||
            g_ascii_strcasecmp(nm, "email") == 0 || g_ascii_strcasecmp(nm, "user_email") == 0 ||
            g_ascii_strcasecmp(nm, "owner_email") == 0) {
            idx_uploader_email = i;
        }
    }

    /* Build a “card” (row) for each dataset */
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;

        /* Title / description text for the card face */
        char title_buf[256] = "(dataset)";
        char desc_buf[512] = "";

        if (idx_nome >= 0) {
            cJSON *cell = cJSON_GetArrayItem(row, idx_nome);
            if (cJSON_IsString(cell) && cell->valuestring) {
                g_strlcpy(title_buf, cell->valuestring, sizeof(title_buf));
            }
        }
        if (idx_desc >= 0) {
            cJSON *cell = cJSON_GetArrayItem(row, idx_desc);
            if (cJSON_IsString(cell) && cell->valuestring) {
                g_strlcpy(desc_buf, cell->valuestring, sizeof(desc_buf));
            }
        }

        /* Size pretty (optional) */
        char size_right[64] = "";
        if (idx_size >= 0) {
            cJSON *cell = cJSON_GetArrayItem(row, idx_size);
            if (cJSON_IsString(cell) && cell->valuestring) {
                char *pp = pretty_size(cell->valuestring);
                if (pp) { g_strlcpy(size_right, pp, sizeof(size_right)); g_free(pp); }
            } else if (cJSON_IsNumber(cell)) {
                char tmp[64];
                g_snprintf(tmp, sizeof(tmp), "%g", cell->valuedouble);
                char *pp = pretty_size(tmp);
                if (pp) { g_strlcpy(size_right, pp, sizeof(size_right)); g_free(pp); }
            }
        }

        /* Build UI for the card */
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *left   = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *labt   = gtk_label_new(NULL);
        GtkWidget *labd   = gtk_label_new(desc_buf);

        gchar *markup = g_markup_printf_escaped("<b>%s</b>", title_buf);
        gtk_label_set_markup(GTK_LABEL(labt), markup);
        gtk_label_set_xalign(GTK_LABEL(labt), 0.0);
        gtk_label_set_xalign(GTK_LABEL(labd), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(labd), PANGO_ELLIPSIZE_END);

        gtk_box_pack_start(GTK_BOX(left), labt, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(left), labd, FALSE, FALSE, 0);

        GtkWidget *size = gtk_label_new(size_right);
        gtk_label_set_xalign(GTK_LABEL(size), 1.0);

        gtk_box_pack_start(GTK_BOX(rowbox), left, TRUE, TRUE, 0);
        gtk_box_pack_end  (GTK_BOX(rowbox), size, FALSE, FALSE, 0);

        /* EventBox wrapper for click */
        GtkWidget *ev = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(ev), rowbox);

        /* Build meta hashtable and attach */
        GHashTable *meta = make_row_meta(columns, row);
        g_object_set_data_full(G_OBJECT(ev), "dataset-meta", meta, (GDestroyNotify)g_hash_table_destroy);

        /* Attach title markup for details header (free with g_free) */
        g_object_set_data_full(G_OBJECT(ev), "card-title", g_strdup(markup), g_free);

        /* Extract uploader-id (from row if present) and store on the eventbox for convenience */
        if (idx_uploader_id >= 0) {
            cJSON *uid_cell = cJSON_GetArrayItem(row, idx_uploader_id);
            if (cJSON_IsNumber(uid_cell)) {
                g_object_set_data(G_OBJECT(ev), "uploader-id", GINT_TO_POINTER((gint)uid_cell->valueint));
            } else if (cJSON_IsString(uid_cell) && uid_cell->valuestring) {
                int uid = atoi(uid_cell->valuestring);
                g_object_set_data(G_OBJECT(ev), "uploader-id", GINT_TO_POINTER(uid));
            } else {
                g_object_set_data(G_OBJECT(ev), "uploader-id", NULL);
            }
        } else {
            /* fallback: try to read from meta with common keys */
            const char *uploader_id_str = meta_get_any(meta,
                (const char*[]){"usuario_idusuario","usuarioid","idusuario","user_id","uploader_id","owner_id",NULL}, 6);
            if (uploader_id_str && *uploader_id_str) {
                g_object_set_data(G_OBJECT(ev), "uploader-id", GINT_TO_POINTER(atoi(uploader_id_str)));
            } else {
                g_object_set_data(G_OBJECT(ev), "uploader-id", NULL);
            }
        }

        /* Also store uploader name/email (if present) on the card for quick access.
           Use set_data_full so g_free() is called when widget is destroyed to avoid leaks. */
        if (idx_uploader_name >= 0) {
            cJSON *name_cell = cJSON_GetArrayItem(row, idx_uploader_name);
            if (cJSON_IsString(name_cell) && name_cell->valuestring && name_cell->valuestring[0]) {
                g_object_set_data_full(G_OBJECT(ev), "uploader-name", g_strdup(name_cell->valuestring), g_free);
            } else {
                g_object_set_data(G_OBJECT(ev), "uploader-name", NULL);
            }
        } else {
            const char *name_from_meta = meta_get_any(meta, (const char*[]){"usuario_nome","enviado_por_nome","user_name",NULL}, 3);
            if (name_from_meta && *name_from_meta) g_object_set_data_full(G_OBJECT(ev), "uploader-name", g_strdup(name_from_meta), g_free);
            else g_object_set_data(G_OBJECT(ev), "uploader-name", NULL);
        }

        if (idx_uploader_email >= 0) {
            cJSON *email_cell = cJSON_GetArrayItem(row, idx_uploader_email);
            if (cJSON_IsString(email_cell) && email_cell->valuestring && email_cell->valuestring[0]) {
                g_object_set_data_full(G_OBJECT(ev), "uploader-email", g_strdup(email_cell->valuestring), g_free);
            } else {
                g_object_set_data(G_OBJECT(ev), "uploader-email", NULL);
            }
        } else {
            const char *email_from_meta = meta_get_any(meta, (const char*[]){"usuario_email","enviado_por_email","email",NULL}, 3);
            if (email_from_meta && *email_from_meta) g_object_set_data_full(G_OBJECT(ev), "uploader-email", g_strdup(email_from_meta), g_free);
            else g_object_set_data(G_OBJECT(ev), "uploader-email", NULL);
        }

        /* dataset id (if present) */
        if (idx_id >= 0) {
            cJSON *idcell = cJSON_GetArrayItem(row, idx_id);
            if (cJSON_IsNumber(idcell)) {
                g_object_set_data(G_OBJECT(ev), "dataset-id", GINT_TO_POINTER(idcell->valueint));
            } else if (cJSON_IsString(idcell) && idcell->valuestring) {
                int did = atoi(idcell->valuestring);
                g_object_set_data(G_OBJECT(ev), "dataset-id", GINT_TO_POINTER(did));
            } else {
                g_object_set_data(G_OBJECT(ev), "dataset-id", NULL);
            }
        } else {
            g_object_set_data(G_OBJECT(ev), "dataset-id", NULL);
        }

        /* Insert into list and connect click handler */
        gtk_list_box_insert(GTK_LIST_BOX(dui->cards), ev, -1);
        g_signal_connect(ev, "button-press-event", G_CALLBACK(on_card_button), dui);

        /* free local markup (we duplicated for storage), original markup var must be freed */
        g_free(markup);
    }

    /* Show list page after refresh */
    gtk_stack_set_visible_child_name(dui->stack, "list");

    /* Ensure cards are visible */
    gtk_widget_show_all(GTK_WIDGET(dui->cards));

    cJSON_Delete(root);
}


static void populate_ds_combo_from_api(EnvCtx *ctx) {
    if (!ctx || !ctx->ds_combo) return;

    char *resp = NULL;
    if (!api_dump_table("dataset", &resp) || !resp) {
        fprintf(stderr, "populate_ds_combo_from_api: api_dump_table failed\n");
        if (resp) free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) { fprintf(stderr, "populate_ds_combo_from_api: invalid JSON\n"); return; }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        fprintf(stderr, "API error: %s\n", cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        cJSON_Delete(root);
        return;
    }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) { cJSON_Delete(root); return; }

    /* find 'nome' column index */
    int nome_idx = -1;
    int ncols = cJSON_GetArraySize(columns);
    for (int i = 0; i < ncols; ++i) {
        cJSON *col = cJSON_GetArrayItem(columns, i);
        if (cJSON_IsString(col) && strcmp(col->valuestring, "nome") == 0) { nome_idx = i; break; }
    }

    /* clear and append */
    gtk_combo_box_text_remove_all(ctx->ds_combo);
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;
        cJSON *cell = (nome_idx >= 0) ? cJSON_GetArrayItem(row, nome_idx) : NULL;
        if (cell && cJSON_IsString(cell)) gtk_combo_box_text_append_text(ctx->ds_combo, cell->valuestring);
    }

    cJSON_Delete(root);
}

static void on_refresh_datasets(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    populate_ds_combo_from_api(ctx);
    gtk_label_set_text(GTK_LABEL(ctx->status), "Datasets refreshed");
}

static void on_load_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    const char *sel = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    if (!sel) {
        gtk_label_set_text(GTK_LABEL(ctx->status), "No dataset selected");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "Loaded dataset: %s", sel);
    gtk_label_set_text(GTK_LABEL(ctx->status), buf);
    g_free((gpointer)sel);
}

static TabCtx* add_datasets_tab(GtkNotebook *nb) {
    const char *DATASETS_CSS = parse_CSS_file("datasets.css");

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 6);

    /* Top bar: search + refresh */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Search datasets…");
    gtk_box_pack_start(GTK_BOX(top), entry, TRUE, TRUE, 0);

    GtkWidget *btn_refresh = gtk_button_new();
    gtk_box_pack_start(GTK_BOX(top), btn_refresh, FALSE, FALSE, 0);
    {
        GError *err = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("./assets/refresh_button.png", &err);
        if (pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 24, 24, GDK_INTERP_BILINEAR);
            if (scaled) {
                gtk_button_set_image(GTK_BUTTON(btn_refresh), gtk_image_new_from_pixbuf(scaled));
                g_object_unref(scaled);
            }
            g_object_unref(pixbuf);
        }
        if (err) g_error_free(err);
    }

    /* Stack: list(cards) | details */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    /* LIST page with cards */
    GtkWidget *cards_sc = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *cards = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(cards_sc), cards);
    gtk_stack_add_titled(GTK_STACK(stack), cards_sc, "list", "List");

    /* DETAILS page */
    GtkWidget *details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    /* Header: Back + Title */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_back = gtk_button_new_with_label("◀ Back");
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Dataset</b>");
    gtk_box_pack_start(GTK_BOX(hdr), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hdr), title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(details), hdr, FALSE, FALSE, 0);

    /* Info grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    GtkWidget *l_user = gtk_label_new("User/Owner:");
    GtkWidget *l_size = gtk_label_new("Size:");
    GtkWidget *l_rows = gtk_label_new("Rows:");
    GtkWidget *l_link = gtk_label_new("Link:");
    GtkWidget *l_desc = gtk_label_new("Description:");

    GtkWidget *lbl_user_inner = gtk_label_new("—");
    GtkWidget *v_user_event = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(v_user_event), FALSE);
    gtk_container_add(GTK_CONTAINER(v_user_event), lbl_user_inner);

    GtkWidget *v_size = gtk_label_new("—");
    GtkWidget *v_rows = gtk_label_new("—");
    GtkWidget *v_link = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(v_link), TRUE);
    gtk_label_set_selectable(GTK_LABEL(v_link), TRUE);
    GtkWidget *v_desc = gtk_label_new("—");
    gtk_label_set_line_wrap(GTK_LABEL(v_desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(v_desc), 0.0);

    int r = 0;
    gtk_grid_attach(GTK_GRID(grid), l_user, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v_user_event, 1, r++, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l_size, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v_size, 1, r++, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l_rows, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v_rows, 1, r++, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l_link, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v_link, 1, r++, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l_desc, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v_desc, 1, r++, 1, 1);

    gtk_box_pack_start(GTK_BOX(details), grid, FALSE, FALSE, 0);

    /* (Optional) Import button area */
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_import = gtk_button_new_with_label("Import to Environment");
    gtk_box_pack_end(GTK_BOX(actions), btn_import, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(details), actions, FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(stack), details, "details", "Details");

    /* Wrap and mount */
    gtk_box_pack_start(GTK_BOX(outer), wrap_CSS(DATASETS_CSS, "metal-panel", top, "env_top"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), wrap_CSS(DATASETS_CSS, "metal-panel", stack, "env_scroll"), TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, outer, lbl);

    /* Build TabCtx */
    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry = GTK_ENTRY(entry);

    /* (We keep a TreeView pointer in TabCtx in case you reuse it later; not needed for details) */
    ctx->view  = GTK_TREE_VIEW(gtk_tree_view_new());
    ctx->store = NULL;

    /* Bundle UI pointers & stash them on the entry (easy to grab on refresh) */
    DatasetsUI *dui = g_new0(DatasetsUI, 1);
    dui->user_event = v_user_event;
    dui->stack = GTK_STACK(stack);
    dui->cards = GTK_LIST_BOX(cards);
    dui->title_label = title;
    dui->lbl_user = GTK_LABEL(lbl_user_inner);
    dui->lbl_size = GTK_LABEL(v_size);
    dui->lbl_rows = GTK_LABEL(v_rows);
    dui->lbl_link = GTK_LABEL(v_link);
    dui->lbl_desc = GTK_LABEL(v_desc);

    g_object_set_data_full(G_OBJECT(entry), "datasets-ui", dui, g_free);

    /* Signals */
    g_signal_connect(v_user_event, "button-press-event", G_CALLBACK(on_user_clicked), NULL);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(refresh_datasets_cb), ctx);
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_to_list_clicked), dui->stack);

    gtk_widget_show_all(outer);

    /* First fill */
    refresh_datasets_cb(NULL, ctx);

    return ctx;
}


// datasets locais: upload/download

#include <gio/gio.h>

typedef struct {
    GPtrArray *columns; /* GPtrArray<char*> nomes de colunas */
    GPtrArray *rows;    /* GPtrArray<GPtrArray<char*>> linhas, cada célula é string */
    char delim;
} CsvPreview;

/* --- Utils de CSV --- */
static char detect_delim(const char *header) {
    if (!header) return ',';
    int c = 0, s = 0, t = 0;
    for (const char *p = header; *p; ++p) {
        if (*p == ',') c++;
        else if (*p == ';') s++;
        else if (*p == '\t') t++;
    }
    if (t >= c && t >= s) return '\t';
    if (s >= c && s >= t) return ';';
    return ',';
}

/* parse [simples] que respeita aspas duplas básicas: "a,b",c -> [a,b] [c] */
static void split_csv_line(const char *line, char delim, GPtrArray *out_cells) {
    const char *p = line;
    GString *cell = g_string_new(NULL);
    gboolean in_quotes = FALSE;

    while (*p) {
        char ch = *p++;
        if (ch == '"') {
            if (in_quotes && *p == '"') { g_string_append_c(cell, '"'); p++; }
            else in_quotes = !in_quotes;
        } else if (ch == delim && !in_quotes) {
            g_ptr_array_add(out_cells, g_string_free(cell, FALSE));
            cell = g_string_new(NULL);
        } else if (ch == '\r') {
            /* ignore */
        } else if (ch == '\n' && !in_quotes) {
            break;
        } else {
            g_string_append_c(cell, ch);
        }
    }
    g_ptr_array_add(out_cells, g_string_free(cell, FALSE));
}

/* --- Constrói preview em GtkTreeView (com listras alternadas) --- */
static void tv_build_from_preview(GtkTreeView *tv, CsvPreview *pv, guint max_cols) {
    GList *cols = gtk_tree_view_get_columns(tv);
    for (GList *l = cols; l; l = l->next) gtk_tree_view_remove_column(tv, GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(cols);

    gtk_tree_view_set_rules_hint(tv, TRUE);

    guint ncols = pv->columns ? pv->columns->len : 0;
    if (ncols == 0) return;
    if (max_cols && ncols > max_cols) ncols = max_cols;

    GType *types = g_new0(GType, ncols + 1);           /* +1 for bg color */
    for (guint i=0;i<ncols;i++) types[i] = G_TYPE_STRING;
    types[ncols] = G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(ncols + 1, types);
    g_free(types);

    for (guint i=0;i<ncols;i++) {
        const char *title = i < pv->columns->len ? (const char*)pv->columns->pdata[i] : "";
        GtkCellRenderer *rend = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(title, rend, "text", i, "cell-background", ncols, NULL);
        gtk_tree_view_append_column(tv, col);
    }

    guint shown = 0;
    for (guint r=0; r<pv->rows->len && shown < 200; r++, shown++) {
        GPtrArray *cells = pv->rows->pdata[r];
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        const char *bg = (r % 2 == 0) ? "#ffffff" : "#f5f5f5";
        for (guint c=0;c<ncols;c++) {
            const char *val = (c < cells->len) ? (const char*)cells->pdata[c] : "";
            gtk_list_store_set(store, &it, c, val, -1);
        }
        gtk_list_store_set(store, &it, ncols, bg, -1);
    }

    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_grid_lines(tv, GTK_TREE_VIEW_GRID_LINES_BOTH);
}

static void csv_preview_free(CsvPreview *pv) {
    if (!pv) return;
    if (pv->columns) g_ptr_array_free(pv->columns, TRUE);   /* cells freed by free_func */
    if (pv->rows)    g_ptr_array_free(pv->rows, TRUE);      /* each row unref'd; rows free their cells */
    g_free(pv);
}

typedef struct {
    gchar *path;
    GtkTreeView *target_tv;   // <— add this
} LoadTaskData;

static void task_read_preview(GTask *task, gpointer src, gpointer task_data, GCancellable *canc) {
    LoadTaskData *td = (LoadTaskData*)task_data;
    GError *err = NULL;

    GFile *gf = g_file_new_for_path(td->path);
    GFileInputStream *fis = g_file_read(gf, canc, &err);
    if (!fis) {
        g_task_return_error(task, err);
        g_object_unref(gf);
        return;
    }
    GDataInputStream *din = g_data_input_stream_new(G_INPUT_STREAM(fis));
    g_object_unref(fis);

    CsvPreview *pv = g_new0(CsvPreview, 1);
    pv->columns = g_ptr_array_new_with_free_func(g_free);
    pv->rows    = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);

    /* header */
    gsize len=0;
    gchar *line = g_data_input_stream_read_line(din, &len, canc, &err);
    if (!line) {
        g_clear_object(&din); g_object_unref(gf);
        if (!err) err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "Empty file");
        g_task_return_error(task, err);
        csv_preview_free(pv);
        return;
    }
    pv->delim = detect_delim(line);
    GPtrArray *hdr = g_ptr_array_new_with_free_func(g_free);
    split_csv_line(line, pv->delim, hdr);
    for (guint i=0;i<hdr->len;i++) g_ptr_array_add(pv->columns, g_strdup((char*)hdr->pdata[i]));
    g_ptr_array_free(hdr, TRUE);
    g_free(line);

    /* rows */
    guint cap = 10000; /* preview limit */
    for (guint r=0; r<cap; r++) {
        line = g_data_input_stream_read_line(din, &len, canc, &err);
        if (!line) break;
        GPtrArray *row = g_ptr_array_new_with_free_func(g_free);
        split_csv_line(line, pv->delim, row);
        g_ptr_array_add(pv->rows, row);
        g_free(line);
    }

    g_clear_object(&din);
    g_object_unref(gf);

    if (err) {
        csv_preview_free(pv);
        g_task_return_error(task, err);
        return;
    }
    g_task_return_pointer(task, pv, (GDestroyNotify)csv_preview_free);
}

/* --- Callback após worker --- */
static void on_task_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    GError *err = NULL;
    CsvPreview *pv = g_task_propagate_pointer(G_TASK(res), &err);
    LoadTaskData *td = (LoadTaskData*)g_task_get_task_data(G_TASK(res));

    if (ctx && ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
    if (ctx && ctx->status)   gtk_label_set_text(ctx->status, err ? "Load failed" : "Loaded");

    if (err) {
        GtkWidget *dlg = gtk_message_dialog_new(
            (ctx && ctx->main_window) ? GTK_WINDOW(ctx->main_window) : NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Erro ao ler dataset: %s", err->message);
        gtk_window_set_title(GTK_WINDOW(dlg), "Erro");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        g_error_free(err);
        return;
    }

    /* rebuild preview table */
    if (td && td->target_tv) {
        tv_build_from_preview(td->target_tv, pv, 64);

        enable_drop_on_env_entries(ctx);
        wire_treeview_headers_for_dnd(ctx, td->target_tv);
    }

}

static void free_load_task_data(LoadTaskData *td) {
    if (!td) return;
    g_free(td->path);
    g_free(td);
}

/* helper: find a notebook page by visible label text */
static gint find_notebook_page_by_label(GtkNotebook *nb, const char *label) {
    if (!nb || !label) return -1;
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i=0; i<n; ++i) {
        GtkWidget *child = gtk_notebook_get_nth_page(nb, i);
        GtkWidget *tab   = gtk_notebook_get_tab_label(nb, child);
        if (GTK_IS_LABEL(tab)) {
            const gchar *txt = NULL;
            g_object_get(tab, "label", &txt, NULL); // works in GTK3
            if (txt && g_strcmp0(txt, label) == 0) return i;
        }
    }
    return -1;
}

static void start_load_file(EnvCtx *ctx, const char *path) {
    if (!ctx || !path || !*path) return;

    g_free(ctx->current_dataset_path);
    ctx->current_dataset_path = g_strdup(path);

    if (ctx->status)   gtk_label_set_text(ctx->status, "Loading…");
    if (ctx->progress) gtk_progress_bar_pulse(ctx->progress);

    const char *TAB_NAME = "Preview dataset";
    GtkTreeView *tv = ctx->ds_preview_tv;
    gint page_idx = find_notebook_page_by_label(ctx->right_nb, TAB_NAME);

    if (!tv || page_idx < 0) {
        /* first time: build page = [ scroller(table) ; picker-bar ] */
        GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

        GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        GtkWidget *tvw = gtk_tree_view_new();
        gtk_container_add(GTK_CONTAINER(sc), tvw);
        gtk_box_pack_start(GTK_BOX(page_box), sc, TRUE, TRUE, 0);

        /* picker bar placeholder (we’ll create real combos after CSV is parsed) */
        GtkWidget *placeholder = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(page_box), placeholder, FALSE, FALSE, 0);

        ctx->ds_preview_tv = GTK_TREE_VIEW(tvw);
        /* remember parent vbox for later */
        g_object_set_data(G_OBJECT(tvw), "picker-parent", page_box);

        gint page = gtk_notebook_append_page(ctx->right_nb, page_box, gtk_label_new(TAB_NAME));
        gtk_widget_show_all(page_box);
        gtk_notebook_set_current_page(ctx->right_nb, page);
    } else {
        gtk_notebook_set_current_page(ctx->right_nb, page_idx);
    }

    /* Launch worker and tell it where to render */
    LoadTaskData *td = g_new0(LoadTaskData, 1);
    td->path = g_strdup(path);
    td->target_tv = ctx->ds_preview_tv;

    GTask *t = g_task_new(NULL, NULL, on_task_done, ctx);
    g_task_set_task_data(t, td, (GDestroyNotify)free_load_task_data);
    g_task_run_in_thread(t, task_read_preview);
    g_object_unref(t);
}


static void on_load_selected_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    gchar *path = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    if (!path) {
        if (ctx->status) gtk_label_set_text(ctx->status, "No dataset selected");
        return;
    }
    if (ctx->status) gtk_label_set_text(ctx->status, "Loading…");
    start_load_file(ctx, path);
    g_free(path);
}

/* --- File Chooser (Load) --- */
static void on_load_local_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;

    GtkWindow *parent = NULL;
    if (ctx->main_window && GTK_IS_WINDOW(ctx->main_window))
        parent = GTK_WINDOW(ctx->main_window);

#if defined(G_OS_WIN32)
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *fc = GTK_FILE_CHOOSER(dlg);
#else
#  if GTK_CHECK_VERSION(3,20,0)
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Choose a dataset", parent,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open", "_Cancel");
    GtkFileChooser *fc = GTK_FILE_CHOOSER(native);
#  else
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *fc = GTK_FILE_CHOOSER(dlg);
#  endif
#endif

    /* filtros */
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, "Data files (CSV/TSV)");
    gtk_file_filter_add_pattern(flt, "*.csv");
    gtk_file_filter_add_pattern(flt, "*.tsv");
    gtk_file_filter_add_mime_type(flt, "text/csv");
    gtk_file_filter_add_mime_type(flt, "text/tab-separated-values");
    gtk_file_chooser_add_filter(fc, flt);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(fc, all);

    /* diretório sugerido: ./datasets ao lado do executável */
    gchar *cwd = g_get_current_dir();
    gchar *datasets_dir = g_build_filename(cwd, "datasets", NULL);
    if (g_file_test(datasets_dir, G_FILE_TEST_IS_DIR))
        gtk_file_chooser_set_current_folder(fc, datasets_dir);
    g_free(datasets_dir);
    g_free(cwd);

    /* run + fallback se necessário */
    gboolean accepted = FALSE;
    char *path = NULL;

#if defined(G_OS_WIN32)
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    }
    gtk_widget_destroy(dlg);
#else
#  if GTK_CHECK_VERSION(3,20,0)
    gint resp = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    } else if (resp == GTK_RESPONSE_DELETE_EVENT) {
        /* Fallback explícito pro Dialog se o Native falhar/fechar de forma anômala */
        GtkWidget *dlg2 = gtk_file_chooser_dialog_new(
            "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
        GtkFileChooser *fc2 = GTK_FILE_CHOOSER(dlg2);
        gint r2 = gtk_dialog_run(GTK_DIALOG(dlg2));
        if (r2 == GTK_RESPONSE_ACCEPT) {
            path = gtk_file_chooser_get_filename(fc2);
            accepted = (path != NULL);
        }
        gtk_widget_destroy(dlg2);
    }
    g_object_unref(native);
#  else
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    }
    gtk_widget_destroy(dlg);
#  endif
#endif

    if (!accepted || !path) {
        if (ctx->status) gtk_label_set_text(ctx->status, "Cancelled");
        if (path) g_free(path);
        return;
    }

    if (ctx->ds_combo) {
        gtk_combo_box_text_append_text(ctx->ds_combo, path);
        GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(ctx->ds_combo));
        if (m) {
            gint n = gtk_tree_model_iter_n_children(m, NULL);
            if (n > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), n - 1);
        }
    }
    if (ctx->status) gtk_label_set_text(ctx->status, "Ready to load");
    g_free(path);
}


/* --- Popular combo de datasets a partir de ./datasets --- */
static void on_refresh_local_datasets(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    /* limpa combo de forma correta e sem leaks */
    gtk_combo_box_text_remove_all(ctx->ds_combo);

    gchar *cwd = g_get_current_dir();
    gchar *datasets_dir = g_build_filename(cwd, "datasets", NULL);
    if (g_file_test(datasets_dir, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(datasets_dir, 0, NULL);
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            if (g_str_has_suffix(name, ".csv") || g_str_has_suffix(name, ".tsv")) {
                gchar *full = g_build_filename(datasets_dir, name, NULL);
                gtk_combo_box_text_append_text(ctx->ds_combo, full);
                g_free(full);
            }
        }
        g_dir_close(dir);
    }
    g_free(datasets_dir);
    g_free(cwd);

    /* seleciona o primeiro, se houver */
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(ctx->ds_combo));
    if (m && gtk_tree_model_iter_n_children(m, NULL) > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), 0);
    }

    if (ctx->status) gtk_label_set_text(ctx->status, "Datasets refreshed");
}


#endif