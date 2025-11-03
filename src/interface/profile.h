// profile.h

#include <gtk/gtk.h>
#include <cjson/cJSON.h>
#include <string.h>
#include "../css/css.h"
#include "debug_window.h"
#include "../backend/communicator.h"
#include "context.h"
#include <math.h>

#ifndef PROFILE_H
#define PROFILE_H

#define AVATAR_SIZE 192

#ifdef _WIN32
  #include <windows.h>
#else
  #include <wchar.h>
#endif

/* Estrutura para gerenciar a stack na janela de perfi */
typedef struct {
    GtkStack *stack;
    GtkWidget *profile_page;
    GtkWidget *dataset_page;
    GtkWindow *parent_window;
    GtkImage *avatar_image;
    GtkWidget *avatar_box;
} ProfileWindowUI;

typedef struct {
    ProfileWindowUI *pui;
    char *nome;
    char *descricao;
    char *tamanho;
    char *url;
    char *data_cadastro;
    char *usuario_nome;
    char *usuario_email;
} DatasetUIData;

/* Protótipos */
static void on_back_to_profile_clicked(GtkButton *btn, gpointer user_data);
static void on_dataset_info_clicked(GtkButton *btn, gpointer user_data);
static void install_env_w95_titlebar(GtkWindow *win, const char *title_text);
static GdkPixbuf* pixbuf_cover_square_(GdkPixbuf *src, int target);


/* Funções auxiliares para conversão de caracteres */
static WCHAR* utf8_to_wchar_alloc(const char *utf8) {
    if (!utf8) return NULL;
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0) return NULL;
    WCHAR *w = (WCHAR*)malloc(wide_len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wide_len);
    return w;
#else
    gunichar2 *utf16 = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    if (!utf16) return NULL;
    size_t wlen = g_utf16_len(utf16);
    WCHAR *w = (WCHAR*)malloc((wlen + 1) * sizeof(WCHAR));
    for (size_t i = 0; i < wlen; ++i) w[i] = (WCHAR)utf16[i];
    w[wlen] = 0;
    g_free(utf16);
    return w;
#endif
}
static void fit_avatar_box_(ProfileWindowUI *ctx, GdkPixbuf *pix){
    (void)pix;
    if (!ctx || !ctx->avatar_box) return;
    gtk_widget_set_size_request(ctx->avatar_box, AVATAR_SIZE, AVATAR_SIZE);
}

/* === Cursor “mãozinha” cross-GTK === */
#if GTK_CHECK_VERSION(4,0,0)
static void pf_set_hand_cursor(GtkWidget *w, gboolean on) {
    if (!w) return;
    GdkDisplay *dpy = gdk_display_get_default();
    GdkCursor  *cur = on ? gdk_cursor_new_from_name(dpy, "pointer") : NULL;
    gtk_widget_set_cursor(w, cur);
    if (cur) g_object_unref(cur);
}

static void pf_on_motion_enter(GtkEventControllerMotion *c, double x, double y, gpointer user_data) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));
    pf_set_hand_cursor(w, TRUE);
}
static void pf_on_motion_leave(GtkEventControllerMotion *c, gpointer user_data) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));
    pf_set_hand_cursor(w, FALSE);
}

