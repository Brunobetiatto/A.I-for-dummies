#include "../css/css.h"
#include "debug_window.h"
#include "../backend/communicator.h"
#include "context.h"
#include <pango/pangocairo.h>
#include "profile.h"
#include "dataset_upload.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <ctype.h>

/* Portable fallback: some platforms/compilers don't expose M_PI by default.
    Provide a local definition when it's missing to avoid "M_PI undeclared" errors. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef DATASETS_H
#define DATASETS_H

static const GtkTargetEntry DND_TARGETS[] = {
    { "text/plain", 0, 1 },
};

#ifndef gdk_atom_equal
#  define gdk_atom_equal(a,b) ((a) == (b))
#endif

typedef struct {
    GtkTreeView  *tv;
    GtkListStore *store;
} DsListView;

typedef struct {
    GtkStack   *stack;
    DsListView list;

    GtkWidget  *title_label; /* top title in details */

    GtkLabel   *lbl_user;
    GtkLabel   *lbl_size;
    GtkLabel   *lbl_rows;
    GtkLabel   *lbl_link;
    GtkLabel   *lbl_desc;

    GtkWidget  *user_event;
} DatasetsUI;

/* === LIST (Win95) model === */
enum {
    DS_COL_ICON = 0,
    DS_COL_NAME,
    DS_COL_DESC,
    DS_COL_SIZE,
    DS_COL_META,
    DS_N_COLS
};


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

/* --- Helpers de conversão de tamanho para MB --- */
static gboolean parse_to_mb(const char *s, double *out_mb) {
    if (!s || !*s || !out_mb) return FALSE;

    char *tmp = g_strdup(s);
    trim_spaces(tmp);

    /* lower-case e tira "bytes"/"byte" do final, se tiver */
    for (char *p = tmp; *p; ++p) *p = (char)g_ascii_tolower(*p);
    size_t len = strlen(tmp);
    if (len >= 5 && g_str_has_suffix(tmp, "bytes")) { tmp[len-5] = '\0'; trim_spaces(tmp); }
    else if (len >= 4 && g_str_has_suffix(tmp, "byte")) { tmp[len-4] = '\0'; trim_spaces(tmp); }

    char *end = NULL;
    double v = g_ascii_strtod(tmp, &end);
    if (end == tmp) { g_free(tmp); return FALSE; }

    while (end && *end && g_ascii_isspace(*end)) end++;

    double mb = 0.0;
    if (!end || !*end) {
        /* sem sufixo => assume bytes */
        mb = v / (1024.0 * 1024.0);
    } else if (*end == 'k') {                       /* kb/kib */
        mb = v / 1024.0;
    } else if (*end == 'm') {                       /* mb/mib */
        mb = v;
    } else if (*end == 'g') {                       /* gb/gib */
        mb = v * 1024.0;
    } else if (*end == 't') {                       /* tb/tib */
        mb = v * 1024.0 * 1024.0;
    } else if (*end == 'b') {                       /* b */
        mb = v / (1024.0 * 1024.0);
    } else {
        /* sufixo desconhecido -> tenta como bytes */
        mb = v / (1024.0 * 1024.0);
    }

    g_free(tmp);
    *out_mb = mb;
    return TRUE;
}

static char* size_to_mb_string(const char *s) {
    double mb = 0.0;
    if (!parse_to_mb(s, &mb)) return g_strdup(s ? s : "");
    return g_strdup_printf("%.1f MB", mb);
}

static char* size_bytes_to_mb_string(double bytes) {
    double mb = bytes / (1024.0 * 1024.0);
    return g_strdup_printf("%.1f MB", mb);
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

/* Protótipos (iguais aos de login.h) para o compilador conhecer antes do uso */
static gboolean on_enter(GtkWidget *w, GdkEventCrossing *e, gpointer u);
static gboolean on_leave(GtkWidget *w, GdkEventCrossing *e, gpointer u);


/* === Cursor helpers: aplica enter/leave a 1 widget e percorre containers === */

static void apply_hand_cursor_to(GtkWidget *w) {
    if (!w) return;
    gtk_widget_add_events(w, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(w, "enter-notify-event", G_CALLBACK(on_enter), NULL);
    g_signal_connect(w, "leave-notify-event", G_CALLBACK(on_leave), NULL);
}

/* percorre recursivamente e aplica em botões, eventboxes e link-buttons */
static void hand_cursor_forall(GtkWidget *child, gpointer user_data) {
    (void)user_data;
    if (GTK_IS_BUTTON(child) || GTK_IS_EVENT_BOX(child) || GTK_IS_LINK_BUTTON(child)) {
        apply_hand_cursor_to(child);
    }
    if (GTK_IS_CONTAINER(child)) {
        gtk_container_forall(GTK_CONTAINER(child), hand_cursor_forall, NULL);
    }
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

/* Helper used by enable_drop_on_env_entries (pure C). */
static void reset_drop(GtkWidget *w, gboolean is_y, gpointer ctx, GtkTargetList *tl) {
    if (!w) return;

    /* Disconnect any previous handlers (match both NULL and ctx user_data). */
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(on_drop_drag_motion), NULL);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(on_drop_drag_leave),  NULL);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(is_y ? on_y_drag_drop : on_x_drag_drop), NULL);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(is_y ? on_y_drag_data_received : on_x_drag_data_received), NULL);

    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(on_drop_drag_motion), ctx);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(on_drop_drag_leave),  ctx);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(is_y ? on_y_drag_drop : on_x_drag_drop), ctx);
    g_signal_handlers_disconnect_by_func(w, G_CALLBACK(is_y ? on_y_drag_data_received : on_x_drag_data_received), ctx);

    gtk_drag_dest_unset(w);
    gtk_drag_dest_set(w, 0 /* no defaults */, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_set_target_list(w, tl);

    g_signal_connect(w, "drag-motion",        G_CALLBACK(on_drop_drag_motion),       NULL);
    g_signal_connect(w, "drag-leave",         G_CALLBACK(on_drop_drag_leave),        NULL);
    g_signal_connect(w, "drag-drop",          G_CALLBACK(is_y ? on_y_drag_drop : on_x_drag_drop),            ctx);
    g_signal_connect(w, "drag-data-received", G_CALLBACK(is_y ? on_y_drag_data_received : on_x_drag_data_received), ctx);

    gtk_widget_set_tooltip_text(
        w, is_y ? "Drag a column header here to set Y (target)"
                : "Drag column headers here to add X features");
}

