/* debug_window.h - terminal-like debug/backlog window for GTK
 *
 * Behavior:
 *   - Type commands in the input area (multiline).
 *   - Press Enter to send (Shift+Enter -> newline).
 *   - Use Up/Down to travel command history.
 *   - Ctrl+L clears backlog.
 *
 * Callback prototype: char* cb(const char *cmd_utf8) -> returns malloc'd UTF-8 string.
 * If no callback is set, it will call run_api_command(WCHAR*).
 */
#include <gtk/gtk.h>
#include <stdarg.h>
#include <time.h>
#include <wchar.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include "../css/css.h" 
#include "context.h"


#ifndef DEBUG_WINDOW_H
#define DEBUG_WINDOW_H


#ifdef __cplusplus
extern "C" {
#endif

/* external prototype (from your communicator) */
extern WCHAR* run_api_command(const WCHAR *command);

/* callback type */
typedef char* (*debug_command_cb_t)(const char *cmd_utf8);

/* public API */
gboolean debug_window_create(GtkWindow *parent);
void debug_window_show(void);
void debug_log(const char *fmt, ...);
void debug_set_command_callback(debug_command_cb_t cb);

/* --- implementation --- */
#define DEBUG_MAX_HISTORY 256
#define DEBUG_MAX_LINE    8192

typedef struct {
    GtkWidget *window;
    GtkWidget *backlog_scroller;
    GtkTextView *backlog_view;    /* output area (read-only) */
    GtkWidget *cmd_scroller;
    GtkTextView *cmd_view;        /* input area - terminal-like */
    GtkWidget *clear_btn;
    debug_command_cb_t custom_cb;
    GtkWindow *parent;

    /* history */
    char *history[DEBUG_MAX_HISTORY];
    int history_len;
    int history_pos; /* current position when navigating: 0..history_len (history_len = empty) */
} DebugWinCtx;

static DebugWinCtx g_debug_ctx = {0};

/* now helper */
static void _now_str(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

/* helper local: alterna maximizar/restaurar (sem outras dependências) */
static void debug_titlebar_on_max_clicked(GtkButton *btn, gpointer win_) {
    (void)btn;
    GtkWindow *win = GTK_WINDOW(win_);
    if (gtk_window_is_maximized(win)) gtk_window_unmaximize(win);
    else                               gtk_window_maximize(win);
}

static void install_w95_titlebar_debug(GtkWindow *win) {
    GtkWidget *hb = gtk_header_bar_new();
    gtk_widget_set_name(hb, "w95-titlebar");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), FALSE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), NULL);

    /* ESQUERDA: logo + título */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GdkPixbuf *pb_logo =
        gdk_pixbuf_new_from_file_at_scale("assets/AI-for-dummies.png", 20, 20, TRUE, NULL);
    GtkWidget *logo = gtk_image_new_from_pixbuf(pb_logo);
    g_object_unref(pb_logo);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), logo, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new("AI for Dummies");
    gtk_widget_set_name(title, "w95-title");
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), left);

    /* DIREITA: [min] [max] [close] com PNGs fixos */
    GtkWidget *btn_min   = gtk_button_new();
    GtkWidget *btn_max   = gtk_button_new();
    GtkWidget *btn_close = gtk_button_new();

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_min),   "win95");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_max),   "win95");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "win95");

    GdkPixbuf *pb_min   = gdk_pixbuf_new_from_file_at_scale("assets/minimize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_max   = gdk_pixbuf_new_from_file_at_scale("assets/maximize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_close = gdk_pixbuf_new_from_file_at_scale("assets/close.png",    12, 12, TRUE, NULL);

    gtk_button_set_image(GTK_BUTTON(btn_min),   gtk_image_new_from_pixbuf(pb_min));
    gtk_button_set_image(GTK_BUTTON(btn_max),   gtk_image_new_from_pixbuf(pb_max));
    gtk_button_set_image(GTK_BUTTON(btn_close), gtk_image_new_from_pixbuf(pb_close));
    gtk_button_set_always_show_image(GTK_BUTTON(btn_min),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_max),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_close), TRUE);

    g_object_unref(pb_min); g_object_unref(pb_max); g_object_unref(pb_close);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_close);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_max);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_min);

    /* sinais */
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_hide), win);
    g_signal_connect_swapped(btn_min,   "clicked", G_CALLBACK(gtk_window_iconify), win);
    g_signal_connect        (btn_max,   "clicked", G_CALLBACK(debug_titlebar_on_max_clicked), win);

    gtk_window_set_titlebar(win, hb);
}