static void pf_apply_hand_cursor_to(GtkWidget *w) {
    if (!w) return;
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(pf_on_motion_enter), NULL);
    g_signal_connect(motion, "leave", G_CALLBACK(pf_on_motion_leave), NULL);
    gtk_widget_add_controller(w, motion);
}
#else
/* GTK3 */
static gboolean pf_on_enter(GtkWidget *w, GdkEventCrossing *e, gpointer u) {
    (void)e; (void)u;
    if (!gtk_widget_get_window(w)) return FALSE;
    GdkDisplay *dpy = gdk_display_get_default();
    GdkCursor  *cur = gdk_cursor_new_from_name(dpy, "pointer");
    if (!cur) cur = gdk_cursor_new_for_display(dpy, GDK_HAND2);
    gdk_window_set_cursor(gtk_widget_get_window(w), cur);
    if (cur) g_object_unref(cur);
    return FALSE;
}
static gboolean pf_on_leave(GtkWidget *w, GdkEventCrossing *e, gpointer u) {
    (void)e; (void)u;
    if (gtk_widget_get_window(w)) gdk_window_set_cursor(gtk_widget_get_window(w), NULL);
    return FALSE;
}
static void pf_apply_hand_cursor_to(GtkWidget *w) {
    if (!w) return;
    gtk_widget_add_events(w, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(w, "enter-notify-event", G_CALLBACK(pf_on_enter), NULL);
    g_signal_connect(w, "leave-notify-event", G_CALLBACK(pf_on_leave), NULL);
}
#endif

#if GTK_CHECK_VERSION(4,0,0)
static void on_item_box_gesture_pressed(GtkGestureClick *g,
                                        int n_press, double x, double y,
                                        gpointer user_data)
{
    (void)n_press; (void)x; (void)y;
    ProfileWindowUI *pui = (ProfileWindowUI*)user_data;
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    const char *child = (const char*) g_object_get_data(G_OBJECT(w), "dataset_stack_name");
    if (pui && child && *child) {
        gtk_stack_set_visible_child_name(pui->stack, child);
    }
}
#endif


/* GTK3: aplica 'row-hover' no GtkListBoxRow e também no item_box (.dataset-row) interno */
static gboolean on_row_enter(GtkWidget *row, GdkEventCrossing *e, gpointer unused) {
    (void)e; (void)unused;
    if (!row) return FALSE;

    /* Classe no próprio row (para regras que estilizam o row) */
    gtk_style_context_add_class(gtk_widget_get_style_context(row), "row-hover");

    /* E TAMBÉM no conteúdo interno que tem .dataset-row (para pintar o fundo) */
    GtkWidget *child1 = NULL, *child2 = NULL;
    if (GTK_IS_BIN(row)) child1 = gtk_bin_get_child(GTK_BIN(row));           /* EventBox */
    if (child1 && GTK_IS_BIN(child1)) child2 = gtk_bin_get_child(GTK_BIN(child1)); /* item_box */

    if (child2) {
        gtk_style_context_add_class(gtk_widget_get_style_context(child2), "row-hover");
    } else if (child1) {
        gtk_style_context_add_class(gtk_widget_get_style_context(child1), "row-hover");
    }

    gtk_widget_queue_draw(row);
    debug_log("on_row_enter(): row=%p hover ON", row);
    return FALSE;
}

/* GTK3: sai do hover */
static gboolean on_row_leave(GtkWidget *row, GdkEventCrossing *e, gpointer unused) {
    (void)e; (void)unused;
    if (!row) return FALSE;

    gtk_style_context_remove_class(gtk_widget_get_style_context(row), "row-hover");

    GtkWidget *child1 = NULL, *child2 = NULL;
    if (GTK_IS_BIN(row)) child1 = gtk_bin_get_child(GTK_BIN(row));
    if (child1 && GTK_IS_BIN(child1)) child2 = gtk_bin_get_child(GTK_BIN(child1));

    if (child2) gtk_style_context_remove_class(gtk_widget_get_style_context(child2), "row-hover");
    else if (child1) gtk_style_context_remove_class(gtk_widget_get_style_context(child1), "row-hover");

    gtk_widget_queue_draw(row);
    return FALSE;
}

/* === Helpers para carregar e aplicar profile.css === */
static char* load_profile_css_() {
    const char *cands[] = {
        "css/profile.css",
        "./css/profile.css",
        "../css/profile.css",
        "profile.css"
    };
    for (guint i = 0; i < G_N_ELEMENTS(cands); ++i) {
        char *buf = parse_CSS_file(cands[i]);
        if (buf) {
            debug_log("[profile.css] carregado de: %s", cands[i]);
            return buf;
        }
    }
    debug_log("[profile.css] NÃO encontrado em caminhos padrão");
    return NULL;
}

/* Envolve um child e também registra o provider em PRIORITY_USER para vencer o tema global */
static GtkWidget* wrap_with_profile_css_(GtkWidget *child) {
    char *css = load_profile_css_();
    GtkWidget *wrapped = child;

    if (css) {
        /* Mantém seu wrapper (classe e name previsíveis) */
        wrapped = wrap_CSS(css, "profile-root", child, "profile-root");

        /* EXTRA: registra o MESMO CSS com prioridade USER p/ garantir override do tema */
    #if GTK_CHECK_VERSION(4,0,0)
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_data(prov, css, -1, NULL);
        GdkDisplay *disp = gdk_display_get_default();
        gtk_style_context_add_provider_for_display(disp,
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_unref(prov);
    #else
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_data(prov, css, -1, NULL);
        GdkScreen *screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(screen,
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_unref(prov);
    #endif

        free(css); /* wrap_CSS já aplicou; e acima carregamos denovo no provider USER */
    } else {
        debug_log("Seguindo sem CSS de perfil (provider não aplicado).");
    }
    return wrapped;
}

static GdkPixbuf* load_cover_from_file_(const char *path, int target, GError **err){
    if (!path) return NULL;
    GError *lerr = NULL;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(path, &lerr);
    if (!src){
        if (err) *err = lerr; else if (lerr) g_error_free(lerr);
        return NULL;
    }
    GdkPixbuf *out = pixbuf_cover_square_(src, target);
    g_object_unref(src);
    return out;
}

static void set_default_avatar_(ProfileWindowUI *ctx){
    if (!ctx || !ctx->avatar_image) return;
    const char *cands[] = {"./assets/default_avatar.png","./assets/default_avatar.png"};
    for (int i=0;i<2;i++){
        GError *err=NULL;
        GdkPixbuf *pix = load_cover_from_file_(cands[i], AVATAR_SIZE, &err);
        if (pix){
            gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
            fit_avatar_box_(ctx, pix);
            g_object_unref(pix);
            return;
        }
        if (err) g_error_free(err);
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(ctx->avatar_image), "avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(ctx->avatar_image), AVATAR_SIZE);
    fit_avatar_box_(ctx, NULL);
}


/* escala para cobrir AVATAR_SIZE x AVATAR_SIZE e recorta centro */
static GdkPixbuf* pixbuf_cover_square_(GdkPixbuf *src, int target){
    if (!src) return NULL;
    int sw = gdk_pixbuf_get_width(src);
    int sh = gdk_pixbuf_get_height(src);
    if (sw <= 0 || sh <= 0) return NULL;

    /* “cover”: escolhe o MAIOR fator de escala p/ cobrir todo o alvo */
    double scale = fmax((double)target / (double)sw, (double)target / (double)sh);
    int nw = (int)ceil(sw * scale);
    int nh = (int)ceil(sh * scale);

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(src, nw, nh, GDK_INTERP_BILINEAR);
    if (!scaled) return NULL;

    /* recorta quadrado central target x target */
    int x = (nw - target) / 2;
    int y = (nh - target) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    GdkPixbuf *out = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(scaled),
                                    gdk_pixbuf_get_has_alpha(scaled),
                                    gdk_pixbuf_get_bits_per_sample(scaled),
                                    target, target);
    if (out){
        gdk_pixbuf_copy_area(scaled, x, y, target, target, out, 0, 0);
    }
    g_object_unref(scaled);
    return out;
}




static char* wchar_to_utf8_alloc(const WCHAR *w) {
    if (!w) return NULL;
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return NULL;
    char *out = (char*)malloc(size);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL);
    return out;
#else
    size_t len = w ? wcslen((const wchar_t*)w) : 0;
    gunichar2 *utf16 = g_new(gunichar2, len + 1);
    for (size_t i = 0; i < len; ++i) utf16[i] = (gunichar2) ((const wchar_t*)w)[i];
    utf16[len] = 0;
    char *out = g_utf16_to_utf8(utf16, len, NULL, NULL, NULL);
    g_free(utf16);
    return out;
#endif
}

static void show_stack_child(ProfileWindowUI *pui, const char *child_name) {
    if (!pui || !child_name) return;
    if (GTK_IS_STACK(pui->stack)) {
        gtk_stack_set_visible_child_name(pui->stack, child_name);
        debug_log("show_stack_child(): set visible child to '%s' (pui=%p, stack=%p)", child_name, pui, pui ? pui->stack : NULL);
    }
}

/* Import dataset to environment handler
 * - lê a URL armazenada em "dataset-url" no botão
 * - extrai basename (após última '/')
 * - chama run_api_command("GET_DATASET <basename>") usando as conversões do projeto
 * - atualiza um label de status armazenado em "dataset_status_label" no próprio botão
 */
static void on_import_to_environment_profile(GtkButton *btn, gpointer user_data) {
    (void)user_data; /* não usado, mantido para compatibilidade */

    const char *url = (const char*) g_object_get_data(G_OBJECT(btn), "dataset-url");
    GtkLabel *lbl_status = (GtkLabel*) g_object_get_data(G_OBJECT(btn), "dataset_status_label");

    debug_log("on_import_to_environment(): clicked, url=%s, status_label=%p", url ? url : "(null)", lbl_status);

    if (!url || !*url) {
        if (lbl_status) gtk_label_set_text(lbl_status, "Nenhum link de dataset disponível.");
        else debug_log("on_import_to_environment(): no url available");
        return;
    }

    /* extrair basename após última '/' */
    const char *basename = strrchr(url, '/');
    if (basename && *(basename + 1) != '\0') {
        basename++; /* aponta para depois da barra */
    } else {
        basename = url; /* talvez já seja só o nome */
    }

    if (!basename || !*basename) {
        if (lbl_status) gtk_label_set_text(lbl_status, "Nome de arquivo inválido no link.");
        else debug_log("on_import_to_environment(): invalid basename");
        return;
    }

    /* montar comando UTF-8 */
    char cmd_utf8[1024];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "GET_DATASET %s", basename);

    debug_log("on_import_to_environment(): command -> %s", cmd_utf8);

    /* converter para WCHAR (usa utf8_to_wchar_alloc do teu arquivo) */
    WCHAR *wcmd = utf8_to_wchar_alloc(cmd_utf8);
    if (!wcmd) {
        if (lbl_status) gtk_label_set_text(lbl_status, "Erro de conversão (UTF-8 -> WCHAR).");
        else debug_log("on_import_to_environment(): utf8_to_wchar_alloc failed");
        return;
    }

    /* chamada bloqueante à API (como no resto do teu código) */
    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);

    if (!wresp) {
        if (lbl_status) gtk_label_set_text(lbl_status, "Falha: sem resposta da API.");
        else debug_log("on_import_to_environment(): run_api_command returned NULL");
        return;
    }

    /* converter resposta para UTF-8 */
    char *resp = wchar_to_utf8_alloc(wresp);
    free(wresp);

    if (!resp) {
        if (lbl_status) gtk_label_set_text(lbl_status, "Erro de conversão da resposta.");
        else debug_log("on_import_to_environment(): wchar_to_utf8_alloc failed");
        return;
    }

    debug_log("on_import_to_environment(): API response -> %s", resp);

    /* parse simples: status == "OK" ou fallback 'OK' prefix */
    gboolean ok = FALSE;
    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
        if (cJSON_IsString(status) && strcmp(status->valuestring, "OK") == 0) ok = TRUE;
        cJSON_Delete(root);
    } else {
        if (strncmp(resp, "OK", 2) == 0) ok = TRUE;
    }

    if (ok) {
        if (lbl_status) gtk_label_set_text(lbl_status, "✅ Dataset importado para o ambiente.");
        debug_log("on_import_to_environment(): import OK");
    } else {
        if (lbl_status) gtk_label_set_text(lbl_status, "❌ Falha ao importar dataset.");
        debug_log("on_import_to_environment(): import FAILED");
    }

    /* liberar resposta */
    g_free(resp);
}