/* Idempotent: clears old handlers/targets on X/Y, then re-applies them. */
static void enable_drop_on_env_entries(EnvCtx *ctx) {
    GtkTargetList *tl;
    if (!ctx) return;

    tl = gtk_target_list_new(NULL, 0);
    gtk_target_list_add(tl, get_col_atom(), 0, 99);   /* private atom preferred */
    gtk_target_list_add_text_targets(tl, 0);          /* fallbacks */

    if (ctx->x_feat && GTK_IS_ENTRY(ctx->x_feat))
        reset_drop(GTK_WIDGET(ctx->x_feat), FALSE, ctx, tl);
    if (ctx->y_feat && GTK_IS_ENTRY(ctx->y_feat))
        reset_drop(GTK_WIDGET(ctx->y_feat), TRUE,  ctx, tl);

    gtk_target_list_unref(tl);
}

static void wire_treeview_headers_for_dnd(EnvCtx *ctx, GtkTreeView *tv) {
    GList *cols, *l;

    (void)ctx;
    if (!tv || !GTK_IS_TREE_VIEW(tv)) return;

    gtk_tree_view_set_rules_hint(tv, TRUE);

    cols = gtk_tree_view_get_columns(tv);
    for (l = cols; l; l = l->next) {
        GtkTreeViewColumn *col = GTK_TREE_VIEW_COLUMN(l->data);
        const gchar *title0 = gtk_tree_view_column_get_title(col);
        const gchar *title  = (title0 && *title0) ? title0 : "col";

        /* Fresh header widget every call to avoid stale wiring */
        GtkWidget *eb  = gtk_event_box_new();
        GtkWidget *lab = gtk_label_new(title);
        GtkTargetList *tl = NULL;
        gchar *name_copy;

        apply_hand_cursor_to(eb);
        gtk_container_add(GTK_CONTAINER(eb), lab);
        gtk_widget_set_tooltip_text(eb, "Drag to X or Y");
        gtk_widget_show_all(eb);
        gtk_tree_view_column_set_widget(col, eb);

        /* Source targets */
        tl = gtk_target_list_new(NULL, 0);
        gtk_target_list_add(tl, get_col_atom(), 0, 99);
        gtk_target_list_add_text_targets(tl, 0);

        gtk_drag_source_unset(eb);
        gtk_drag_source_set(eb, GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY);
        gtk_drag_source_set_target_list(eb, tl);
        gtk_target_list_unref(tl);

        /* Handlers: data-get + cosmetics; keep name_copy alive while eb lives */
        name_copy = g_strdup(title);
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

static gchar* make_download_link_markup(const char *url);

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
        size_pp = size_to_mb_string(v_size);
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
    gchar *mk = make_download_link_markup(v_link);
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

/* Libera o meta (GHashTable*) de todas as linhas antes de limpar a store */
static void free_all_meta_from_store(GtkListStore *store) {
    if (!store) return;
    GtkTreeModel *m = GTK_TREE_MODEL(store);
    GtkTreeIter it;
    gboolean ok = gtk_tree_model_get_iter_first(m, &it);
    while (ok) {
        GHashTable *meta = NULL;
        gtk_tree_model_get(m, &it, DS_COL_META, &meta, -1);
        if (meta) g_hash_table_destroy(meta);
        ok = gtk_tree_model_iter_next(m, &it);
    }
}

/* Preenche o painel de detalhes a partir do meta + título já pronto */
static void fill_details_from_meta(DatasetsUI *dui, GHashTable *meta, const char *title_mk) {
    if (!dui) return;

    if (title_mk && GTK_IS_LABEL(dui->title_label))
        gtk_label_set_markup(GTK_LABEL(dui->title_label), title_mk);

    const char *A_USER[]  = {"usuario","user","owner","author","autor","usuario_nome","enviado_por_nome"};
    const char *A_EMAIL[] = {"usuario_email","enviado_por_email","email","user_email","owner_email"};
    const char *A_SIZE[]  = {"size","tamanho","bytes"};
    const char *A_ROWS[]  = {"rows","linhas","nrows","amostras","samples"};
    const char *A_LINK[]  = {"link","url","source","path"};
    const char *A_DESC[]  = {"descricao","description","desc","enviado_por_descricao"};

    const char *v_user  = meta ? meta_get_any(meta, A_USER,  G_N_ELEMENTS(A_USER))  : NULL;
    const char *v_email = meta ? meta_get_any(meta, A_EMAIL, G_N_ELEMENTS(A_EMAIL)) : NULL;
    const char *v_size  = meta ? meta_get_any(meta, A_SIZE,  G_N_ELEMENTS(A_SIZE))  : NULL;
    const char *v_rows  = meta ? meta_get_any(meta, A_ROWS,  G_N_ELEMENTS(A_ROWS))  : NULL;
    const char *v_link  = meta ? meta_get_any(meta, A_LINK,  G_N_ELEMENTS(A_LINK))  : NULL;
    const char *v_desc  = meta ? meta_get_any(meta, A_DESC,  G_N_ELEMENTS(A_DESC))  : NULL;
    char *size_pp = (v_size && *v_size) ? size_to_mb_string(v_size) : NULL;

    char user_display[256] = "—";
    if (v_user && *v_user) g_strlcpy(user_display, v_user, sizeof(user_display));

    gtk_label_set_text(dui->lbl_user, user_display);
    gtk_label_set_text(dui->lbl_rows, (v_rows && *v_rows) ? v_rows : "—");
    gtk_label_set_text(dui->lbl_desc, (v_desc && *v_desc) ? v_desc : "—");
    gtk_label_set_text(dui->lbl_size,
        (size_pp && *size_pp) ? size_pp : ((v_size && *v_size) ? v_size : "—"));

    if (v_link && *v_link) {
    gchar *mk = make_download_link_markup(v_link);
    gtk_label_set_markup(GTK_LABEL(dui->lbl_link), mk);
    g_free(mk);
    } else {
        gtk_label_set_text(dui->lbl_link, "—");
    }
    g_free(size_pp);

    /* uploader-id (se existir) para o clique no nome do usuário abrir o perfil */
    if (dui->user_event) {
        const char *A_UID[] = {"usuario_idusuario","usuarioid","user_id","uploader_id","idusuario","owner_id","id"};
        const char *uid_str = meta ? meta_get_any(meta, A_UID, G_N_ELEMENTS(A_UID)) : NULL;
        int uid = (uid_str && *uid_str) ? atoi(uid_str) : 0;
        if (uid > 0) {
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-id", GINT_TO_POINTER(uid));
            gtk_widget_set_sensitive(dui->user_event, TRUE);
        } else {
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-id", NULL);
            gtk_widget_set_sensitive(dui->user_event, FALSE);
        }
        if (v_user && *v_user)
            g_object_set_data_full(G_OBJECT(dui->user_event), "uploader-name", g_strdup(v_user), g_free);
        else
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-name", NULL);

        if (v_email && *v_email)
            g_object_set_data_full(G_OBJECT(dui->user_event), "uploader-email", g_strdup(v_email), g_free);
        else
            g_object_set_data(G_OBJECT(dui->user_event), "uploader-email", NULL);
    }

    if (GTK_IS_STACK(dui->stack))
        gtk_stack_set_visible_child_name(dui->stack, "details");
}

/* duplo-clique/Enter na linha da lista => abrir detalhes */
static void on_ds_row_activated(GtkTreeView *tv, GtkTreePath *path,
                                GtkTreeViewColumn *col, gpointer user_data) {
    (void)col;
    DatasetsUI *dui = (DatasetsUI*)user_data;
    GtkTreeModel *m = gtk_tree_view_get_model(tv);
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(m, &it, path)) return;

    gchar *name = NULL, *desc = NULL, *size = NULL;
    GHashTable *meta = NULL;
    gtk_tree_model_get(m, &it,
        DS_COL_NAME, &name,
        DS_COL_DESC, &desc,
        DS_COL_SIZE, &size,
        DS_COL_META, &meta, -1);

    gchar *mk = g_markup_printf_escaped("<b>%s</b>", name ? name : "Dataset");
    fill_details_from_meta(dui, meta, mk);
    g_free(mk); g_free(name); g_free(desc); g_free(size);
}

static void refresh_datasets_cb(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    TabCtx *ctx = (TabCtx*)user_data;
    if (!ctx) return;

    DatasetsUI *dui = (DatasetsUI*)g_object_get_data(G_OBJECT(ctx->entry), "datasets-ui");
    if (!dui || !dui->list.store) return;

    /* chama API */
    char *resp = NULL;
    if (!api_dump_table("dataset", &resp) || !resp) { if (resp) free(resp); return; }
    cJSON *root = cJSON_Parse(resp); free(resp);
    if (!root) return;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) { cJSON_Delete(root); return; }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data    = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) { cJSON_Delete(root); return; }

    /* mapeia índices mais comuns */
    int ncols = cJSON_GetArraySize(columns);
    int idx_nome=-1, idx_desc=-1, idx_size=-1;
    for (int i=0;i<ncols;i++) {
        cJSON *col = cJSON_GetArrayItem(columns, i);
        if (!cJSON_IsString(col) || !col->valuestring) continue;
        const char *nm = col->valuestring;
        if (!g_ascii_strcasecmp(nm,"nome") || !g_ascii_strcasecmp(nm,"name") || !g_ascii_strcasecmp(nm,"title")) idx_nome=i;
        if (!g_ascii_strcasecmp(nm,"descricao") || !g_ascii_strcasecmp(nm,"description") || !g_ascii_strcasecmp(nm,"desc")) idx_desc=i;
        if (!g_ascii_strcasecmp(nm,"tamanho") || !g_ascii_strcasecmp(nm,"size") || !g_ascii_strcasecmp(nm,"bytes")) idx_size=i;
    }

    /* limpar store com segurança (liberar metas antigas) */
    free_all_meta_from_store(dui->list.store);
    gtk_list_store_clear(dui->list.store);

    /* inserir linhas */
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;

        /* texto das colunas */
        char name_buf[256] = "(dataset)";
        char desc_buf[512] = "";
        char size_buf[64]  = "";

        if (idx_nome >= 0) {
            cJSON *cell = cJSON_GetArrayItem(row, idx_nome);
            if (cJSON_IsString(cell) && cell->valuestring) g_strlcpy(name_buf, cell->valuestring, sizeof(name_buf));
        }
        if (idx_desc >= 0) {
            cJSON *cell = cJSON_GetArrayItem(row, idx_desc);
            if (cJSON_IsString(cell) && cell->valuestring) g_strlcpy(desc_buf, cell->valuestring, sizeof(desc_buf));
        }
        if (idx_size >= 0) {
        cJSON *cell = cJSON_GetArrayItem(row, idx_size);
        if (cJSON_IsString(cell) && cell->valuestring) {
            char *pp = size_to_mb_string(cell->valuestring);
            g_strlcpy(size_buf, pp ? pp : cell->valuestring, sizeof(size_buf));
            g_free(pp);
        } else if (cJSON_IsNumber(cell)) {
            char *pp = size_bytes_to_mb_string(cell->valuedouble); /* assume número em bytes */
            g_strlcpy(size_buf, pp ? pp : "", sizeof(size_buf));
            g_free(pp);
        }
    }

        /* meta K/V para detalhes e cliques */
        GHashTable *meta = make_row_meta(columns, row);
        GdkPixbuf *row_icon = g_object_get_data(G_OBJECT(dui->list.store), "row-icon");

        GtkTreeIter it;
        gtk_list_store_append(dui->list.store, &it);
        gtk_list_store_set(dui->list.store, &it,
            DS_COL_ICON, row_icon,
            DS_COL_NAME, name_buf,
            DS_COL_DESC, desc_buf,
            DS_COL_SIZE, size_buf,
            DS_COL_META, meta, -1);
    }

    /* volta para a página List ao atualizar */
    gtk_stack_set_visible_child_name(dui->stack, "list");

    GtkEntry *search_entry = ctx ? ctx->entry : NULL;
    if (search_entry) {
        GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER(g_object_get_data(G_OBJECT(search_entry), "ds-filter"));
        if (filter) gtk_tree_model_filter_refilter(filter);
    }
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

