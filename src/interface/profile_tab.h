#include <gtk/gtk.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <wchar.h>
#include <windows.h>
#include <math.h>
#include "../css/css.h"

#include "../backend/communicator.h"
#include "context.h"
#include "debug_window.h"

#ifndef PROFILE_TAB_H
#define PROFILE_TAB_H
#define AVATAR_SIZE 120

typedef struct {
    GtkWidget *container;
    int user_id;

    /* avatar */
    GtkWidget *avatar_image;
    char *avatar_tmp_path;
    GtkWidget *avatar_box;
    guint status_timeout_id;

    /* name/bio */
    GtkWidget *lbl_name;
    GtkWidget *entry_name;
    GtkWidget *lbl_email;
    GtkWidget *lbl_bio;
    GtkWidget *textview_bio;
    GtkTextBuffer *bio_buffer;

    GtkWidget *btn_save;
    GtkWidget *status_label;

    EnvCtx *env;
    gboolean is_editing;

    /* edit controls */
    GtkWidget *entry_email;
    GtkWidget *btn_edit_name;
    GtkWidget *btn_edit_email;
    GtkWidget *btn_edit_bio;

    /* banner de boas-vindas */
    GtkWidget *welcome_label;

    /* datasets list */
    GtkWidget *datasets_section;
    GtkWidget *datasets_list;

} ProfileTabCtx;

/* Forward declarations */
static void set_default_avatar(ProfileTabCtx *ctx);
static void profile_tab_load_user(ProfileTabCtx *ctx);
static void profile_tab_load_datasets(ProfileTabCtx *ctx);
static void profile_tab_on_avatar_clicked(GtkWidget *w, GdkEventButton *ev, gpointer user_data);
static void profile_tab_on_name_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data);
static void profile_tab_on_bio_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data);
static gboolean profile_tab_on_name_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data);
static gboolean profile_tab_on_bio_focus_out(GtkWidget *textview, GdkEventFocus *event, gpointer user_data);
static gboolean profile_tab_on_bio_key_press(GtkWidget *textview, GdkEventKey *event, gpointer user_data);
static void profile_tab_on_save_clicked(GtkButton *btn, gpointer user_data);

static inline void add_cls(GtkWidget *w, const char *c){
    if (w && c) gtk_style_context_add_class(gtk_widget_get_style_context(w), c);
}
static const char *PROFILE_CSS = NULL;

static void prof_on_session_logout_clicked(GtkButton *btn, gpointer user_data) {
    ds_on_session_logout_clicked(btn, user_data);
}

static void prof_on_session_debug_clicked(GtkButton *btn, gpointer user_data) {
    ds_on_session_debug_clicked(btn, user_data);
}