static gboolean on_item_box_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    const char *child_name = (const char*) g_object_get_data(G_OBJECT(widget), "dataset_stack_name");
    ProfileWindowUI *pui = (ProfileWindowUI*) user_data;
    debug_log("on_item_box_button_press(): widget=%p child=%s", widget, child_name ? child_name : "(null)");
    show_stack_child(pui, child_name);
    return TRUE; /* consumir o evento */
}


static gboolean parse_to_mb_(const char *s, double *out_mb) {
    if (!s || !*s || !out_mb) return FALSE;

    char *tmp = g_strdup(s);
    char *start = tmp;
    while (*start && g_ascii_isspace(*start)) start++;
    char *end = start + strlen(start) - 1;
    while (end > start && g_ascii_isspace(*end)) *end-- = '\0';
    
    for (char *p = start; *p; ++p) *p = g_ascii_tolower(*p);
    
    size_t len = strlen(start);
    if (len >= 5 && g_str_has_suffix(start, "bytes")) { start[len-5] = '\0'; }
    else if (len >= 4 && g_str_has_suffix(start, "byte")) { start[len-4] = '\0'; }
    
    char *clean = start;
    while (*clean && g_ascii_isspace(*clean)) clean++;
    end = clean + strlen(clean) - 1;
    while (end > clean && g_ascii_isspace(*end)) *end-- = '\0';

    char *endptr = NULL;
    double v = g_ascii_strtod(clean, &endptr);
    if (endptr == clean) { 
        g_free(tmp); 
        return FALSE; 
    }

    while (endptr && *endptr && g_ascii_isspace(*endptr)) endptr++;

    double mb = 0.0;
    if (!endptr || !*endptr) {
        mb = v / (1024.0 * 1024.0);
    } else if (*endptr == 'k') {
        mb = v / 1024.0;
    } else if (*endptr == 'm') {
        mb = v;
    } else if (*endptr == 'g') {
        mb = v * 1024.0;
    } else if (*endptr == 't') {
        mb = v * 1024.0 * 1024.0;
    } else {
        mb = v / (1024.0 * 1024.0);
    }

    g_free(tmp);
    *out_mb = mb;
    return TRUE;
}

/* Função para fechar janela */
static void profile_close_cb(GtkWidget *btn, gpointer data) {
    GtkWidget *win = GTK_WIDGET(data);
    gtk_widget_destroy(win);
}

// Função auxiliar para converter tamanho
static char* size_to_mb_string_(const char *s) {
    double mb = 0.0;
    if (!parse_to_mb_(s, &mb)) return g_strdup(s ? s : "");
    return g_strdup_printf("%.1f MB", mb);
}