/* helper: open upload dialog using the notebook's toplevel window as parent */
static void on_open_upload_dialog(GtkButton *btn, gpointer user_data) {
    EnvCtx *env = (EnvCtx*) user_data;
    GtkNotebook *nb = GTK_NOTEBOOK(gtk_widget_get_parent(GTK_WIDGET(btn)));
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
    GtkWindow *parent = GTK_WINDOW(toplevel);
    int default_user_id = env ? env->current_user_id : 0;
    /* prefill nome/email: passaremos a enviar também os strings (se estiverem presentes) */
    show_dataset_upload_dialog(parent, env);
    /* se desejar pré-preencher enviado_por_nome/email automaticamente dentro do diálogo,
       você pode modificar show_dataset_upload_dialog para aceitar nome/email extras
       ou setar os entries via g_object_set_data no UploadUI. */
}

/* Importa dataset selecionado para o ambiente (chamada pelo botão "Import to Environment") */
static void on_import_to_environment(GtkButton *btn, gpointer user_data) {
    (void)btn;
    DatasetsUI *dui = (DatasetsUI*) user_data;
    if (!dui) return;

    /* 1) obter o texto do label de link (deveria ser a URL visível) */
    const char *link_text = NULL;
    if (GTK_IS_LABEL(dui->lbl_link)) {
        /* gtk_label_get_text retorna o conteúdo "visível" do label (sem tags) */
        link_text = gtk_label_get_text(GTK_LABEL(dui->lbl_link));
    }

    if (!link_text || !*link_text || g_strcmp0(link_text, "—") == 0) {
        gtk_label_set_text(dui->lbl_desc, "Nenhum link de dataset disponível.");
        return;
    }

    /* 2) às vezes o label contém markup (ex.: <a href="...">url</a>), porém gtk_label_get_text
       deve devolver o texto exibido — assumimos que isso retorna a URL ou ao menos contém '/'. */

    /* 3) extrair somente o nome do arquivo (após última '/') */
    const char *basename = strrchr(link_text, '/');
    if (basename && *(basename + 1) != '\0') {
        basename++; /* avança para depois da barra */
    } else {
        /* se não houver '/', talvez o label contenha apenas o nome já */
        basename = link_text;
    }

    if (!basename || !*basename) {
        gtk_label_set_text(dui->lbl_desc, "Nome de arquivo inválido no link.");
        return;
    }

    /* 4) montar comando UTF-8 "GET_DATASET <filename>" */
    char cmd_utf8[1024];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "GET_DATASET %s", basename);

    /* 5) converter para WCHAR (UTF-16) */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    if (wlen <= 0) {
        gtk_label_set_text(dui->lbl_desc, "Erro de conversão (UTF-8->WCHAR).");
        return;
    }
    WCHAR *wcmd = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
    if (!wcmd) {
        gtk_label_set_text(dui->lbl_desc, "Erro de memória.");
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);

    /* 6) chama run_api_command (bloqueante) */
    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);

    if (!wresp) {
        gtk_label_set_text(dui->lbl_desc, "Falha: sem resposta da API.");
        return;
    }

    /* 7) converter resposta WCHAR -> UTF-8 */
    int rlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    if (rlen <= 0) {
        free(wresp);
        gtk_label_set_text(dui->lbl_desc, "Erro de conversão da resposta.");
        return;
    }
    char *resp = (char*)malloc((size_t)rlen);
    if (!resp) {
        free(wresp);
        gtk_label_set_text(dui->lbl_desc, "Erro de memória.");
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, rlen, NULL, NULL);
    free(wresp);

    /* 8) parse curto: apenas OK / ERROR */
    gboolean ok = FALSE;
    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
        if (cJSON_IsString(status) && strcmp(status->valuestring, "OK") == 0) ok = TRUE;
        cJSON_Delete(root);
    } else {
        /* fallback para formato legado "OK ..." */
        if (strncmp(resp, "OK", 2) == 0) ok = TRUE;
    }

    /* 9) mostrar mensagem curta e elegante no lbl_desc */
    if (ok) {
        gtk_label_set_text(dui->lbl_desc, "✅ Dataset importado para o ambiente.");
    } else {
        gtk_label_set_text(dui->lbl_desc, "❌ Falha ao importar dataset.");
    }

    free(resp);
}