/* Ao tentar fechar pela decoração/Alt+F4: só esconde */
static gboolean _on_debug_delete(GtkWidget *w, GdkEvent *e, gpointer user_data) {
    (void)e; (void)user_data;
    gtk_widget_hide(w);
    return TRUE;  // impede destruir
}

/* Se por algum motivo destruir acontecer, zere o ponteiro global */
static void _on_debug_destroy(GtkWidget *w, gpointer user_data) {
    (void)w; (void)user_data;
    g_debug_ctx.window = NULL;
}

/* schedule append via idle (thread-safe) */
static gboolean _idle_append_cb(gpointer data) {
    char *txt = (char*)data;
    if (txt) {
        if (g_debug_ctx.backlog_view) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.backlog_view);
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buf, &end);
            gtk_text_buffer_insert(buf, &end, txt, -1);
            gtk_text_buffer_insert(buf, &end, "\n", 1);

            /* auto-scroll */
            GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(g_debug_ctx.backlog_scroller));
            if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
        }
        free(txt);
    }
    return FALSE;
}

static char* communicator_debug_wrapper(const char *cmd_utf8) {
    if (!cmd_utf8) return NULL;

    /* converte UTF-8 -> WCHAR */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    WCHAR *wcmd = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    if (!wcmd) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);

    /* chama communicator (run_api_command) */
    WCHAR *wresp = run_api_command(wcmd);

    free(wcmd);

    if (!wresp) return NULL;

    /* converte WCHAR -> UTF-8 */
    int outlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    if (outlen <= 0) { free(wresp); return NULL; }
    char *resp = (char*)malloc(outlen);
    if (!resp) { free(wresp); return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, outlen, NULL, NULL);

    /* libera buffer WCHAR retornado por run_api_command (se alocado com malloc) */
    free(wresp);

    return resp; /* caller (debug_window) deve free() */
}

/* Wrapper público para abrir a janela de debug a partir de qualquer lugar */
void show_debug_window(GtkWindow *parent) {
    /* Garante que a janela exista/foi criada */
    debug_window_create(parent);

    /* Se você quer que os comandos passem pelo communicator: */
    debug_set_command_callback(communicator_debug_wrapper);

    /* Exibe e dá foco no input */
    debug_window_show();
}

static void on_debug_button_clicked(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ectx = (EnvCtx*)user;
    debug_window_create(GTK_WINDOW(ectx->main_window));
    debug_set_command_callback(communicator_debug_wrapper);
    debug_window_show();
}



/* append (main thread) */
static void _backlog_append(const char *txt) {
    if (!g_debug_ctx.backlog_view || !txt) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.backlog_view);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, txt, -1);
    gtk_text_buffer_insert(buf, &end, "\n", 1);

    GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(g_debug_ctx.backlog_scroller));
    if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}

/* thread-safe logging entry point */
void debug_log(const char *fmt, ...) {
    char line[DEBUG_MAX_LINE];
    char ts[64];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    _now_str(ts, sizeof ts);

    size_t outsz = strlen(ts) + 3 + strlen(line) + 1;
    char *out = (char*)malloc(outsz);
    if (!out) return;
    snprintf(out, outsz, "[%s] %s", ts, line);

    /* if we're in main thread, append directly; otherwise schedule */
    if (g_main_context_is_owner(g_main_context_default())) {
        _backlog_append(out);
        free(out);
    } else {
        g_idle_add(_idle_append_cb, out);
    }
}