/* ===== Session strip (Logged as / Debug / Logout) ===== */
/* ===== Session strip (Logged as / Debug / Logout) ===== */
static GtkWidget* prof_build_session_strip(EnvCtx *env) {
    if (!PROFILE_CSS) {
        PROFILE_CSS = parse_CSS_file("profile_tab.css");
    }

    /* Linha: filler à esquerda + sessão + Logout + Debug */
    GtkWidget *toprow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(toprow, "env-toolbar-row");
    gtk_widget_set_hexpand(toprow, TRUE);

    gtk_widget_set_margin_start (toprow, 16);
    gtk_widget_set_margin_end   (toprow, 16);
    gtk_widget_set_margin_top   (toprow, 12);
    gtk_widget_set_margin_bottom(toprow, 12);

    GtkWidget *filler = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(filler, TRUE);
    gtk_box_pack_start(GTK_BOX(toprow), filler, TRUE, TRUE, 0);

    GtkWidget *session_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(session_box, "env-session");

    char who[256] = "";
    if (env && env->current_user_name && *env->current_user_name)
        g_snprintf(who, sizeof(who), "Logged as: %s", env->current_user_name);
    else if (env && env->current_user_email && *env->current_user_email)
        g_snprintf(who, sizeof(who), "Logged as: %s", env->current_user_email);
    else
        g_snprintf(who, sizeof(who), "Logged in");

    GtkWidget *lbl_who = gtk_label_new(who);
    gtk_label_set_xalign(GTK_LABEL(lbl_who), 0.0);
    gtk_box_pack_start(GTK_BOX(session_box), lbl_who, FALSE, FALSE, 0);

    /* ---- BOTÃO LOGOUT COM ÍCONE ---- */
    GtkWidget *btn_logout = gtk_button_new_with_label("Logout");
    {
        /* tenta carregar assets/logout.png (16x16); cai para um ícone do tema se faltar */
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale("assets/logout.png", 16, 16, TRUE, NULL);
        GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb)
                            : gtk_image_new_from_icon_name("system-log-out-symbolic", GTK_ICON_SIZE_MENU);
        if (pb) g_object_unref(pb);
        gtk_button_set_image(GTK_BUTTON(btn_logout), img);
        gtk_button_set_always_show_image(GTK_BUTTON(btn_logout), TRUE);
        gtk_button_set_image_position(GTK_BUTTON(btn_logout), GTK_POS_LEFT);
    }
    gtk_widget_set_tooltip_text(btn_logout, "Encerrar sessão");
    g_signal_connect(btn_logout, "clicked", G_CALLBACK(prof_on_session_logout_clicked), env);
    pf_apply_hand_cursor_to(btn_logout);
    gtk_box_pack_start(GTK_BOX(session_box), btn_logout, FALSE, FALSE, 0);

    /* ---- BOTÃO DEBUG (já existia) ---- */
    GtkWidget *btn_debug = gtk_button_new_with_label("Debug");
    {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale("assets/debug.png", 16, 16, TRUE, NULL);
        GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb) : gtk_image_new();
        if (pb) g_object_unref(pb);
        gtk_button_set_image(GTK_BUTTON(btn_debug), img);
        gtk_button_set_always_show_image(GTK_BUTTON(btn_debug), TRUE);
        gtk_button_set_image_position(GTK_BUTTON(btn_debug), GTK_POS_LEFT);
    }
    gtk_widget_set_tooltip_text(btn_debug, "Open Debug/Backlog Window");
    pf_apply_hand_cursor_to(btn_debug);
    g_signal_connect(btn_debug, "clicked", G_CALLBACK(prof_on_session_debug_clicked), env);

    gtk_box_pack_end(GTK_BOX(toprow), btn_debug, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toprow), session_box, FALSE, FALSE, 0);

    /* Wrapper que pinta fundo: EventBox com classe metal-panel */
    GtkWidget *panel = gtk_event_box_new();
    gtk_widget_set_name(panel, "env-window");
    add_cls(panel, "metal-panel");
    gtk_container_add(GTK_CONTAINER(panel), toprow);

    gtk_widget_set_size_request(panel, -1, 44);

    /* Garante que o CSS está aplicado neste sub-árvore */
    if (PROFILE_CSS) {
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_data(prov, PROFILE_CSS, -1, NULL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(panel),
                                       GTK_STYLE_PROVIDER(prov),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
    }

    gtk_widget_set_hexpand(panel, TRUE);
    gtk_widget_set_halign(panel, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(panel, FALSE);
    gtk_widget_set_margin_start(panel, 0);
    gtk_widget_set_margin_end(panel, 0);
    gtk_widget_set_margin_top(panel, 0);
    gtk_widget_set_margin_bottom(panel, 0);

    return panel;
}

/* escala para cobrir AVATAR_SIZE x AVATAR_SIZE e recorta centro */
static GdkPixbuf* pixbuf_cover_square(GdkPixbuf *src, int target){
    if (!src) return NULL;
    int sw = gdk_pixbuf_get_width(src);
    int sh = gdk_pixbuf_get_height(src);
    if (sw <= 0 || sh <= 0) return NULL;

    /* "cover": escolhe o MAIOR fator de escala p/ cobrir todo o alvo */
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

/* carrega do arquivo e já retorna "cover 192x192" */
static GdkPixbuf* load_cover_from_file(const char *path, int target, GError **err){
    if (!path) return NULL;
    GError *lerr = NULL;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(path, &lerr);
    if (!src){
        if (err) *err = lerr; else if (lerr) g_error_free(lerr);
        return NULL;
    }
    GdkPixbuf *out = pixbuf_cover_square(src, target);
    g_object_unref(src);
    return out;
}

static void profile_tab_clear_status(ProfileTabCtx *ctx);

static gboolean hide_status_cb(gpointer data){
    ProfileTabCtx *ctx = (ProfileTabCtx*)data;
    ctx->status_timeout_id = 0;
    profile_tab_clear_status(ctx);
    return G_SOURCE_REMOVE;
}

static void profile_tab_set_status(ProfileTabCtx *ctx, const char *msg, gboolean ok){
    if (!ctx || !ctx->status_label) return;

    /* cancela timeout anterior, se houver */
    if (ctx->status_timeout_id){
        g_source_remove(ctx->status_timeout_id);
        ctx->status_timeout_id = 0;
    }

    if (!msg || !*msg){
        gtk_widget_hide(ctx->status_label);
        return;
    }

    gtk_label_set_text(GTK_LABEL(ctx->status_label), msg);
    gtk_widget_show(ctx->status_label);

    GtkStyleContext *st = gtk_widget_get_style_context(ctx->status_label);
    if (ok){ gtk_style_context_add_class(st, "status-success");
             gtk_style_context_remove_class(st, "status-error"); }
    else   { gtk_style_context_add_class(st, "status-error");
             gtk_style_context_remove_class(st, "status-success"); }

    ctx->status_timeout_id = g_timeout_add_seconds(5, hide_status_cb, ctx);
}

static void profile_tab_clear_status(ProfileTabCtx *ctx){
    if (!ctx || !ctx->status_label) return;
    if (ctx->status_timeout_id){
        g_source_remove(ctx->status_timeout_id);
        ctx->status_timeout_id = 0;
    }
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "");
    gtk_widget_hide(ctx->status_label);
}

static void fit_avatar_box(ProfileTabCtx *ctx, GdkPixbuf *pix){
    (void)pix;
    if (!ctx || !ctx->avatar_box) return;
    gtk_widget_set_size_request(ctx->avatar_box, AVATAR_SIZE, AVATAR_SIZE);
}

static void profile_tab_on_avatar_clicked(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
    (void)w; (void)ev;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return;

    GtkWidget *fc = gtk_file_chooser_dialog_new("Selecionar Avatar",
                                                 GTK_WINDOW(gtk_widget_get_toplevel(ctx->container)),
                                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                                 "_Cancelar", GTK_RESPONSE_CANCEL,
                                                 "_Abrir", GTK_RESPONSE_ACCEPT,
                                                 NULL);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_filter_set_name(filter, "Imagens");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);

    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (path) {
            debug_log("profile_tab: avatar escolhido: %s", path);
            
            GError *err = NULL;
            GdkPixbuf *pix = load_cover_from_file(path, AVATAR_SIZE, &err);
            if (pix) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                fit_avatar_box(ctx, pix);
                g_object_unref(pix);
                
                if (ctx->avatar_tmp_path) g_free(ctx->avatar_tmp_path);
                ctx->avatar_tmp_path = g_strdup(path);
                profile_tab_set_status(ctx, "Avatar selecionado (clique em Salvar para enviar)", TRUE);
            } else {
                debug_log("profile_tab: falha ao carregar avatar: %s", err ? err->message : "(unknown)");
                profile_tab_set_status(ctx, "Erro ao carregar imagem selecionada", FALSE);
                if (err) g_error_free(err);
            }
            g_free(path);
        }
    }
    gtk_widget_destroy(fc);
}

static void profile_tab_on_name_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data) {
    (void)lbl; (void)ev;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx || ctx->is_editing) return;
    
    ctx->is_editing = TRUE;
    const char *current_name = gtk_label_get_text(GTK_LABEL(ctx->lbl_name));
    gtk_entry_set_text(GTK_ENTRY(ctx->entry_name), current_name ? current_name : "");
    
    gtk_widget_hide(ctx->lbl_name);
    gtk_widget_show(ctx->entry_name);
    gtk_widget_grab_focus(ctx->entry_name);
}

static void profile_tab_on_bio_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data) {
    (void)lbl; (void)ev;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx || ctx->is_editing) return;
    
    ctx->is_editing = TRUE;
    const char *current_bio = gtk_label_get_text(GTK_LABEL(ctx->lbl_bio));
    gtk_text_buffer_set_text(ctx->bio_buffer, current_bio ? current_bio : "", -1);
    
    gtk_widget_hide(ctx->lbl_bio);
    gtk_widget_show(ctx->textview_bio);
    gtk_widget_grab_focus(ctx->textview_bio);
}

static gboolean profile_tab_on_name_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
    (void)entry; (void)event;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return FALSE;
    
    const char *new_name = gtk_entry_get_text(GTK_ENTRY(ctx->entry_name));
    if (new_name && *new_name) {
        gtk_label_set_text(GTK_LABEL(ctx->lbl_name), new_name);
    }
    
    gtk_widget_hide(ctx->entry_name);
    gtk_widget_show(ctx->lbl_name);
    ctx->is_editing = FALSE;
    
    return FALSE;
}