/* Forward declarations de callbacks usados antes da definição */
static gboolean search_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data);

/* Forward declarations (busca) */
static void on_search_changed(GtkEditable *e, gpointer user_data);
static void on_search_activate(GtkEntry *e, gpointer user_data);
static void on_search_icon_press(GtkEntry *e, GtkEntryIconPosition pos, GdkEvent *ev, gpointer u);
static gboolean search_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data);
static void on_clear_clicked(GtkButton *btn, gpointer user_data);

static TabCtx* add_datasets_tab(GtkNotebook *nb, EnvCtx *env) {
    const char *DATASETS_CSS = parse_CSS_file("datasets.css");

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 6);

    gtk_widget_set_name(outer, "datasets-window");

    /* Top bar: search + upload + refresh */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    /* Entry + botão clear */
    GtkWidget *entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Search datasets…");
    gtk_box_pack_start(GTK_BOX(entry_box), entry, TRUE, TRUE, 0);

    /* Botão “clear”*/
    GtkWidget *clear_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(clear_btn), GTK_RELIEF_NONE);  /* estilo “flat” */
    gtk_widget_set_tooltip_text(clear_btn, "Limpar busca");

    gtk_widget_set_name(clear_btn, "entry-clear");

    {
        GError *err = NULL;
        GdkPixbuf *pix = gdk_pixbuf_new_from_file("./assets/clear.png", &err);
        GtkWidget *img = NULL;
        if (pix) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, 22, 22, GDK_INTERP_BILINEAR);
            img = gtk_image_new_from_pixbuf(scaled ? scaled : pix);
            if (scaled) g_object_unref(scaled);
            g_object_unref(pix);
        } else {
            /* fallback do tema */
            img = gtk_image_new_from_icon_name("edit-clear-symbolic", GTK_ICON_SIZE_BUTTON);
            if (err) g_error_free(err);
        }
    #if !GTK_CHECK_VERSION(4,0,0)
        gtk_button_set_always_show_image(GTK_BUTTON(clear_btn), TRUE);
    #endif
        gtk_button_set_image(GTK_BUTTON(clear_btn), img);
    }

    /* coloca o botão à direita do entry */
    gtk_box_pack_start(GTK_BOX(entry_box), clear_btn, FALSE, FALSE, 0);

    /* agora empacote o entry_box no top */
    gtk_box_pack_start(GTK_BOX(top), entry_box, TRUE, TRUE, 0);

    /* limpar ao clicar */
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), entry);

    /* --- Upload button --- */
    GtkWidget *btn_upload_ui = gtk_button_new();
    {
        GError *err = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("./assets/upload.png", &err);
        if (pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 40, 40, GDK_INTERP_BILINEAR);
            GtkWidget *img = gtk_image_new_from_pixbuf(scaled ? scaled : pixbuf);
            gtk_button_set_image(GTK_BUTTON(btn_upload_ui), img);
    #if !GTK_CHECK_VERSION(4,0,0)
            gtk_button_set_always_show_image(GTK_BUTTON(btn_upload_ui), TRUE);
    #endif
            if (scaled) g_object_unref(scaled);
            g_object_unref(pixbuf);
        } else {
            /* fallback: texto se a imagem não existir */
            gtk_button_set_label(GTK_BUTTON(btn_upload_ui), "Upload");
            if (err) g_error_free(err);
        }
    }
    gtk_box_pack_start(GTK_BOX(top), btn_upload_ui, FALSE, FALSE, 0);
    g_signal_connect(btn_upload_ui, "clicked", G_CALLBACK(on_open_upload_dialog), env);

    /* existing refresh button */
    GtkWidget *btn_refresh = gtk_button_new();
    /* id para pegar o estilo #ds-refresh no CSS */
    gtk_widget_set_name(btn_refresh, "ds-refresh");
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

    /* LIST page — Win95 list (TreeView com cabeçalho) */
    GtkWidget *list_sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_sc),
                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *tv = gtk_tree_view_new();
    /* habilita highlight da linha sob o mouse */
    gtk_tree_view_set_hover_selection(GTK_TREE_VIEW(tv), TRUE);
    gtk_widget_set_name(tv, "ds-tv");
    GtkStyleContext *tvsc = gtk_widget_get_style_context(tv);
    gtk_style_context_add_class(tvsc, "win95-list");     /* CSS */

    GtkListStore *store = gtk_list_store_new(DS_N_COLS,
        GDK_TYPE_PIXBUF,  /* icon  */
        G_TYPE_STRING,   /* name  */
        G_TYPE_STRING,   /* desc  */
        G_TYPE_STRING,   /* size  */
        G_TYPE_POINTER); /* meta* */
    
    /* Ícone padrão das linhas (16x16) */
    {
        GError *err = NULL;
        GdkPixbuf *row_icon = gdk_pixbuf_new_from_file("./assets/dataset.png", &err);
        if (row_icon) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(row_icon, 16, 16, GDK_INTERP_BILINEAR);
            if (scaled) { g_object_unref(row_icon); row_icon = scaled; }
            /* guarda no store para recuperar no refresh */
            g_object_set_data_full(G_OBJECT(store), "row-icon",
                                g_object_ref(row_icon), g_object_unref);
            g_object_unref(row_icon);
        } else if (err) {
            g_error_free(err);
        }
    }
    /* === NOVO: filter (para busca) + sort (para manter ordenação nas colunas) === */
    GtkTreeModel *filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
                                        search_visible_func, entry, NULL);
    GtkTreeModel *sort = gtk_tree_model_sort_new_with_model(filter);

    /* o TreeView passa a enxergar sort->filter->store */
    gtk_tree_view_set_model(GTK_TREE_VIEW(tv), sort);

    /* guarde refs e ponteiros pra reuso */
    g_object_ref(store);
    g_object_set_data(G_OBJECT(entry), "ds-filter", filter);   /* pra refilter no "changed" */

    /* atualiza dinamicamente enquanto digita */
    g_signal_connect(entry, "changed",   G_CALLBACK(on_search_changed), NULL);
    /* Enter: se só sobrar 1 linha, abre detalhes */
    g_signal_connect(entry, "activate", G_CALLBACK(on_search_activate), NULL);
    /* clique no X limpa e refiltra */
    g_signal_connect(entry, "icon-press", G_CALLBACK(on_search_icon_press), NULL);

    /* guardamos um ref próprio para usar no refresh */
    g_object_ref(store);

    gtk_container_add(GTK_CONTAINER(list_sc), tv);
    gtk_stack_add_titled(GTK_STACK(stack), list_sc, "list", "List");

    GtkCellRenderer *ri = gtk_cell_renderer_pixbuf_new();
    g_object_set(ri, "xpad", 4, NULL);

    /* colunas */
    GtkCellRenderer *r1 = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c_name, "Name");
    gtk_tree_view_column_pack_start(c_name, ri, FALSE);
    gtk_tree_view_column_add_attribute(c_name, ri, "pixbuf", DS_COL_ICON);
    gtk_tree_view_column_pack_start(c_name, r1, TRUE);
    gtk_tree_view_column_add_attribute(c_name, r1, "text", DS_COL_NAME);

    gtk_tree_view_column_set_resizable(c_name, TRUE);
    gtk_tree_view_column_set_sort_column_id(c_name, DS_COL_NAME);
    gtk_tree_view_column_set_sort_indicator(c_name, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c_name);

    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c_desc = gtk_tree_view_column_new_with_attributes("Description", r2, "text", DS_COL_DESC, NULL);
    gtk_tree_view_column_set_resizable(c_desc, TRUE);
    gtk_tree_view_column_set_sort_column_id(c_desc, DS_COL_DESC);
    gtk_tree_view_column_set_sort_indicator(c_desc, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c_desc);

    GtkCellRenderer *r3 = gtk_cell_renderer_text_new();
    g_object_set(r3, "xalign", 1.0, NULL); /* alinhar Size à direita */
    GtkTreeViewColumn *c_size = gtk_tree_view_column_new_with_attributes("Size", r3, "text", DS_COL_SIZE, NULL);
    gtk_tree_view_column_set_resizable(c_size, TRUE);
    gtk_tree_view_column_set_sort_column_id(c_size, DS_COL_SIZE);
    gtk_tree_view_column_set_sort_indicator(c_size, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c_size);

    DatasetsUI *dui = g_new0(DatasetsUI, 1);
    dui->stack = GTK_STACK(stack);
    dui->list.tv = GTK_TREE_VIEW(tv);
    dui->list.store = store;

    /* ativação = abrir detalhes */
    g_signal_connect(tv, "row-activated", G_CALLBACK(on_ds_row_activated), dui);

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

    /* card envolvendo infos + botões*/
    GtkWidget *card_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *card = wrap_CSS(DATASETS_CSS, "ds-details-card", card_box, "ds_details_card");
    gtk_box_pack_start(GTK_BOX(details), card, FALSE, FALSE, 0);

    /* Info grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    GtkWidget *l_user = gtk_label_new("User/Owner:");
    GtkWidget *l_size = gtk_label_new("Size:");
    GtkWidget *l_link = gtk_label_new("Download Link:");
    GtkWidget *l_desc = gtk_label_new("Description:");

    GtkWidget *lbl_user_inner = gtk_label_new("—");
    GtkWidget *v_user_event = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(v_user_event), FALSE);
    gtk_container_add(GTK_CONTAINER(v_user_event), lbl_user_inner);
    gtk_widget_set_tooltip_text(v_user_event, "Open User Profile");
    {
        /* cauda “(abrir perfil)” para indicar que é clicável */
        GtkWidget *user_row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *user_hint = gtk_label_new("(Open Profile)");
        gtk_widget_set_name(user_hint, "user-link-hint");
        gtk_box_pack_start(GTK_BOX(user_row_box), v_user_event, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(user_row_box), user_hint, FALSE, FALSE, 0);

        /* linha User/Owner */
            int r = 0;
            gtk_grid_attach(GTK_GRID(grid), l_user, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), user_row_box, 1, r++, 1, 1);

            /* Size */
            GtkWidget *v_size = gtk_label_new("—");
            gtk_grid_attach(GTK_GRID(grid), l_size, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), v_size, 1, r++, 1, 1);

            /* (Rows) — NÃO anexamos à grid para sumir da UI */
            GtkWidget *v_rows = gtk_label_new("—"); /* mantemos o widget para não quebrar código antigo */

            /* Download Link */
            GtkWidget *v_link = gtk_label_new(NULL);
            gtk_label_set_use_markup(GTK_LABEL(v_link), TRUE);
            gtk_label_set_selectable(GTK_LABEL(v_link), TRUE);
            gtk_widget_set_name(v_link, "download-link");
            gtk_grid_attach(GTK_GRID(grid), l_link, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), v_link, 1, r++, 1, 1);

            /* Description */
            GtkWidget *v_desc = gtk_label_new("—");
            gtk_label_set_line_wrap(GTK_LABEL(v_desc), TRUE);
            gtk_label_set_xalign(GTK_LABEL(v_desc), 0.0);
            gtk_grid_attach(GTK_GRID(grid), l_desc, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), v_desc, 1, r++, 1, 1);


            gtk_box_pack_start(GTK_BOX(card_box), grid, FALSE, FALSE, 0);

            /* Botões (dentro do card) */
            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            GtkWidget *btn_import = gtk_button_new_with_label("Import to Environment");
            gtk_box_pack_end(GTK_BOX(actions), btn_import, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(card_box), actions, FALSE, FALSE, 0);

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
            DatasetsUI *dui_local = dui;
            dui_local->user_event = v_user_event;
            dui_local->stack = GTK_STACK(stack);
            dui_local->title_label = title;
            dui_local->lbl_user = GTK_LABEL(lbl_user_inner);
            dui_local->lbl_size = GTK_LABEL(v_size);
            dui_local->lbl_rows = GTK_LABEL(v_rows); 
            dui_local->lbl_link = GTK_LABEL(v_link);
            dui_local->lbl_desc = GTK_LABEL(v_desc);

            g_object_set_data_full(G_OBJECT(entry), "datasets-ui", dui, g_free);
            
            /* estilos “link”/“mãozinha” no nome do usuário */
            gtk_style_context_add_class(gtk_widget_get_style_context(v_user_event), "eventbox-clickable");
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_user_inner), "link");

            /* Signals */
            /* conectar o botão import para usar o dui (detalhes atuais) */
            g_signal_connect(btn_import, "clicked", G_CALLBACK(on_import_to_environment), dui_local);
            g_signal_connect(v_user_event, "button-press-event", G_CALLBACK(on_user_clicked), NULL);
            g_signal_connect(btn_refresh, "clicked", G_CALLBACK(refresh_datasets_cb), ctx);
            g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_to_list_clicked), dui_local->stack);

            /* aplica “mãozinha” em todos os botões/eventboxes da aba de datasets */
            hand_cursor_forall(outer, NULL);
    
            gtk_widget_show_all(outer);

            /* First fill */
            refresh_datasets_cb(NULL, ctx);

            return ctx;
    }
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