/* history push */
static void _history_push(const char *cmd) {
    if (!cmd || !*cmd) return;
    /* ignore consecutive duplicates */
    if (g_debug_ctx.history_len > 0) {
        const char *last = g_debug_ctx.history[g_debug_ctx.history_len - 1];
        if (last && strcmp(last, cmd) == 0) return;
    }
    if (g_debug_ctx.history_len < DEBUG_MAX_HISTORY) {
        g_debug_ctx.history[g_debug_ctx.history_len++] = strdup(cmd);
    } else {
        /* drop oldest */
        free(g_debug_ctx.history[0]);
        memmove(&g_debug_ctx.history[0], &g_debug_ctx.history[1], sizeof(char*) * (DEBUG_MAX_HISTORY - 1));
        g_debug_ctx.history[DEBUG_MAX_HISTORY - 1] = strdup(cmd);
    }
    g_debug_ctx.history_pos = g_debug_ctx.history_len; /* reset pos to "empty" */
}

/* get/set text in input TextView */
static char* _cmdview_get_text(void) {
    if (!g_debug_ctx.cmd_view) return NULL;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.cmd_view);
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    char *txt = gtk_text_buffer_get_text(buf, &start, &end, FALSE); /* returns g_malloc'd */
    char *res = NULL;
    if (txt) {
        res = strdup(txt);
        g_free(txt);
    }
    return res;
}

static void _cmdview_set_text(const char *txt) {
    if (!g_debug_ctx.cmd_view) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.cmd_view);
    gtk_text_buffer_set_text(buf, txt ? txt : "", -1);
    /* place cursor at end */
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_place_cursor(buf, &end);
}

/* default sender: use run_api_command(WCHAR*) */
static char* _default_send_command(const char *cmd_utf8) {
    if (!cmd_utf8) return NULL;
    /* if user set custom cb, prefer it */
    if (g_debug_ctx.custom_cb) return g_debug_ctx.custom_cb(cmd_utf8);

    int wchar_size = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    if (wchar_size <= 0) return NULL;
    WCHAR *wcmd = (WCHAR*)malloc(wchar_size * sizeof(WCHAR));
    if (!wcmd) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wchar_size);

    WCHAR *wresp = NULL;
    char *ret = NULL;
    if (&run_api_command) {
        wresp = run_api_command(wcmd);
    }
    free(wcmd);
    if (!wresp) return NULL;

    int out_len = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    if (out_len <= 0) { free(wresp); return NULL; }
    ret = (char*)malloc(out_len);
    if (!ret) { free(wresp); return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, ret, out_len, NULL, NULL);
    free(wresp);
    return ret;
}

/* execute command and show result in backlog */
static void _do_send_command(const char *cmd) {
    if (!cmd) return;
    /* trim trailing whitespace */
    size_t L = strlen(cmd);
    while (L > 0 && (cmd[L-1] == '\n' || cmd[L-1] == '\r' || cmd[L-1] == ' ' || cmd[L-1] == '\t')) L--;
    if (L == 0) return;
    char *trim = (char*)malloc(L + 1);
    if (!trim) return;
    memcpy(trim, cmd, L); trim[L] = '\0';

    char ts[64]; _now_str(ts, sizeof ts);
    char header[512];
    snprintf(header, sizeof header, "[%s] > %s", ts, trim);

    /* append header on main loop */
    char *dup_header = strdup(header);
    g_idle_add(_idle_append_cb, dup_header);

    /* record history */
    _history_push(trim);

    /* call handler (may block) */
    char *resp = NULL;
    if (g_debug_ctx.custom_cb) {
        resp = g_debug_ctx.custom_cb(trim);
    } else {
        resp = _default_send_command(trim);
    }

    if (resp) {
        char *dup_resp = strdup(resp);
        g_idle_add(_idle_append_cb, dup_resp);
        free(resp);
    } else {
        char *err = strdup("[erro ao executar comando]");
        g_idle_add(_idle_append_cb, err);
    }

    free(trim);
}