static gboolean profile_tab_on_bio_focus_out(GtkWidget *textview, GdkEventFocus *event, gpointer user_data) {
    (void)textview; (void)event;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return FALSE;
    
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(ctx->bio_buffer, &start);
    gtk_text_buffer_get_end_iter(ctx->bio_buffer, &end);
    char *bio_text = gtk_text_buffer_get_text(ctx->bio_buffer, &start, &end, FALSE);
    
    if (bio_text) {
        gtk_label_set_text(GTK_LABEL(ctx->lbl_bio), bio_text);
        g_free(bio_text);
    }
    
    gtk_widget_hide(ctx->textview_bio);
    gtk_widget_show(ctx->lbl_bio);
    ctx->is_editing = FALSE;
    
    return FALSE;
}

static gboolean profile_tab_on_bio_key_press(GtkWidget *textview, GdkEventKey *event, gpointer user_data) {
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return FALSE;
    
    if (event->keyval == GDK_KEY_Return && (event->state & GDK_CONTROL_MASK)) {
        profile_tab_on_bio_focus_out(textview, NULL, user_data);
        return TRUE;
    }
    
    return FALSE;
}

static GtkWidget* mk_icon_16(const char *path, const char *fallback_icon){
    GError *err = NULL;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file(path, &err);
    GtkWidget *img = NULL;
    if (pix){
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, 16, 16, GDK_INTERP_BILINEAR);
        img = gtk_image_new_from_pixbuf(scaled ? scaled : pix);
        if (scaled) g_object_unref(scaled);
        g_object_unref(pix);
    } else {
        if (err) g_error_free(err);
        img = gtk_image_new_from_icon_name(fallback_icon, GTK_ICON_SIZE_MENU);
    }
    return img;
}

static void toggle_entry(GtkButton *btn, GtkEntry *entry){
    gboolean ed = gtk_editable_get_editable(GTK_EDITABLE(entry));
    gtk_editable_set_editable(GTK_EDITABLE(entry), !ed);
    gtk_widget_set_can_focus(GTK_WIDGET(entry), !ed);
    if (!ed) gtk_widget_grab_focus(GTK_WIDGET(entry));
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(btn));
    gtk_image_set_from_icon_name(GTK_IMAGE(child),
        !ed ? "document-save-symbolic" : "document-edit-symbolic",
        GTK_ICON_SIZE_MENU);
}

static void toggle_textview(GtkButton *btn, GtkTextView *tv){
    gboolean ed = gtk_text_view_get_editable(tv);
    gtk_text_view_set_editable(tv, !ed);
    gtk_text_view_set_cursor_visible(tv, !ed);
    if (!ed) gtk_widget_grab_focus(GTK_WIDGET(tv));
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(btn));
    gtk_image_set_from_icon_name(GTK_IMAGE(child),
        !ed ? "document-save-symbolic" : "document-edit-symbolic",
        GTK_ICON_SIZE_MENU);
}

static void dataset_delete_clicked(GtkButton *btn, gpointer user_data) {
    ProfileTabCtx *ctx = (ProfileTabCtx*) user_data;
    if (!ctx) return;

    /* recuperar id e opcionalmente o widget-row (armazenado em object-data) */
    gpointer id_ptr = g_object_get_data(G_OBJECT(btn), "dataset-id");
    GtkWidget *row_widget = (GtkWidget*) g_object_get_data(G_OBJECT(btn), "dataset-row");
    int dataset_id = GPOINTER_TO_INT(id_ptr);

    if (dataset_id <= 0) {
        profile_tab_set_status(ctx, "ID de dataset inválido", FALSE);
        return;
    }

    /* confirmação */
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Deseja mesmo excluir o dataset %d?", dataset_id);
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_YES) return;

    /* montar comando e converter para WCHAR */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "DELETE_DATASET %d", dataset_id);
    WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
    if (!wcmd) {
        profile_tab_set_status(ctx, "Erro de memória (wchar)", FALSE);
        return;
    }

    /* chamar API (bloqueante) */
    WCHAR *wresult = run_api_command(wcmd);
    free(wcmd);

    if (!wresult) {
        profile_tab_set_status(ctx, "Erro ao chamar API", FALSE);
        return;
    }

    /* converter resposta WCHAR -> UTF-8 */
    char *result = wchar_to_utf8_alloc(wresult);
    /* liberar wresult assim que convertido (conforme uso noutros lugares) */
    free(wresult);

    if (!result) {
        profile_tab_set_status(ctx, "Erro ao interpretar resposta da API", FALSE);
        return;
    }

    /* interpretar resposta simples */
    gboolean ok = FALSE;
    if (strstr(result, "OK") || strstr(result, "Success") || strstr(result, "DELETED")) {
        ok = TRUE;
    }

    if (ok) {
        /* Recarrega toda a listagem (mais robusto do que apenas destruir a linha)
           — profile_tab_load_datasets limpa e reconsulta do backend. */
        profile_tab_set_status(ctx, "Dataset excluído com sucesso.", TRUE);
        profile_tab_load_datasets(ctx);
    } else {
        /* mostra a mensagem da API como erro (ou fallback) */
        char buf[512];
        snprintf(buf, sizeof(buf), "Falha ao excluir: %s", result);
        profile_tab_set_status(ctx, buf, FALSE);
    }

    g_free(result);
}




static void on_edit_name (GtkButton *b, gpointer u){ ProfileTabCtx *c=(ProfileTabCtx*)u; toggle_entry(b, GTK_ENTRY(c->entry_name)); }
static void on_edit_email(GtkButton *b, gpointer u){ ProfileTabCtx *c=(ProfileTabCtx*)u; toggle_entry(b, GTK_ENTRY(c->entry_email)); }
static void on_edit_bio  (GtkButton *b, gpointer u){ ProfileTabCtx *c=(ProfileTabCtx*)u; toggle_textview(b, GTK_TEXT_VIEW(c->textview_bio)); }