/* === Helper específicos da tela de detalhes === */
/* Monta o markup do link de download (azul sublinhado, mostrando só o basename). */
static gchar* make_download_link_markup(const char *url) {
    if (!url || !*url) return g_strdup("—");
    const char *disp = strrchr(url, '/');
    disp = disp ? disp + 1 : url;
    return g_markup_printf_escaped(
        "<a href=\"%s\"><span foreground=\"#0645ad\" underline=\"single\">%s</span></a>",
        url, disp
    );
}

/* --- SEARCH / FILTER helpers --- */
static gboolean str_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return TRUE;
    if (!hay || !*hay) return FALSE;
    gchar *a = g_utf8_casefold(hay, -1);
    gchar *b = g_utf8_casefold(needle, -1);
    gboolean ok = (a && b) ? (g_strstr_len(a, -1, b) != NULL) : FALSE;
    g_free(a); g_free(b);
    return ok;
}

/* todas as palavras (separadas por espaço) devem aparecer em pelo menos um dos campos */
static gboolean search_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
    GtkEntry *entry = GTK_ENTRY(user_data);
    const char *q = gtk_entry_get_text(entry);
    if (!q || !*q) return TRUE;

    gchar *name=NULL, *desc=NULL, *size=NULL;
    gtk_tree_model_get(model, iter,
        DS_COL_NAME, &name,
        DS_COL_DESC, &desc,
        DS_COL_SIZE, &size, -1);

    gboolean visible = TRUE;
    gchar **tokens = g_strsplit(q, " ", -1);
    for (int i=0; tokens && tokens[i]; ++i) {
        const char *t = tokens[i];
        if (!t || !*t) continue;
        if (!(str_contains_ci(name, t) || str_contains_ci(desc, t) || str_contains_ci(size, t))) {
            visible = FALSE; break;
        }
    }
    g_strfreev(tokens);
    g_free(name); g_free(desc); g_free(size);
    return visible;
}