/* clear backlog button */
static void _on_clear_clicked(GtkButton *b, gpointer user) {
    (void)b; (void)user;
    if (!g_debug_ctx.backlog_view) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.backlog_view);
    gtk_text_buffer_set_text(buf, "", -1);
}

/* helper: set input text from history */
static void _history_apply_pos(void) {
    if (g_debug_ctx.history_pos < 0 || g_debug_ctx.history_pos > g_debug_ctx.history_len) return;
    if (g_debug_ctx.history_pos == g_debug_ctx.history_len) {
        _cmdview_set_text("");
    } else {
        _cmdview_set_text(g_debug_ctx.history[g_debug_ctx.history_pos]);
    }
}

/* key-press handler for cmd_view:
   - Enter without Shift -> send
   - Shift+Enter -> newline
   - Up / Down -> history navigation
   - Ctrl+L -> clear backlog
*/
static gboolean _on_cmd_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user) {
    (void)widget; (void)user;

    guint key = event->keyval;
    GdkModifierType mods = event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK);

    if (key == GDK_KEY_Return) {
        if (!(mods & GDK_SHIFT_MASK)) {
            /* send entire input */
            char *txt = _cmdview_get_text();
            if (txt) {
                _do_send_command(txt);
                _cmdview_set_text("");
                free(txt);
            }
            return TRUE; /* prevent inserting newline */
        } else {
            /* allow newline insertion */
            return FALSE;
        }
    }
    else if (key == GDK_KEY_Up && !(mods & GDK_CONTROL_MASK) && !(mods & GDK_MOD1_MASK)) {
        if (g_debug_ctx.history_len == 0) return FALSE;
        if (g_debug_ctx.history_pos > 0) {
            g_debug_ctx.history_pos--;
        } else {
            g_debug_ctx.history_pos = 0;
        }
        _history_apply_pos();
        return TRUE;
    }
    else if (key == GDK_KEY_Down && !(mods & GDK_CONTROL_MASK) && !(mods & GDK_MOD1_MASK)) {
        if (g_debug_ctx.history_len == 0) return FALSE;
        if (g_debug_ctx.history_pos < g_debug_ctx.history_len) {
            g_debug_ctx.history_pos++;
            if (g_debug_ctx.history_pos > g_debug_ctx.history_len) g_debug_ctx.history_pos = g_debug_ctx.history_len;
        }
        _history_apply_pos();
        return TRUE;
    }
    else if ((mods & GDK_CONTROL_MASK) && (key == GDK_KEY_l || key == GDK_KEY_L)) {
        /* Ctrl+L = clear backlog */
        if (g_debug_ctx.backlog_view) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(g_debug_ctx.backlog_view);
            gtk_text_buffer_set_text(buf, "", -1);
        }
        return TRUE;
    }

    return FALSE; /* let GTK handle other keys (inserting text etc) */
}