static void profile_tab_on_save_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return;

    profile_tab_clear_status(ctx);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "user_id", ctx->user_id);

    const char *name_text  = gtk_entry_get_text(GTK_ENTRY(ctx->entry_name));
    const char *email_text = gtk_entry_get_text(GTK_ENTRY(ctx->entry_email));
    cJSON_AddStringToObject(j, "nome",  name_text  ? name_text  : "");
    cJSON_AddStringToObject(j, "email", email_text ? email_text : "");

    GtkTextIter s,e;
    gtk_text_buffer_get_start_iter(ctx->bio_buffer, &s);
    gtk_text_buffer_get_end_iter  (ctx->bio_buffer, &e);
    char *bio_text = gtk_text_buffer_get_text(ctx->bio_buffer, &s, &e, FALSE);
    cJSON_AddStringToObject(j, "bio", bio_text ? bio_text : "");
    if (bio_text) g_free(bio_text);

    if (ctx->avatar_tmp_path && *ctx->avatar_tmp_path)
        cJSON_AddStringToObject(j, "avatar", ctx->avatar_tmp_path);

    char *payload = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);

    if (!payload) {
        profile_tab_set_status(ctx, "Erro ao construir requisição", FALSE);
        return;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "UPDATE_USER %s", payload);
    debug_log("profile_tab: chamando run_api_command para update: %s", cmd);
    g_free(payload);

    WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
    if (!wcmd) {
        profile_tab_set_status(ctx, "Erro de codificação", FALSE);
        return;
    }

    WCHAR *wres = run_api_command(wcmd);
    free(wcmd);

    if (!wres) {
        profile_tab_set_status(ctx, "Sem resposta do servidor", FALSE);
        return;
    }

    char *resp = wchar_to_utf8_alloc(wres);
    free(wres);
    if (!resp) {
        profile_tab_set_status(ctx, "Resposta do servidor inválida", FALSE);
        return;
    }

    debug_log("profile_tab: resposta do update: %s", resp);

    /* Parse response */
    cJSON *r = cJSON_Parse(resp);
    gboolean ok = FALSE;
    char shortmsg[256];
    if (r) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(r, "status");
        cJSON *message = cJSON_GetObjectItemCaseSensitive(r, "message");
        if (cJSON_IsString(status) && strcmp(status->valuestring, "OK") == 0) {
            ok = TRUE;
            snprintf(shortmsg, sizeof(shortmsg), "Perfil atualizado com sucesso!");
        } else if (cJSON_IsString(message)) {
            snprintf(shortmsg, sizeof(shortmsg), "%s", message->valuestring);
        } else {
            snprintf(shortmsg, sizeof(shortmsg), "Falha ao atualizar perfil");
        }
        cJSON_Delete(r);
    } else {
        if (strncmp(resp, "OK", 2) == 0) {
            ok = TRUE;
            snprintf(shortmsg, sizeof(shortmsg), "Perfil atualizado com sucesso!");
        } else if (strncmp(resp, "ERR", 3) == 0) {
            snprintf(shortmsg, sizeof(shortmsg), "%s", resp + 4);
        } else {
            snprintf(shortmsg, sizeof(shortmsg), "Resposta inesperada do servidor");
        }
    }

    profile_tab_set_status(ctx, shortmsg, ok);

    if (ok) {
        if (ctx->avatar_tmp_path) { 
            g_free(ctx->avatar_tmp_path); 
            ctx->avatar_tmp_path = NULL; 
        }
        profile_tab_load_user(ctx);
    }

    g_free(resp);
}