static void on_search_changed(GtkEditable *e, gpointer user_data) {
    GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER(g_object_get_data(G_OBJECT(e), "ds-filter"));
    if (filter) gtk_tree_model_filter_refilter(filter);
}

static void on_search_activate(GtkEntry *e, gpointer user_data) {
    (void)user_data;
    DatasetsUI *dui = (DatasetsUI*) g_object_get_data(G_OBJECT(e), "datasets-ui");
    if (!dui || !GTK_IS_TREE_VIEW(dui->list.tv)) return;

    GtkTreeModel *m = gtk_tree_view_get_model(dui->list.tv);
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(m, &it)) return;

    int count = 0;
    GtkTreeIter first = {0};
    do {
        if (count == 0) first = it;
        count++;
    } while (gtk_tree_model_iter_next(m, &it));

    if (count == 1) {
        gchar *name=NULL,*desc=NULL,*size=NULL;
        GHashTable *meta=NULL;
        gtk_tree_model_get(m, &first,
            DS_COL_NAME, &name,
            DS_COL_DESC, &desc,
            DS_COL_SIZE, &size,
            DS_COL_META, &meta, -1);
        gchar *mk = g_markup_printf_escaped("<b>%s</b>", name ? name : "Dataset");
        fill_details_from_meta(dui, meta, mk);
        g_free(mk); g_free(name); g_free(desc); g_free(size);
    }
}