/* create window (only once) */
gboolean debug_window_create(GtkWindow *parent) {
    if (g_debug_ctx.window) return TRUE;

    g_debug_ctx.parent = parent;

    g_debug_ctx.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(g_debug_ctx.window), 900, 480);
    gtk_window_set_title(GTK_WINDOW(g_debug_ctx.window), "Debug / Terminal");
    /* Torne independente do parent (não minimize em grupo) */
    gtk_window_set_transient_for(GTK_WINDOW(g_debug_ctx.window), NULL);
    gtk_window_set_modal(GTK_WINDOW(g_debug_ctx.window), FALSE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(g_debug_ctx.window), FALSE);
    /* mostre no taskbar normalmente */
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(g_debug_ctx.window), FALSE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(g_debug_ctx.window), FALSE);

    /* Eventos de fechar/limpar ponteiro */
    g_signal_connect(g_debug_ctx.window, "delete-event",
                    G_CALLBACK(_on_debug_delete), NULL);
    g_signal_connect(g_debug_ctx.window, "destroy",
                    G_CALLBACK(_on_debug_destroy), NULL);
    install_w95_titlebar_debug(GTK_WINDOW(g_debug_ctx.window));
    /* nome do topo para o CSS */
    gtk_widget_set_name(g_debug_ctx.window, "debug-window");

    /* carrega o CSS deste módulo */
    const char *DEBUG_CSS = parse_CSS_file("debug.css");
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, DEBUG_CSS ? DEBUG_CSS : "", -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(prov);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(vbox, "debug-panel");
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(g_debug_ctx.window), vbox);

    /* backlog area */
    g_debug_ctx.backlog_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(g_debug_ctx.backlog_view, FALSE);
    gtk_text_view_set_cursor_visible(g_debug_ctx.backlog_view, FALSE);
    g_debug_ctx.backlog_scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_debug_ctx.backlog_scroller), "sunken");
    gtk_widget_set_vexpand(g_debug_ctx.backlog_scroller, TRUE);
    gtk_container_add(GTK_CONTAINER(g_debug_ctx.backlog_scroller), GTK_WIDGET(g_debug_ctx.backlog_view));
    gtk_box_pack_start(GTK_BOX(vbox), g_debug_ctx.backlog_scroller, TRUE, TRUE, 0);

    /* command input scroller + view */
    g_debug_ctx.cmd_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(g_debug_ctx.cmd_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(g_debug_ctx.cmd_view), TRUE);
    gtk_text_view_set_left_margin(g_debug_ctx.cmd_view, 6);
    gtk_text_view_set_right_margin(g_debug_ctx.cmd_view, 6);
    gtk_widget_set_size_request(GTK_WIDGET(g_debug_ctx.cmd_view), -1, 100); 

    g_debug_ctx.cmd_scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_debug_ctx.cmd_scroller), "sunken");
    gtk_widget_set_vexpand(g_debug_ctx.cmd_scroller, FALSE);
    gtk_container_add(GTK_CONTAINER(g_debug_ctx.cmd_scroller), GTK_WIDGET(g_debug_ctx.cmd_view));
    gtk_box_pack_start(GTK_BOX(vbox), g_debug_ctx.cmd_scroller, FALSE, FALSE, 0);

    /* buttons row: only clear backlog */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_debug_ctx.clear_btn = gtk_button_new_with_label("Clear Backlog");
    gtk_box_pack_end(GTK_BOX(hbox), g_debug_ctx.clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* signals */
    g_signal_connect(g_debug_ctx.clear_btn, "clicked", G_CALLBACK(_on_clear_clicked), NULL);
    g_signal_connect(GTK_WIDGET(g_debug_ctx.cmd_view), "key-press-event", G_CALLBACK(_on_cmd_key_press), NULL);

    /* initialize history */
    g_debug_ctx.history_len = 0;
    g_debug_ctx.history_pos = 0;
    for (int i=0;i<DEBUG_MAX_HISTORY;++i) g_debug_ctx.history[i] = NULL;

    gtk_widget_show_all(g_debug_ctx.window);
    gtk_widget_hide(g_debug_ctx.window);
    return TRUE;
}

void debug_window_show(void) {
    if (!g_debug_ctx.window) return;
    gtk_window_present(GTK_WINDOW(g_debug_ctx.window));
    gtk_widget_show_all(g_debug_ctx.window);
    if (g_debug_ctx.cmd_view)
        gtk_widget_grab_focus(GTK_WIDGET(g_debug_ctx.cmd_view)); 
}

void debug_set_command_callback(debug_command_cb_t cb) {
    g_debug_ctx.custom_cb = cb;
}

/* cleanup helper (if you want to call on app exit) */
static void debug_window_cleanup(void) {
    int i;
    if (g_debug_ctx.window) {
        gtk_widget_destroy(g_debug_ctx.window);
        g_debug_ctx.window = NULL;
    }
    for (i=0;i<g_debug_ctx.history_len;++i) {
        free(g_debug_ctx.history[i]);
        g_debug_ctx.history[i] = NULL;
    }
    g_debug_ctx.history_len = 0;
    g_debug_ctx.history_pos = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_WINDOW_H */