static void profile_tab_load_user(ProfileTabCtx *ctx) {
    if (!ctx) return;
    char *resp = NULL;
    if (!api_get_user_by_id(ctx->user_id, &resp) || !resp) {
        debug_log("profile_tab_load_user: falha ao buscar usuário %d", ctx->user_id);
        profile_tab_set_status(ctx, "Falha ao carregar perfil", FALSE);
        if (resp) free(resp);
        return;
    }

    debug_log("profile_tab_load_user: recebeu JSON do usuário: %s", resp);
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        profile_tab_set_status(ctx, "Resposta inválida do servidor", FALSE);
        debug_log("profile_tab_load_user: JSON inválido");
        return;
    }

    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    if (!user) { 
        cJSON_Delete(root); 
        profile_tab_set_status(ctx, "Nenhum usuário na resposta", FALSE); 
        return; 
    }

    const char *nome = NULL, *email = NULL, *bio = NULL, *avatar = NULL;
    cJSON *j;
    j = cJSON_GetObjectItemCaseSensitive(user, "nome"); 
    if (cJSON_IsString(j)) nome = j->valuestring;
    j = cJSON_GetObjectItemCaseSensitive(user, "email"); 
    if (cJSON_IsString(j)) email = j->valuestring;
    j = cJSON_GetObjectItemCaseSensitive(user, "bio"); 
    if (cJSON_IsString(j)) bio = j->valuestring;
    j = cJSON_GetObjectItemCaseSensitive(user, "avatar_url"); 
    if (cJSON_IsString(j)) avatar = j->valuestring;

    debug_log("profile_tab_load_user: analisado nome=%s email=%s avatar=%s", 
              nome?nome:"(null)", email?email:"(null)", avatar?avatar:"(null)");

    /* Update UI */
    gtk_label_set_text(GTK_LABEL(ctx->lbl_name), nome ? nome : "");
    gtk_entry_set_text(GTK_ENTRY(ctx->entry_name), nome ? nome : ""); 
    gtk_entry_set_text(GTK_ENTRY(ctx->entry_email), email ? email : "");
    gtk_text_buffer_set_text(ctx->bio_buffer, bio ? bio : "", -1);
    
    /* Atualiza o banner de boas-vindas com o nome do usuário */
    if (ctx->welcome_label) {
        char *markup = g_markup_printf_escaped(
            "<span weight='bold' size='xx-large'>Bem-vindo ao seu Perfil %s!</span>",
            nome ? nome : "");
        gtk_label_set_markup(GTK_LABEL(ctx->welcome_label), markup);
        g_free(markup);
    }

    /* Load avatar */
    if (avatar && *avatar) {
        if (g_str_has_prefix(avatar, "http://") || g_str_has_prefix(avatar, "https://")) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "GET_USER_AVATAR %d", ctx->user_id);
            WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
            if (wcmd) {
                WCHAR *wres = run_api_command(wcmd);
                free(wcmd);
                if (wres) {
                    char *path = wchar_to_utf8_alloc(wres);
                    free(wres);
                    if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
                        GError *err = NULL;
                        GdkPixbuf *pix = load_cover_from_file(path, AVATAR_SIZE, &err);
                        debug_log("profile path: %s", path);
                        if (pix) {
                            gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                            fit_avatar_box(ctx, pix);
                            g_object_unref(pix);
                        } else {
                            debug_log("profile_tab: falha ao carregar avatar: %s", err ? err->message : "(unknown)");
                            if (err) g_error_free(err);
                            profile_tab_set_status(ctx, "Erro ao carregar imagem selecionada", FALSE);
                        }
                        
                        g_free(path);

                    }
                }
            }
        } else {
            if (g_file_test(avatar, G_FILE_TEST_EXISTS)) {
                GError *err = NULL;
                GdkPixbuf *pix = load_cover_from_file(avatar, AVATAR_SIZE, &err);
                if (pix) {
                    gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                    fit_avatar_box(ctx, pix);
                    g_object_unref(pix);
                } else {
                    debug_log("profile_tab_load_user: falha ao carregar pixbuf para %s -> %s", 
                             avatar, err?err->message:NULL);
                    if (err) g_error_free(err);
                }
            } else {
                char uploads_path[1024];
                snprintf(uploads_path, sizeof(uploads_path), "uploads/%s", avatar);
                if (g_file_test(uploads_path, G_FILE_TEST_EXISTS)) {
                    GError *err = NULL;
                    GdkPixbuf *pix = load_cover_from_file(uploads_path, AVATAR_SIZE, &err);
                    if (pix) {
                        gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                        fit_avatar_box(ctx, pix);
                        g_object_unref(pix);
                        if (!gtk_image_get_pixbuf(GTK_IMAGE(ctx->avatar_image)))
                            set_default_avatar(ctx);
                    } else {
                        set_default_avatar(ctx);
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    
    /* Carrega os datasets do usuário */
    profile_tab_load_datasets(ctx);
}

static void dataset_edit_clicked(GtkButton *btn, gpointer user_data) {
    ProfileTabCtx *ctx = (ProfileTabCtx*) user_data;
    if (!ctx) return;
    debug_log("dataset_edit_clicked chamado");


    /* recuperar id + dados atuais (armazenados ao criar o botão) */
    int ds_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "dataset-id"));
    const char *cur_name = (const char*) g_object_get_data(G_OBJECT(btn), "dataset-name");
    const char *cur_desc = (const char*) g_object_get_data(G_OBJECT(btn), "dataset-desc");

    debug_log("dataset_edit_clicked: id=%d nome='%s' desc='%s'", ds_id,
              cur_name ? cur_name : "(null)",
              cur_desc ? cur_desc : "(null)");

    if (ds_id <= 0) {
        profile_tab_set_status(ctx, "ID de dataset inválido para edição", FALSE);
        return;
    }

    /* Construir diálogo modal simples */
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn)));
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Editar Dataset",
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancelar", GTK_RESPONSE_CANCEL,
                                                    "_Salvar", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget *lbl_name = gtk_label_new("Nome:");
    gtk_label_set_xalign(GTK_LABEL(lbl_name), 0.0);
    GtkWidget *entry_name = gtk_entry_new();
    if (cur_name) gtk_entry_set_text(GTK_ENTRY(entry_name), cur_name);

    GtkWidget *lbl_desc = gtk_label_new("Descrição:");
    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
    GtkWidget *txt_desc = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(txt_desc), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(txt_desc));
    if (cur_desc) gtk_text_buffer_set_text(buf, cur_desc, -1);
    GtkWidget *sc_desc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sc_desc, 400, 160);
    gtk_container_add(GTK_CONTAINER(sc_desc), txt_desc);

    gtk_grid_attach(GTK_GRID(grid), lbl_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_desc, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sc_desc, 1, 1, 1, 1);

    gtk_widget_show_all(content);

    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry_name));

        GtkTextIter s,e;
        gtk_text_buffer_get_start_iter(buf, &s);
        gtk_text_buffer_get_end_iter(buf, &e);
        char *new_desc = gtk_text_buffer_get_text(buf, &s, &e, FALSE);

        if (!new_name || !*new_name) {
            profile_tab_set_status(ctx, "O nome do dataset não pode ficar vazio.", FALSE);
            if (new_desc) g_free(new_desc);
            gtk_widget_destroy(dialog);
            return;
        }

        /* Montar JSON: { "dataset-id": X, "nome": "...", "descricao": "..." } */
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "dataset-id", ds_id);
        cJSON_AddStringToObject(j, "nome", new_name);
        cJSON_AddStringToObject(j, "descricao", new_desc ? new_desc : "");
        char *payload = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);

        if (!payload) {
            profile_tab_set_status(ctx, "Erro ao construir payload JSON", FALSE);
            if (new_desc) g_free(new_desc);
            gtk_widget_destroy(dialog);
            return;
        }

        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "UPDATE_DATASET_INFO %s", payload);
        g_free(payload);

        /* converter e enviar */
        WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
        if (!wcmd) {
            profile_tab_set_status(ctx, "Erro de codificação (wchar)", FALSE);
            if (new_desc) g_free(new_desc);
            gtk_widget_destroy(dialog);
            return;
        }

        WCHAR *wres = run_api_command(wcmd);
        free(wcmd);

        if (!wres) {
            profile_tab_set_status(ctx, "Sem resposta do servidor", FALSE);
            if (new_desc) g_free(new_desc);
            gtk_widget_destroy(dialog);
            return;
        }

        char *resp_utf8 = wchar_to_utf8_alloc(wres);
        free(wres);

        if (!resp_utf8) {
            profile_tab_set_status(ctx, "Resposta inválida do servidor", FALSE);
            if (new_desc) g_free(new_desc);
            gtk_widget_destroy(dialog);
            return;
        }

        /* interpretar resposta */
        gboolean ok = FALSE;
        cJSON *r = cJSON_Parse(resp_utf8);
        if (r) {
            cJSON *status = cJSON_GetObjectItemCaseSensitive(r, "status");
            if (cJSON_IsString(status) && strcmp(status->valuestring, "OK") == 0) ok = TRUE;
            cJSON_Delete(r);
        } else {
            if (strncmp(resp_utf8, "OK", 2) == 0) ok = TRUE;
        }

        if (ok) {
            profile_tab_set_status(ctx, "Dataset atualizado com sucesso.", TRUE);
            /* recarrega a lista para refletir a alteração */
            profile_tab_load_datasets(ctx);
        } else {
            char bufmsg[512];
            snprintf(bufmsg, sizeof(bufmsg), "Falha ao atualizar dataset: %s", resp_utf8);
            profile_tab_set_status(ctx, bufmsg, FALSE);
        }

        g_free(resp_utf8);
        if (new_desc) g_free(new_desc);
    }

    gtk_widget_destroy(dialog);
}