/* Função para preencher detalhes do dataset na stack */
static gboolean fill_dataset_details_in_stack_ui(gpointer user_data) {
    DatasetUIData *d = (DatasetUIData*)user_data;
    ProfileWindowUI *pui = d->pui;

    if (!pui || !GTK_IS_STACK(pui->stack)) goto out_free;

    GtkWidget *existing = gtk_stack_get_child_by_name(pui->stack, "dataset");
    if (existing) gtk_widget_destroy(existing);

    GtkWidget *dataset_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(dataset_page), 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(dataset_page), "profile-panel");

    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_back = gtk_button_new_with_label("◀ Back to Profile");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_back), "pf-btn");
    GtkWidget *title = gtk_label_new(NULL);
    char *title_markup = g_markup_printf_escaped("<span size='large' weight='bold'>Dataset: %s</span>", d->nome ? d->nome : "—");
    gtk_label_set_markup(GTK_LABEL(title), title_markup); g_free(title_markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(hdr), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hdr), title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(dataset_page), hdr, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(dataset_page), grid, FALSE, FALSE, 10);

    int row = 0;

    GtkWidget *lbl_size = gtk_label_new("Size:");
    gtk_label_set_xalign(GTK_LABEL(lbl_size), 0.0);
    char *size_display = (d->tamanho && strcmp(d->tamanho, "—") != 0) ? size_to_mb_string_(d->tamanho) : g_strdup("—");
    GtkWidget *val_size = gtk_label_new(size_display);
    gtk_label_set_xalign(GTK_LABEL(val_size), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl_size, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), val_size, 1, row, 1, 1);
    row++;
    g_free(size_display);

    GtkWidget *lbl_date = gtk_label_new("Created:");
    gtk_label_set_xalign(GTK_LABEL(lbl_date), 0.0);
    GtkWidget *val_date = gtk_label_new(d->data_cadastro ? d->data_cadastro : "—");
    gtk_label_set_xalign(GTK_LABEL(val_date), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl_date, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), val_date, 1, row, 1, 1);
    row++;

    if (d->url && *d->url) {
        GtkWidget *lbl_url = gtk_label_new("Download Link:");
        gtk_label_set_xalign(GTK_LABEL(lbl_url), 0.0);
        GtkWidget *val_url = gtk_label_new(NULL);

        /* texto do link = apenas o basename do arquivo */
        const char *base = strrchr(d->url, '/');
        const char *fname = (base && *(base+1)) ? base+1 : d->url;

        char *url_markup = g_markup_printf_escaped("<a href=\"%s\">%s</a>", d->url, fname);
        gtk_label_set_markup(GTK_LABEL(val_url), url_markup);
        gtk_label_set_xalign(GTK_LABEL(val_url), 0.0);
        gtk_label_set_selectable(GTK_LABEL(val_url), TRUE);
        g_free(url_markup);

        gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), val_url, 1, row, 1, 1);
        row++;
    }

    if (d->descricao && strcmp(d->descricao, "—") != 0) {
        GtkWidget *desc_frame = gtk_frame_new("Description");
        GtkWidget *desc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(desc_box), 6);
        GtkWidget *lbl_desc = gtk_label_new(d->descricao);
        gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
        gtk_label_set_selectable(GTK_LABEL(lbl_desc), TRUE);
        gtk_box_pack_start(GTK_BOX(desc_box), lbl_desc, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(desc_frame), desc_box);
        gtk_box_pack_start(GTK_BOX(dataset_page), desc_frame, FALSE, FALSE, 10);
    }

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(btn_box), TRUE);
    GtkWidget *btn_open_url = gtk_button_new_with_label("import to environment");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_open_url), "pf-btn");
    if (d->url && *d->url) {
        g_object_set_data_full(G_OBJECT(btn_open_url), "dataset-url", g_strdup(d->url), g_free);
        g_signal_connect(btn_open_url, "clicked", G_CALLBACK(on_import_to_environment_profile), pui->parent_window);
    } else {
        gtk_widget_set_sensitive(btn_open_url, FALSE);
    }

    pf_apply_hand_cursor_to(btn_back);
    pf_apply_hand_cursor_to(btn_open_url);

    gtk_box_pack_start(GTK_BOX(btn_box), btn_open_url, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(dataset_page), btn_box, FALSE, FALSE, 0);

    gtk_stack_add_named(pui->stack, dataset_page, "dataset");
    pui->dataset_page = dataset_page;

    gtk_widget_show_all(dataset_page);
    gtk_stack_set_visible_child(GTK_STACK(pui->stack), dataset_page);
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_to_profile_clicked), pui);

out_free:
    if (d->nome) g_free(d->nome);
    if (d->descricao) g_free(d->descricao);
    if (d->tamanho) g_free(d->tamanho);
    if (d->url) g_free(d->url);
    if (d->data_cadastro) g_free(d->data_cadastro);
    if (d->usuario_nome) g_free(d->usuario_nome);
    if (d->usuario_email) g_free(d->usuario_email);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void on_back_to_profile_clicked(GtkButton *btn, gpointer user_data) {
    ProfileWindowUI *pui = (ProfileWindowUI*)user_data;
    if (pui && GTK_IS_STACK(pui->stack)) {
        gtk_stack_set_visible_child_name(pui->stack, "profile");
    }
}

static void on_dataset_info_clicked(GtkButton *btn, gpointer user_data) {
    ProfileWindowUI *pui = (ProfileWindowUI*)user_data;
    const char *child_name = (const char*) g_object_get_data(G_OBJECT(btn), "dataset_stack_name");

    debug_log("on_dataset_info_clicked(): child_name=%s (pui=%p, stack=%p)", child_name ? child_name : "(null)", pui, pui ? pui->stack : NULL);
    if (!child_name || !pui) return;

    /* mostrar o child já pré-criado */
    if (GTK_IS_STACK(pui->stack)) {
        gtk_stack_set_visible_child_name(pui->stack, child_name);
        debug_log("on_dataset_info_clicked(): set visible child to '%s'", child_name);
    } else {
        debug_log("on_dataset_info_clicked(): pui->stack is not a GtkStack");
    }
}