/* clique no ícone secundário (x) limpa a busca */
static void on_search_icon_press(GtkEntry *e, GtkEntryIconPosition pos, GdkEvent *ev, gpointer u) {
    if (pos == GTK_ENTRY_ICON_SECONDARY) {
        gtk_entry_set_text(e, "");
        on_search_changed(GTK_EDITABLE(e), NULL);
    }
}

static void on_clear_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkEntry *e = GTK_ENTRY(user_data);
    gtk_entry_set_text(e, "");
    on_search_changed(GTK_EDITABLE(e), NULL);  /* refiltra a lista */
    gtk_widget_grab_focus(GTK_WIDGET(e));
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

/* Find the right-notebook page flagged as our unique preview tab. */
static gint find_preview_page(GtkNotebook *nb) {
    if (!nb) return -1;
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; ++i) {
        GtkWidget *child = gtk_notebook_get_nth_page(nb, i);
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "is-preview-page")) == 1)
            return i;
    }
    return -1;
}

/* Make sure both ends are (re)wired. Safe to call anytime. */
static void ensure_preview_dnd_ready(EnvCtx *ctx) {
    if (!ctx) return;
    enable_drop_on_env_entries(ctx);
    if (ctx->ds_preview_tv && GTK_IS_TREE_VIEW(ctx->ds_preview_tv)) {
        wire_treeview_headers_for_dnd(ctx, ctx->ds_preview_tv);
    }
}