/* Função para carregar a listagem de datasets */
static void profile_tab_load_datasets(ProfileTabCtx *ctx) {
    if (!ctx || !ctx->datasets_list || ctx->user_id <= 0) {
        debug_log("profile_tab_load_datasets: contexto inválido ou user_id <= 0");
        return;
    }

    /* Limpa a lista atual */
    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->datasets_list));
    for (GList *iter = children; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "GET_USER_DATASETS_JSON %d", ctx->user_id);
    debug_log("profile_tab: buscando datasets com comando: %s", cmd);

    WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
    if (!wcmd) {
        debug_log("profile_tab: utf8_to_wchar_alloc() falhou para comando: %s", cmd);
        return;
    }

    WCHAR *wres = run_api_command(wcmd);
    free(wcmd);

    if (!wres) {
        debug_log("profile_tab: run_api_command() retornou NULL para cmd: %s", cmd);
        return;
    }

    char *json_resp = wchar_to_utf8_alloc(wres);
    free(wres);

    if (!json_resp) {
        debug_log("profile_tab: wchar_to_utf8_alloc() falhou ou retornou NULL");
        return;
    }

    debug_log("profile_tab: resposta JSON dos datasets recebida, tamanho: %zu", strlen(json_resp));

    cJSON *r2 = cJSON_Parse(json_resp);
    if (!r2) {
        debug_log("profile_tab: falha ao parsear JSON de resposta dos datasets");
        g_free(json_resp);
        return;
    }

    cJSON *st = cJSON_GetObjectItemCaseSensitive(r2, "status");
    cJSON *datasets = cJSON_GetObjectItemCaseSensitive(r2, "datasets");

    if (st && cJSON_IsString(st)) {
        debug_log("profile_tab: status dos datasets: %s", st->valuestring);
    } else {
        debug_log("profile_tab: nenhum 'status' na resposta dos datasets");
    }

    if (st && cJSON_IsString(st) && strcmp(st->valuestring, "OK") == 0 && cJSON_IsArray(datasets)) {
        debug_log("profile_tab: array de datasets encontrado, iterando...");

        cJSON *ds;
        int dataset_count = 0;

        cJSON_ArrayForEach(ds, datasets) {
            /* extrair id corretamente (int) */
            int ds_id = 0;
            cJSON *id_ds = cJSON_GetObjectItemCaseSensitive(ds, "iddataset");
            if (!id_ds) id_ds = cJSON_GetObjectItemCaseSensitive(ds, "dataset_id");
            if (id_ds && cJSON_IsNumber(id_ds)) ds_id = id_ds->valueint;
            debug_log("profile_tab: dataset id = %d", ds_id);

            cJSON *nome_ds = cJSON_GetObjectItemCaseSensitive(ds, "nome");
            cJSON *desc_ds = cJSON_GetObjectItemCaseSensitive(ds, "descricao");
            cJSON *tamanho = cJSON_GetObjectItemCaseSensitive(ds, "tamanho");
            cJSON *dt_ds = cJSON_GetObjectItemCaseSensitive(ds, "dataCadastro");

            const char *ds_name = (nome_ds && cJSON_IsString(nome_ds)) ? nome_ds->valuestring : "unnamed";
            const char *ds_desc = (desc_ds && cJSON_IsString(desc_ds)) ? desc_ds->valuestring : "";
            const char *ds_size = (tamanho && cJSON_IsString(tamanho)) ? tamanho->valuestring : NULL;
            const char *ds_dt   = (dt_ds && cJSON_IsString(dt_ds)) ? dt_ds->valuestring : NULL;

            debug_log("profile_tab: processando dataset: %s", ds_name);

            /* Cria o container do item */
            GtkWidget *item_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            add_cls(item_box, "dataset-row");
            gtk_widget_set_margin_start(item_box, 6);
            gtk_widget_set_margin_end(item_box, 6);
            gtk_widget_set_margin_top(item_box, 4);
            gtk_widget_set_margin_bottom(item_box, 4);

            /* Linha horizontal com nome e metadados */
            GtkWidget *hrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

            /* Nome do dataset como label (não clicável) */
            GtkWidget *name_lbl = gtk_label_new(ds_name);
            add_cls(name_lbl, "dataset-name");
            gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0);
            gtk_box_pack_start(GTK_BOX(hrow), name_lbl, FALSE, FALSE, 0);

            /* Metadados (tamanho e data) */
            char meta[256] = "";
            if (ds_size) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s", ds_size);
            if (ds_dt) {
                if (strlen(meta) > 0) strncat(meta, " • ", sizeof(meta) - strlen(meta) - 1);
                strncat(meta, ds_dt, sizeof(meta) - strlen(meta) - 1);
            }

            GtkWidget *lbl_meta = gtk_label_new(meta);
            add_cls(lbl_meta, "dataset-meta");
            gtk_label_set_xalign(GTK_LABEL(lbl_meta), 0.0);
            gtk_box_pack_start(GTK_BOX(hrow), lbl_meta, TRUE, TRUE, 0);

            /* ----- BOTÕES: Editar e Excluir ----- */
            /* Edit */
            GtkWidget *btn_edit = gtk_button_new_with_label("Editar");
            add_cls(btn_edit, "edit-button");
            g_object_set_data(G_OBJECT(btn_edit), "dataset-id", GINT_TO_POINTER(ds_id));
            /* duplica nome/descrição para uso posterior no diálogo */
            g_object_set_data_full(G_OBJECT(btn_edit), "dataset-name", g_strdup(ds_name), g_free);
            g_object_set_data_full(G_OBJECT(btn_edit), "dataset-desc", g_strdup(ds_desc), g_free);
            g_signal_connect(btn_edit, "clicked", G_CALLBACK(dataset_edit_clicked), ctx);
            pf_apply_hand_cursor_to(btn_edit);

            /* Delete */
            GtkWidget *btn_delete = gtk_button_new_with_label("Excluir");
            add_cls(btn_delete, "delete-button");
            g_object_set_data(G_OBJECT(btn_delete), "dataset-id", GINT_TO_POINTER(ds_id));
            g_signal_connect(btn_delete, "clicked", G_CALLBACK(dataset_delete_clicked), ctx);
            if (ds_id <= 0) gtk_widget_set_sensitive(btn_delete, FALSE);
            pf_apply_hand_cursor_to(btn_delete);

            /* empacotar botões (ajuste visual: Edit antes do Delete) */
            gtk_box_pack_end(GTK_BOX(hrow), btn_delete, FALSE, FALSE, 0);
            gtk_box_pack_end(GTK_BOX(hrow), btn_edit, FALSE, FALSE, 6);

            gtk_box_pack_start(GTK_BOX(item_box), hrow, FALSE, FALSE, 0);

            /* Descrição (se houver) */
            if (ds_desc && *ds_desc) {
                GtkWidget *lbl_desc = gtk_label_new(ds_desc);
                gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
                gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
                gtk_box_pack_start(GTK_BOX(item_box), lbl_desc, FALSE, FALSE, 0);
            }

            /* Separador */
            GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_box_pack_start(GTK_BOX(item_box), sep, FALSE, FALSE, 6);
            add_cls(sep, "row-sep");

            /* Criar listrow e inserir na lista */
            GtkWidget *listrow = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(listrow), item_box);
            gtk_list_box_insert(GTK_LIST_BOX(ctx->datasets_list), listrow, -1);

            /* associe o listrow ao botão delete (para remoção visual no callback) */
            g_object_set_data(G_OBJECT(btn_delete), "dataset-row", listrow);
            /* opcional: associe também ao botão edit para atualizações locais */
            g_object_set_data(G_OBJECT(btn_edit), "dataset-row", listrow);

            gtk_widget_show_all(listrow);
            dataset_count++;
            debug_log("profile_tab: inserido dataset '%s' na list box (id=%d)", ds_name, ds_id);
        }

        debug_log("profile_tab: total de %d datasets carregados", dataset_count);

        /* Mostra ou esconde a seção baseado na quantidade de datasets */
        if (dataset_count > 0) {
            gtk_widget_show(ctx->datasets_section);
        } else {
            gtk_widget_hide(ctx->datasets_section);
        }
    } else {
        debug_log("profile_tab: resposta dos datasets não OK ou não é um array");
        gtk_widget_hide(ctx->datasets_section);
    }

    cJSON_Delete(r2);
    g_free(json_resp);
}