void profile_create_and_show_from_json(const char *user_json, GtkWindow *parent) {
    debug_log("profile_create_and_show_from_json() called");

    if (!user_json) {
        debug_log("input user_json is NULL — aborting");
        return;
    }
    debug_log("user_json length: %zu", strlen(user_json));

    cJSON *root = cJSON_Parse(user_json);
    if (!root) {
        debug_log("cJSON_Parse failed — invalid JSON");
        return;
    }
    debug_log("JSON parsed successfully");

    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    if (!user || !cJSON_IsObject(user)) {
        debug_log("no 'user' object found in JSON or it's not an object");
        cJSON_Delete(root);
        return;
    }
    debug_log("'user' object found");

    const char *nome = (cJSON_GetObjectItemCaseSensitive(user,"nome") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"nome")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"nome")->valuestring : "—";
    const char *email = (cJSON_GetObjectItemCaseSensitive(user,"email") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"email")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"email")->valuestring : "—";
    const char *bio = (cJSON_GetObjectItemCaseSensitive(user,"bio") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"bio")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"bio")->valuestring : NULL;
    const char *avatar = (cJSON_GetObjectItemCaseSensitive(user,"avatar_url") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"avatar_url")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"avatar_url")->valuestring : NULL;
    const char *dt = (cJSON_GetObjectItemCaseSensitive(user,"dataCadastro") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"dataCadastro")) )
                        ? cJSON_GetObjectItemCaseSensitive(user,"dataCadastro")->valuestring : NULL;

    debug_log("Parsed fields — nome: %s, email: %s, bio present: %s, avatar present: %s, dataCadastro: %s",
              nome ? nome : "(null)", email ? email : "(null)", bio ? "yes" : "no", avatar ? "yes" : "no", dt ? dt : "(null)");

    int user_id = 0;
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(user, "id");
    if (id_item && cJSON_IsNumber(id_item)) {
        user_id = id_item->valueint;
        debug_log("user id: %d", user_id);
    } else {
        debug_log("no valid 'id' field found, defaulting user_id=0");
    }

    /* Build window */
    debug_log("Creating profile window widgets");
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(win, "profile-window");
    char title[256];
    snprintf(title, sizeof(title), "Perfil de %s", nome ? nome : "—");
    install_env_w95_titlebar(GTK_WINDOW(win), title);

    /* Tamanho maior + centralização */
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 700);
    #if GTK_CHECK_VERSION(4,0,0)
        if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    #else
        gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    #endif

    /* Stack */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    debug_log("Stack created and transition set");

    ProfileWindowUI *pui = g_new0(ProfileWindowUI, 1);
    pui->stack = GTK_STACK(stack);
    pui->parent_window = GTK_WINDOW(win);

    /* Profile page */
    GtkWidget *profile_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(profile_page), 10);

    /* Top: avatar + name/email */
    GtkWidget *h_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    /* --- AVATAR WIDGET --- */
    GtkWidget *avatar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    pui->avatar_box = avatar_box;
    gtk_widget_set_hexpand(avatar_box, FALSE);
    gtk_widget_set_vexpand(avatar_box, FALSE);

    pui->avatar_image = GTK_IMAGE(gtk_image_new());
    gtk_box_pack_start(GTK_BOX(avatar_box), GTK_WIDGET(pui->avatar_image), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h_top), avatar_box, FALSE, FALSE, 0);
    debug_log("Avatar widget created");
    debug_log(avatar ? "Avatar URL provided" : "No avatar URL provided");
    debug_log("url: %s", avatar ? avatar : "(null)");

    GtkWidget *v_top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *lbl_nome = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", nome);
    gtk_label_set_markup(GTK_LABEL(lbl_nome), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(v_top), lbl_nome, FALSE, FALSE, 0);

    GtkWidget *lbl_email = gtk_label_new(email);
    gtk_box_pack_start(GTK_BOX(v_top), lbl_email, FALSE, FALSE, 0);

    if (dt) {
        GtkWidget *lbl_dt = gtk_label_new(NULL);
        char *md = g_markup_printf_escaped("<small>Joined: %s</small>", dt);
        gtk_label_set_markup(GTK_LABEL(lbl_dt), md);
        g_free(md);
        gtk_box_pack_start(GTK_BOX(v_top), lbl_dt, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(h_top), v_top, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(profile_page), h_top, FALSE, FALSE, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(profile_page), "profile-panel");
    gtk_style_context_add_class(gtk_widget_get_style_context(h_top), "profile-header");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(pui->avatar_image)), "avatar");

    /* Bio */
    GtkWidget *frame = gtk_frame_new("Biography");
    GtkWidget *frame_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(frame), frame_box);
    GtkWidget *lbl_bio = gtk_label_new(bio ? bio : "—");
    gtk_label_set_xalign(GTK_LABEL(lbl_bio), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_bio), TRUE);
    gtk_box_pack_start(GTK_BOX(frame_box), lbl_bio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(profile_page), frame, FALSE, FALSE, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame_box), "bio");

    /* Datasets area (scrolled list) */
    GtkWidget *ds_frame = gtk_frame_new("Datasets");
    GtkWidget *ds_box_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(ds_frame), ds_box_outer);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(sc, TRUE);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sc), list);
    gtk_box_pack_start(GTK_BOX(ds_box_outer), sc, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(profile_page), ds_frame, TRUE, TRUE, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(ds_frame), "datasets-frame");

    /* Close button */
    GtkWidget *btn_close = gtk_button_new_with_label("Close");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "pf-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "close-tab");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(profile_close_cb), win);
    gtk_box_pack_start(GTK_BOX(profile_page), btn_close, FALSE, FALSE, 0);
    pf_apply_hand_cursor_to(btn_close);

    /* Add pages to stack */
    gtk_stack_add_named(GTK_STACK(stack), profile_page, "profile");
    pui->profile_page = profile_page;

    /* Placeholder dataset_page */
    GtkWidget *dataset_placeholder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(dataset_placeholder), 10);
    gtk_stack_add_named(GTK_STACK(stack), dataset_placeholder, "dataset");
    pui->dataset_page = dataset_placeholder;

    gtk_style_context_add_class(gtk_widget_get_style_context(profile_page), "profile-panel");

    GtkWidget *root_widget = wrap_with_profile_css_(stack);

    #if GTK_CHECK_VERSION(4,0,0)
        gtk_window_set_child(GTK_WINDOW(win), root_widget);
    #else
        gtk_container_add(GTK_CONTAINER(win), root_widget);
    #endif

    /* Avatar load */
    if (avatar && *avatar) {
        debug_log("Avatar string present: %s", avatar);
        if (g_str_has_prefix(avatar, "http://") || g_str_has_prefix(avatar, "https://")) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "GET_USER_AVATAR %d", user_id);
            WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
            debug_log("profile_create_and_show_from_json: baixando avatar via comando: %s", cmd);
            if (wcmd) {
                WCHAR *wres = run_api_command(wcmd);
                free(wcmd);
                debug_log("profile_create_and_show_from_json: resposta do avatar recebida: %s", wres ? wchar_to_utf8_alloc(wres) : "(null)");
                if (wres) {
                    char *path = wchar_to_utf8_alloc(wres);
                    free(wres);
                    if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
                        GError *err = NULL;
                        GdkPixbuf *pix = load_cover_from_file_(path, AVATAR_SIZE, &err);
                        if (pix) {
                            gtk_image_set_from_pixbuf(GTK_IMAGE(pui->avatar_image), pix);
                            fit_avatar_box_(pui, pix);
                            g_object_unref(pix);
                        } else {
                            debug_log("profile_tab: falha ao carregar avatar: %s", err ? err->message : "(unknown)");
                            if (err) g_error_free(err);
                            set_default_avatar_(pui);
                        }
                        g_free(path);
                    } else {
                        debug_log("path retornado pelo servidor inexistente: %s", path ? path : "(null)");
                        if (path) g_free(path);
                        set_default_avatar_(pui);
                    }
                } else {
                    debug_log("run_api_command retornou NULL ao buscar avatar remoto");
                    set_default_avatar_(pui);
                }
            } else {
                debug_log("utf8_to_wchar_alloc falhou para comando GET_USER_AVATAR");
                set_default_avatar_(pui);
            }
        } else {
            if (g_file_test(avatar, G_FILE_TEST_EXISTS)) {
                GError *err = NULL;
                GdkPixbuf *pix = load_cover_from_file_(avatar, AVATAR_SIZE, &err);
                if (pix) {
                    gtk_image_set_from_pixbuf(GTK_IMAGE(pui->avatar_image), pix);
                    fit_avatar_box_(pui, pix);
                    g_object_unref(pix);
                } else {
                    debug_log("profile_create_and_show_from_json: falha ao carregar pixbuf para %s -> %s", avatar, err?err->message:NULL);
                    if (err) g_error_free(err);
                    set_default_avatar_(pui);
                }
            } else {
                char uploads_path[1024];
                snprintf(uploads_path, sizeof(uploads_path), "uploads/%s", avatar);
                if (g_file_test(uploads_path, G_FILE_TEST_EXISTS)) {
                    GError *err = NULL;
                    GdkPixbuf *pix = load_cover_from_file_(uploads_path, AVATAR_SIZE, &err);
                    if (pix) {
                        gtk_image_set_from_pixbuf(GTK_IMAGE(pui->avatar_image), pix);
                        fit_avatar_box_(pui, pix);
                        g_object_unref(pix);
                        if (!gtk_image_get_pixbuf(GTK_IMAGE(pui->avatar_image))) {
                            set_default_avatar_(pui);
                        }
                    } else {
                        debug_log("profile_create_and_show_from_json: falha ao carregar avatar de uploads/ — usando default");
                        set_default_avatar_(pui);
                        if (err) g_error_free(err);
                    }
                } else {
                    debug_log("avatar local não encontrado (nem caminho direto nem uploads/). Usando avatar padrão.");
                    set_default_avatar_(pui);
                }
            }
        }
    } else {
        debug_log("nenhum avatar informado — usando avatar padrão");
        set_default_avatar_(pui);
    }

    /* Fetch user's datasets and PRELOAD details pages */
    if (user_id > 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "GET_USER_DATASETS_JSON %d", user_id);
        debug_log("Prepared command: %s", cmd);

        WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
        if (!wcmd) {
            debug_log("utf8_to_wchar_alloc() failed for command: %s", cmd);
        } else {
            debug_log("utf8_to_wchar_alloc() succeeded");
            WCHAR *wres = run_api_command(wcmd);
            free(wcmd);
            if (!wres) {
                debug_log("run_api_command() returned NULL for cmd: %s", cmd);
            } else {
                debug_log("run_api_command() returned a wide-string response");
                char *json_resp = wchar_to_utf8_alloc(wres);
                free(wres);
                if (!json_resp) {
                    debug_log("wchar_to_utf8_alloc() failed or returned NULL");
                } else {
                    debug_log("Response JSON length: %zu", strlen(json_resp));
                    cJSON *r2 = cJSON_Parse(json_resp);
                    if (!r2) {
                        debug_log("Failed to parse datasets JSON response");
                    } else {
                        cJSON *st = cJSON_GetObjectItemCaseSensitive(r2, "status");
                        cJSON *datasets = cJSON_GetObjectItemCaseSensitive(r2, "datasets");
                        if (st && cJSON_IsString(st)) {
                            debug_log("datasets response status: %s", st->valuestring);
                        } else {
                            debug_log("No 'status' in datasets response");
                        }

                        if (st && cJSON_IsString(st) && strcmp(st->valuestring, "OK") == 0 && cJSON_IsArray(datasets)) {
                            debug_log("Datasets array found, iterating...");
                            cJSON *ds;
                            cJSON_ArrayForEach(ds, datasets) {
                                cJSON *idd = cJSON_GetObjectItemCaseSensitive(ds, "iddataset");
                                cJSON *nome_ds = cJSON_GetObjectItemCaseSensitive(ds, "nome");
                                cJSON *desc_ds = cJSON_GetObjectItemCaseSensitive(ds, "descricao");
                                cJSON *url_ds = cJSON_GetObjectItemCaseSensitive(ds, "url");
                                cJSON *tamanho = cJSON_GetObjectItemCaseSensitive(ds, "tamanho");
                                cJSON *dt_ds = cJSON_GetObjectItemCaseSensitive(ds, "dataCadastro");

                                const char *ds_name = (nome_ds && cJSON_IsString(nome_ds)) ? nome_ds->valuestring : "unnamed";
                                const char *ds_desc = (desc_ds && cJSON_IsString(desc_ds)) ? desc_ds->valuestring : "";
                                const char *ds_url  = (url_ds && cJSON_IsString(url_ds)) ? url_ds->valuestring : NULL;
                                const char *ds_size = (tamanho && cJSON_IsString(tamanho)) ? tamanho->valuestring : NULL;
                                const char *ds_dt   = (dt_ds && cJSON_IsString(dt_ds)) ? dt_ds->valuestring : NULL;

                                char stack_child_name[128];
                                if (idd && cJSON_IsNumber(idd)) {
                                    snprintf(stack_child_name, sizeof(stack_child_name), "dataset:%d", idd->valueint);
                                } else {
                                    char tmpname[96];
                                    snprintf(tmpname, sizeof(tmpname), "%s", ds_name);
                                    for (char *p = tmpname; *p; ++p) if (*p == ' ') *p = '_';
                                    snprintf(stack_child_name, sizeof(stack_child_name), "dataset:%s", tmpname);
                                }

                                debug_log("Preparing dataset child '%s' (name=%s)", stack_child_name, ds_name);

                                GtkWidget *detail_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
                                gtk_container_set_border_width(GTK_CONTAINER(detail_page), 12);
                                gtk_style_context_add_class(gtk_widget_get_style_context(detail_page), "profile-panel");

                                GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                                GtkWidget *btn_back = gtk_button_new_with_label("◀ Back to Profile");
                                gtk_style_context_add_class(gtk_widget_get_style_context(btn_back), "pf-btn");
                                GtkWidget *title_lbl = gtk_label_new(NULL);
                                char *title_markup = g_markup_printf_escaped("<span size='large' weight='bold'>Dataset: %s</span>", ds_name);
                                gtk_label_set_markup(GTK_LABEL(title_lbl), title_markup);
                                g_free(title_markup);
                                gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
                                gtk_box_pack_start(GTK_BOX(hdr), btn_back, FALSE, FALSE, 0);
                                gtk_box_pack_start(GTK_BOX(hdr), title_lbl, TRUE, TRUE, 0);
                                gtk_box_pack_start(GTK_BOX(detail_page), hdr, FALSE, FALSE, 0);

                                GtkWidget *grid = gtk_grid_new();
                                gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
                                gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
                                gtk_box_pack_start(GTK_BOX(detail_page), grid, FALSE, FALSE, 10);

                                int info_row = 0;

                                GtkWidget *lbl_size = gtk_label_new("Size:");
                                gtk_label_set_xalign(GTK_LABEL(lbl_size), 0.0);
                                char *size_display = NULL;
                                if (ds_size && strcmp(ds_size, "") != 0) size_display = size_to_mb_string_(ds_size);
                                else size_display = g_strdup("—");
                                GtkWidget *val_size = gtk_label_new(size_display);
                                gtk_label_set_xalign(GTK_LABEL(val_size), 0.0);
                                gtk_grid_attach(GTK_GRID(grid), lbl_size, 0, info_row, 1, 1);
                                gtk_grid_attach(GTK_GRID(grid), val_size, 1, info_row, 1, 1);
                                info_row++;
                                g_free(size_display);

                                GtkWidget *lbl_date = gtk_label_new("Created:");
                                gtk_label_set_xalign(GTK_LABEL(lbl_date), 0.0);
                                GtkWidget *val_date = gtk_label_new(ds_dt ? ds_dt : "—");
                                gtk_label_set_xalign(GTK_LABEL(val_date), 0.0);
                                gtk_grid_attach(GTK_GRID(grid), lbl_date, 0, info_row, 1, 1);
                                gtk_grid_attach(GTK_GRID(grid), val_date, 1, info_row, 1, 1);
                                info_row++;

                                if (ds_url && *ds_url) {
                                    GtkWidget *lbl_url = gtk_label_new("Download Link:");
                                    gtk_label_set_xalign(GTK_LABEL(lbl_url), 0.0);

                                    GtkWidget *val_url = gtk_label_new(NULL);

                                    /* texto do link = apenas o basename do arquivo (ex.: winequality_...csv) */
                                    const char *base = strrchr(ds_url, '/');
                                    const char *fname = (base && *(base+1)) ? base+1 : ds_url;

                                    char *url_markup = g_markup_printf_escaped("<a href=\"%s\">%s</a>", ds_url, fname);
                                    gtk_label_set_markup(GTK_LABEL(val_url), url_markup);
                                    gtk_label_set_xalign(GTK_LABEL(val_url), 0.0);
                                    gtk_label_set_selectable(GTK_LABEL(val_url), TRUE);
                                    g_free(url_markup);

                                    gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, info_row, 1, 1);
                                    gtk_grid_attach(GTK_GRID(grid), val_url, 1, info_row, 1, 1);
                                    info_row++;
                                }

                                if (ds_desc && ds_desc[0]) {
                                    GtkWidget *desc_frame = gtk_frame_new("Description");
                                    GtkWidget *desc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
                                    gtk_container_set_border_width(GTK_CONTAINER(desc_box), 6);
                                    GtkWidget *lbl_desc = gtk_label_new(ds_desc);
                                    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
                                    gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
                                    gtk_label_set_selectable(GTK_LABEL(lbl_desc), TRUE);
                                    gtk_box_pack_start(GTK_BOX(desc_box), lbl_desc, FALSE, FALSE, 0);
                                    gtk_container_add(GTK_CONTAINER(desc_frame), desc_box);
                                    gtk_box_pack_start(GTK_BOX(detail_page), desc_frame, FALSE, FALSE, 10);
                                }

                                GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                                gtk_box_set_homogeneous(GTK_BOX(btn_box), TRUE);
                                GtkWidget *btn_open_url = gtk_button_new_with_label("Import to Environment");
                                gtk_style_context_add_class(gtk_widget_get_style_context(btn_open_url), "pf-btn");
                                if (ds_url && *ds_url) {
                                    g_object_set_data_full(G_OBJECT(btn_open_url), "dataset-url", g_strdup(ds_url), g_free);
                                    g_signal_connect(btn_open_url, "clicked", G_CALLBACK(on_import_to_environment_profile), pui->parent_window);
                                } else {
                                    gtk_widget_set_sensitive(btn_open_url, FALSE);
                                }
                                pf_apply_hand_cursor_to(btn_open_url);
                                gtk_box_pack_start(GTK_BOX(btn_box), btn_open_url, TRUE, TRUE, 0);
                                gtk_box_pack_start(GTK_BOX(detail_page), btn_box, FALSE, FALSE, 0);

                                gtk_stack_add_named(GTK_STACK(pui->stack), detail_page, stack_child_name);
                                gtk_widget_show_all(detail_page);
                                g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_to_profile_clicked), pui);
                                debug_log("Preloaded detail page for '%s' (child=%s)", ds_name, stack_child_name);

                                GtkWidget *item_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                                gtk_style_context_add_class(gtk_widget_get_style_context(item_box), "dataset-row");
                                gtk_widget_set_margin_start(item_box, 6);
                                gtk_widget_set_margin_end(item_box, 6);
                                gtk_widget_set_margin_top(item_box, 4);
                                gtk_widget_set_margin_bottom(item_box, 4);

                                GtkWidget *hrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                                GtkWidget *btn_name = gtk_button_new_with_label(ds_name);
                                gtk_style_context_add_class(gtk_widget_get_style_context(btn_name), "pf-btn");
                                gtk_style_context_add_class(gtk_widget_get_style_context(btn_name), "dataset-name");
                                gtk_style_context_add_class(gtk_widget_get_style_context(btn_name), "link");
                                pf_apply_hand_cursor_to(btn_name);

                                g_object_set_data_full(G_OBJECT(btn_name), "dataset_stack_name", g_strdup(stack_child_name), g_free);
                                g_signal_connect(btn_name, "clicked", G_CALLBACK(on_dataset_info_clicked), pui);
                                gtk_box_pack_start(GTK_BOX(hrow), btn_name, FALSE, FALSE, 0);

                                char meta[256] = "";
                                if (ds_size) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s", ds_size);
                                if (ds_dt) {
                                    if (strlen(meta) > 0) strncat(meta, " • ", sizeof(meta) - strlen(meta) - 1);
                                    strncat(meta, ds_dt, sizeof(meta) - strlen(meta) - 1);
                                }
                                GtkWidget *lbl_meta = gtk_label_new(meta);
                                gtk_style_context_add_class(gtk_widget_get_style_context(lbl_meta), "dataset-meta");
                                gtk_label_set_xalign(GTK_LABEL(lbl_meta), 0.0);
                                gtk_box_pack_start(GTK_BOX(hrow), lbl_meta, TRUE, TRUE, 0);

                                gtk_box_pack_start(GTK_BOX(item_box), hrow, FALSE, FALSE, 0);

                                if (ds_desc && strlen(ds_desc) > 0) {
                                    GtkWidget *lbl_desc = gtk_label_new(ds_desc);
                                    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
                                    gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
                                    gtk_box_pack_start(GTK_BOX(item_box), lbl_desc, FALSE, FALSE, 0);
                                }

                                GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                                gtk_box_pack_start(GTK_BOX(item_box), sep, FALSE, FALSE, 6);
                                gtk_style_context_add_class(gtk_widget_get_style_context(sep), "row-sep");

                                GtkWidget *listrow = gtk_list_box_row_new();

                                #if GTK_CHECK_VERSION(4,0,0)
                                    g_object_set_data_full(G_OBJECT(item_box), "dataset_stack_name", g_strdup(stack_child_name), g_free);

                                    /* clique para abrir detalhes (pode ficar no item_box) */
                                    GtkGesture *gest = gtk_gesture_click_new();
                                    gtk_widget_add_controller(item_box, GTK_EVENT_CONTROLLER(gest));
                                    g_signal_connect(gest, "pressed", G_CALLBACK(on_item_box_gesture_pressed), pui);

                                    /* CONTEÚDO dentro do row */
                                    gtk_container_add(GTK_CONTAINER(listrow), item_box);

                                    /* HOVER azul — NO ROW (não no item_box) */
                                    GtkEventController *motion = gtk_event_controller_motion_new();
                                    gtk_widget_add_controller(listrow, motion);
                                #else
                                    GtkWidget *eb = gtk_event_box_new();
                                    gtk_container_add(GTK_CONTAINER(eb), item_box);
                                    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);

                                    g_object_set_data_full(G_OBJECT(eb), "dataset_stack_name", g_strdup(stack_child_name), g_free);
                                    g_signal_connect(eb, "button-press-event", G_CALLBACK(on_item_box_button_press), pui);
                                    gtk_style_context_add_class(gtk_widget_get_style_context(eb), "eventbox-clickable");
                                    pf_apply_hand_cursor_to(eb);

                                    /* Conteúdo do row */
                                    gtk_container_add(GTK_CONTAINER(listrow), eb);

                                    gtk_widget_add_events(listrow, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
                                    g_signal_connect(listrow, "enter-notify-event", G_CALLBACK(on_row_enter), NULL);
                                    g_signal_connect(listrow, "leave-notify-event", G_CALLBACK(on_row_leave), NULL);
                                #endif

                                gtk_list_box_insert(GTK_LIST_BOX(list), listrow, -1);
                                gtk_widget_show_all(listrow);
                                debug_log("Inserted dataset '%s' into list box (child=%s, listrow=%p)", ds_name, stack_child_name, listrow);
                            }
                        } else {
                            debug_log("datasets response not OK or not an array");
                        }
                        cJSON_Delete(r2);
                    }
                    g_free(json_resp);
                }
            }
        }
    } else {
        debug_log("user_id <= 0 — skipping dataset fetch");
    }

    /* Garantir profile visível e esconder detalhes */
    if (pui && GTK_IS_STACK(pui->stack)) {
        debug_log("Enforcing profile as visible child - scanning stack children (container API)...");
        GList *kids = gtk_container_get_children(GTK_CONTAINER(pui->stack));
        for (GList *l = kids; l; l = l->next) {
            GtkWidget *child = GTK_WIDGET(l->data);
            debug_log(" stack child: %p visible=%d", child, gtk_widget_get_visible(child));
            if (child != pui->profile_page) {
                gtk_widget_hide(child);
                debug_log("  -> hid child %p", child);
            }
        }
        g_list_free(kids);

        gtk_stack_set_visible_child(GTK_STACK(pui->stack), pui->profile_page);

        const char *vis = gtk_stack_get_visible_child_name(GTK_STACK(pui->stack));
        debug_log("After forcing, stack visible child name -> %s", vis ? vis : "(null)");
    } else {
        debug_log("Could not enforce profile visible: pui/stack invalid");
    }

    gtk_widget_show_all(win);
    debug_log("Window shown (gtk_widget_show_all called)");
    cJSON_Delete(root);
    debug_log("Exiting profile_create_and_show_from_json()");
}

#endif /* PROFILE_H */