/* When you switch pages in the right notebook, if it's the preview page, rewire. */
static void on_right_nb_switch_page(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    GtkWidget *child;
    GtkWidget *tab;
    gboolean is_preview = FALSE;

    (void)page;
    if (!ctx || !nb) return;

    child = gtk_notebook_get_nth_page(nb, page_num);
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "is-preview-page")) == 1) {
        is_preview = TRUE;
    } else {
        tab = gtk_notebook_get_tab_label(nb, child);
        if (GTK_IS_LABEL(tab)) {
            const gchar *txt = gtk_label_get_text(GTK_LABEL(tab));
            if (txt && g_strcmp0(txt, "Preview dataset") == 0) is_preview = TRUE;
        }
    }
    if (is_preview) ensure_preview_dnd_ready(ctx);
}

/* If columns are reconfigured (sorting, autosize, etc.), headers get rebuilt: rewire. */
static void on_tv_columns_changed(GtkTreeView *tv, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    if (tv == ctx->ds_preview_tv) wire_treeview_headers_for_dnd(ctx, tv);
}

static void start_load_file(EnvCtx *ctx, const char *path) {
    if (!ctx || !path || !*path) return;

    if (ctx->current_dataset_path) g_free(ctx->current_dataset_path);
    ctx->current_dataset_path = g_strdup(path);

    if (ctx->status)   gtk_label_set_text(ctx->status, "Loading…");
    if (ctx->progress) gtk_progress_bar_pulse(ctx->progress);

    {
        gint page_idx = find_preview_page(ctx->right_nb);
        if (!ctx->ds_preview_tv || page_idx < 0) {
            GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
            GtkWidget *tvw = gtk_tree_view_new();
            GtkWidget *placeholder = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gint page;

            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
                                           GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_container_add(GTK_CONTAINER(sc), tvw);
            gtk_box_pack_start(GTK_BOX(page_box), sc, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(page_box), placeholder, FALSE, FALSE, 0);

            ctx->ds_preview_tv = GTK_TREE_VIEW(tvw);
            g_object_set_data(G_OBJECT(tvw), "picker-parent", page_box);
            g_object_set_data(G_OBJECT(page_box), "is-preview-page", GINT_TO_POINTER(1));

            page = gtk_notebook_append_page(ctx->right_nb, page_box, gtk_label_new("Preview dataset"));
            gtk_widget_show_all(page_box);
            gtk_notebook_set_current_page(ctx->right_nb, page);

            /* columns-changed rewire hook — connect once per TreeView instance */
            if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ctx->ds_preview_tv), "columns-hook"))) {
                g_signal_connect(ctx->ds_preview_tv, "columns-changed",
                                 G_CALLBACK(on_tv_columns_changed), ctx);
                g_object_set_data(G_OBJECT(ctx->ds_preview_tv), "columns-hook", GINT_TO_POINTER(1));
            }
        } else {
            gtk_notebook_set_current_page(ctx->right_nb, page_idx);
        }
    }

    /* Ensure DnD is alive immediately after showing the page */
    ensure_preview_dnd_ready(ctx);

    /* Hook notebook switch-page once to rewire whenever user returns to preview */
    if (ctx->right_nb && !GPOINTER_TO_INT(g_object_get_data(G_OBJECT(ctx->right_nb), "dnd-switch-hook"))) {
        g_signal_connect(ctx->right_nb, "switch-page", G_CALLBACK(on_right_nb_switch_page), ctx);
        g_object_set_data(G_OBJECT(ctx->right_nb), "dnd-switch-hook", GINT_TO_POINTER(1));
    }

    /* Launch worker and tell it where to render */
    {
        LoadTaskData *td = g_new0(LoadTaskData, 1);
        GTask *t;

        td->path = g_strdup(path);
        td->target_tv = ctx->ds_preview_tv;

        t = g_task_new(NULL, NULL, on_task_done, ctx);
        g_task_set_task_data(t, td, (GDestroyNotify)free_load_task_data);
        g_task_run_in_thread(t, task_read_preview);
        g_object_unref(t);
    }
}


static void on_load_selected_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    const gchar *path = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ctx->ds_combo));
    if (!path) {
        /* Fallback for legacy entries that were appended as plain text */
        gchar *txt = gtk_combo_box_text_get_active_text(ctx->ds_combo);
        if (!txt) {
            if (ctx->status) gtk_label_set_text(ctx->status, "No dataset selected");
            return;
        }
        path = txt; /* use text as path for old entries */
        if (ctx->status) gtk_label_set_text(ctx->status, "Loading…");
        start_load_file(ctx, path);
        g_free(txt);
        return;
    }

    if (ctx->status) gtk_label_set_text(ctx->status, "Loading…");
    start_load_file(ctx, path);
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
        gchar *base = g_path_get_basename(path);
        /* display = basename; id = full path */
        gtk_combo_box_text_append(ctx->ds_combo, path, base);
        g_free(base);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(ctx->ds_combo), path);
    }
    if (ctx->status) gtk_label_set_text(ctx->status, "Ready to load");
    g_free(path);
}


static void on_refresh_local_datasets(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    gtk_combo_box_text_remove_all(ctx->ds_combo);

    gchar *cwd = g_get_current_dir();
    gchar *datasets_dir = g_build_filename(cwd, "datasets", NULL);
    if (g_file_test(datasets_dir, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(datasets_dir, 0, NULL);
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            if (g_str_has_suffix(name, ".csv") || g_str_has_suffix(name, ".tsv")) {
                gchar *full = g_build_filename(datasets_dir, name, NULL);
                /* display = basename; id = full path */
                gtk_combo_box_text_append(ctx->ds_combo, full, name);
                g_free(full);
            }
        }
        g_dir_close(dir);
    }
    g_free(datasets_dir);
    g_free(cwd);

    /* select first item if any */
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(ctx->ds_combo));
    if (m && gtk_tree_model_iter_n_children(m, NULL) > 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), 0);
}


#endif