/* tenta carregar ./assets/default_avatar.png (ou default_avatar.png) e aplicar */
static void set_default_avatar(ProfileTabCtx *ctx){
    if (!ctx || !ctx->avatar_image) return;
    const char *cands[] = {"./assets/default_avatar.png","./assets/default_avatar.png"};
    for (int i=0;i<2;i++){
        GError *err=NULL;
        GdkPixbuf *pix = load_cover_from_file(cands[i], AVATAR_SIZE, &err);
        if (pix){
            gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
            fit_avatar_box(ctx, pix);
            g_object_unref(pix);
            return;
        }
        if (err) g_error_free(err);
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(ctx->avatar_image), "avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(ctx->avatar_image), AVATAR_SIZE);
    fit_avatar_box(ctx, NULL);
}

static void add_profile_tab(GtkNotebook *nb, EnvCtx *env) {
    const char *PROFILE_CSS = parse_CSS_file("profile_tab.css");

    ProfileTabCtx *ctx = g_new0(ProfileTabCtx, 1);
    ctx->env = env;
    ctx->user_id = env ? env->current_user_id : 0;
    ctx->is_editing = FALSE;

    /* Main container */
    GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_cls(main_container, "profile-tab-container");
    gtk_widget_set_hexpand(main_container, TRUE);
    gtk_widget_set_vexpand(main_container, TRUE);

    /* Session trip (Logged as / Debug / Logout) */
    GtkWidget *session_strip_prof = prof_build_session_strip(env);
    gtk_box_pack_start(GTK_BOX(main_container), session_strip_prof, FALSE, FALSE, 0);

    gtk_label_set_xalign(GTK_LABEL(ctx->welcome_label), 0.5);
    gtk_widget_set_halign(ctx->welcome_label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(ctx->welcome_label, TRUE);
    gtk_widget_set_margin_top   (ctx->welcome_label, 8);
    gtk_widget_set_margin_bottom(ctx->welcome_label, 6);

    GtkWidget *banner_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_center_widget(GTK_BOX(banner_row), ctx->welcome_label);
    gtk_box_pack_start(GTK_BOX(main_container), banner_row, FALSE, FALSE, 0);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_cls(card, "profile-card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    /* Não permitir que o card cresça com a janela — deixa o conteúdo rolar internamente */
    gtk_widget_set_valign(card, GTK_ALIGN_START);
    gtk_widget_set_vexpand(card, FALSE);
    gtk_widget_set_size_request(card, 520, -1);
    /* não expande o card no box principal — evita empurrar botões para fora */
    gtk_box_pack_start(GTK_BOX(main_container), card, FALSE, FALSE, 0);

    /* avatar central clicável */
    ctx->avatar_image = gtk_image_new();
    add_cls(ctx->avatar_image, "avatar-image");
    set_default_avatar(ctx);

    GtkWidget *avatar_event = gtk_event_box_new();
    add_cls(avatar_event, "avatar-box");       
    ctx->avatar_box = avatar_event;
    gtk_widget_set_halign(avatar_event, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(avatar_event, GTK_ALIGN_START);
    gtk_widget_set_hexpand(avatar_event, FALSE);
    gtk_widget_set_vexpand(avatar_event, FALSE);          
    gtk_container_add(GTK_CONTAINER(avatar_event), ctx->avatar_image);
    gtk_widget_set_tooltip_text(avatar_event, "Clique para alterar o avatar");
    g_signal_connect(avatar_event, "button-press-event", G_CALLBACK(profile_tab_on_avatar_clicked), ctx);
    gtk_widget_set_halign(avatar_event, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(card), avatar_event, FALSE, FALSE, 0);

    /* Nome grande abaixo do avatar */
    ctx->lbl_name = gtk_label_new("");
    add_cls(ctx->lbl_name, "profile-display-name");
    gtk_widget_set_halign(ctx->lbl_name, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(card), ctx->lbl_name, FALSE, FALSE, 0);

    /* campos empilhados */
    GtkWidget *fields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_cls(fields, "fields-container");
    gtk_box_pack_start(GTK_BOX(card), fields, FALSE, FALSE, 0);

    /* Nome */
    GtkWidget *row_name = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4); 
    add_cls(row_name, "row");

    /* esquerda: ícone + "Nome:" */
    GtkWidget *left_name = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_cls(left_name, "field-left");
    GtkWidget *ic_user = mk_icon_16("./assets/user.png", "user-identity-symbolic");

    GtkWidget *lbl_nome = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(lbl_nome), TRUE);
    gtk_label_set_markup(GTK_LABEL(lbl_nome), "<b>Nome</b>");
    add_cls(lbl_nome, "field-label");
    gtk_label_set_xalign(GTK_LABEL(lbl_nome), 0.0);

    gtk_box_pack_start(GTK_BOX(left_name), ic_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_name), lbl_nome, FALSE, FALSE, 0);

    /* direita: campo */
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_cls(name_box, "edit-row");
    ctx->entry_name = gtk_entry_new();
    add_cls(ctx->entry_name, "edit-input");
    gtk_editable_set_editable(GTK_EDITABLE(ctx->entry_name), TRUE);
    gtk_box_pack_start(GTK_BOX(name_box), ctx->entry_name, TRUE, TRUE, 0);

    /* empacotar */
    gtk_box_pack_start(GTK_BOX(row_name), left_name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row_name), name_box,  TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(fields),   row_name,  FALSE, FALSE, 0);

    /* Email */
    GtkWidget *row_email = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *left_email = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_cls(left_email, "field-left");
    GtkWidget *ic_email = mk_icon_16("./assets/email.png", "mail-send-symbolic");

    GtkWidget *lbl_email = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(lbl_email), TRUE);
    gtk_label_set_markup(GTK_LABEL(lbl_email), "<b>Email</b>");
    add_cls(lbl_email, "field-label");
    gtk_label_set_xalign(GTK_LABEL(lbl_email), 0.0);

    gtk_box_pack_start(GTK_BOX(left_email), ic_email, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_email), lbl_email, FALSE, FALSE, 0);

    GtkWidget *email_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_cls(email_box, "edit-row");
    ctx->entry_email = gtk_entry_new();
    add_cls(ctx->entry_email, "edit-input");
    gtk_editable_set_editable(GTK_EDITABLE(ctx->entry_email), TRUE);
    gtk_box_pack_start(GTK_BOX(email_box), ctx->entry_email, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(row_email), left_email, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row_email), email_box,   TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(fields),    row_email,   FALSE, FALSE, 0);

    /* Biografia */
    GtkWidget *row_bio = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *left_bio = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_cls(left_bio, "field-left");
    GtkWidget *ic_bio = mk_icon_16("./assets/cadastro.png", "text-x-generic-symbolic");

    GtkWidget *lbl_bio = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(lbl_bio), TRUE);
    gtk_label_set_markup(GTK_LABEL(lbl_bio), "<b>Biografia</b>");
    add_cls(lbl_bio, "field-label");
    gtk_label_set_xalign(GTK_LABEL(lbl_bio), 0.0);

    gtk_box_pack_start(GTK_BOX(left_bio), ic_bio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_bio), lbl_bio, FALSE, FALSE, 0);

    GtkWidget *bio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_cls(bio_box, "edit-row");

    ctx->textview_bio = gtk_text_view_new();
    add_cls(ctx->textview_bio, "edit-textview");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ctx->textview_bio), GTK_WRAP_WORD_CHAR);
    ctx->bio_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview_bio));

    GtkWidget *bio_scrolled = gtk_scrolled_window_new(NULL, NULL);
    add_cls(bio_scrolled, "edit-scrolled");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(bio_scrolled),
                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(bio_scrolled, -1, 120);
    gtk_container_add(GTK_CONTAINER(bio_scrolled), ctx->textview_bio);

    gtk_box_pack_start(GTK_BOX(bio_box), bio_scrolled, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(row_bio), left_bio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row_bio), bio_box,  TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(fields),  row_bio,  FALSE, FALSE, 0);

    /* Seção de Datasets */
    ctx->datasets_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_cls(ctx->datasets_section, "datasets-section");
    gtk_box_pack_start(GTK_BOX(card), ctx->datasets_section, FALSE, FALSE, 0);

    /* Título da seção de datasets */
    GtkWidget *datasets_label = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(datasets_label), TRUE);
    gtk_label_set_markup(GTK_LABEL(datasets_label), "<b>Meus Datasets</b>");
    add_cls(datasets_label, "section-title");
    gtk_label_set_xalign(GTK_LABEL(datasets_label), 0.0);
    gtk_box_pack_start(GTK_BOX(ctx->datasets_section), datasets_label, FALSE, FALSE, 0);

    /* Lista de datasets */
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    add_cls(scrolled_window, "datasets-scrolled");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 200);
    gtk_box_pack_start(GTK_BOX(ctx->datasets_section), scrolled_window, TRUE, TRUE, 0);

    ctx->datasets_list = gtk_list_box_new();
    add_cls(ctx->datasets_list, "datasets-list");
    gtk_container_add(GTK_CONTAINER(scrolled_window), ctx->datasets_list);

    /* Inicialmente esconde a seção até que os datasets sejam carregados */
    gtk_widget_hide(ctx->datasets_section);

    /* Actions section */
    GtkWidget *actions_frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_cls(actions_frame, "actions-container");

    ctx->btn_save = gtk_button_new_with_label("Save Changes/Update Profile");
    add_cls(ctx->btn_save, "save-button");
    gtk_box_pack_start(GTK_BOX(actions_frame), ctx->btn_save, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(actions_frame), ctx->btn_save);
    g_signal_connect(ctx->btn_save, "clicked", G_CALLBACK(profile_tab_on_save_clicked), ctx);

    ctx->status_label = gtk_label_new("");
    add_cls(ctx->status_label, "status-message");
    gtk_widget_set_no_show_all(ctx->status_label, TRUE);
    gtk_widget_hide(ctx->status_label);
    gtk_box_pack_end(GTK_BOX(actions_frame), ctx->status_label, FALSE, FALSE, 0);

    
   /* Colocar a barra de ações na base do card, mantendo-a visível */
    gtk_box_pack_end(GTK_BOX(card), actions_frame, FALSE, FALSE, 0);
    gtk_widget_set_hexpand(actions_frame, FALSE);
    /* evitar que o botão fique esticado além do necessário */
    gtk_widget_set_hexpand(ctx->btn_save, FALSE);
    gtk_widget_set_halign(ctx->btn_save, GTK_ALIGN_CENTER);

    /* Wrap with CSS */
    GtkWidget *wrapped = wrap_CSS(PROFILE_CSS, "profile-tab-container", main_container, "profile-tab");
    add_cls(wrapped, "profile-tab");
    {
    GtkCssProvider *userprov = gtk_css_provider_new();
    gtk_css_provider_load_from_path(userprov, "profile_tab.css", NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(wrapped),
        GTK_STYLE_PROVIDER(userprov), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(userprov);
    }

    /* --- Tab "Perfil" com ícone + texto --- */
    GtkWidget *tab_box  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *tab_text = gtk_label_new("Perfil");
    GtkWidget *tab_img  = NULL;

    /* carrega ./assets/user.png (escala para ~16px) com fallback */
    {
        GError *err = NULL;
        GdkPixbuf *pix = gdk_pixbuf_new_from_file("./assets/user.png", &err);
        if (pix) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, 16, 16, GDK_INTERP_BILINEAR);
            tab_img = gtk_image_new_from_pixbuf(scaled ? scaled : pix);
            if (scaled) g_object_unref(scaled);
            g_object_unref(pix);
        } else {
            /* fallback para um ícone do tema, se a imagem não existir */
            tab_img = gtk_image_new_from_icon_name("user-identity-symbolic", GTK_ICON_SIZE_MENU);
            if (err) g_error_free(err);
        }
    }

    /* empacota imagem à esquerda do texto */
    gtk_box_pack_start(GTK_BOX(tab_box), tab_img,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tab_box), tab_text, FALSE, FALSE, 0);

    /* usa o box como rótulo da aba */
    gtk_notebook_append_page(nb, wrapped, tab_box);
    gtk_widget_show_all(tab_box);

    ctx->container = wrapped;

    /* Load initial data */
    if (ctx->user_id > 0) {
        profile_tab_load_user(ctx);
    } else {
        profile_tab_set_status(ctx, "Não logado", FALSE);
    }
}

#endif /* PROFILE_TAB_H */