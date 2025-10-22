#include <gtk/gtk.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <wchar.h>
#include <windows.h>

#include "../backend/communicator.h"
#include "context.h"
#include "debug_window.h"

#ifndef PROFILE_TAB_H
#define PROFILE_TAB_H

typedef struct {
    GtkWidget *container;
    int user_id;

    /* avatar */
    GtkWidget *avatar_image;
    char *avatar_tmp_path;

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
} ProfileTabCtx;

/* Forward declarations */
static void profile_tab_load_user(ProfileTabCtx *ctx);
static void profile_tab_on_avatar_clicked(GtkWidget *w, GdkEventButton *ev, gpointer user_data);
static void profile_tab_on_name_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data);
static void profile_tab_on_bio_label_clicked(GtkWidget *lbl, GdkEventButton *ev, gpointer user_data);
static gboolean profile_tab_on_name_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data);
static gboolean profile_tab_on_bio_focus_out(GtkWidget *textview, GdkEventFocus *event, gpointer user_data);
static gboolean profile_tab_on_bio_key_press(GtkWidget *textview, GdkEventKey *event, gpointer user_data);
static void profile_tab_on_save_clicked(GtkButton *btn, gpointer user_data);

static void profile_tab_set_status(ProfileTabCtx *ctx, const char *msg, gboolean ok) {
    if (!ctx || !ctx->status_label) return;
    
    gtk_label_set_text(GTK_LABEL(ctx->status_label), msg ? msg : "");
    
    GtkStyleContext *st = gtk_widget_get_style_context(ctx->status_label);
    if (ok) {
        gtk_style_context_add_class(st, "status-success");
        gtk_style_context_remove_class(st, "status-error");
    } else {
        gtk_style_context_add_class(st, "status-error");
        gtk_style_context_remove_class(st, "status-success");
    }
}

static void profile_tab_clear_status(ProfileTabCtx *ctx) {
    if (!ctx) return;
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "");
    GtkStyleContext *st = gtk_widget_get_style_context(ctx->status_label);
    gtk_style_context_remove_class(st, "status-success");
    gtk_style_context_remove_class(st, "status-error");
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
            GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(path, 96, 96, TRUE, &err);
            if (pix) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
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

static void profile_tab_on_save_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ProfileTabCtx *ctx = (ProfileTabCtx*)user_data;
    if (!ctx) return;

    profile_tab_clear_status(ctx);
    
    /* Build JSON payload */
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "user_id", ctx->user_id);
    
    const char *name_text = gtk_label_get_text(GTK_LABEL(ctx->lbl_name));
    cJSON_AddStringToObject(j, "nome", name_text ? name_text : "");

    const char *bio_text = gtk_label_get_text(GTK_LABEL(ctx->lbl_bio));
    cJSON_AddStringToObject(j, "bio", bio_text ? bio_text : "");

    if (ctx->avatar_tmp_path && *ctx->avatar_tmp_path) {
        cJSON_AddStringToObject(j, "avatar", ctx->avatar_tmp_path);
    }

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
    if (nome) gtk_label_set_text(GTK_LABEL(ctx->lbl_name), nome);
    else gtk_label_set_text(GTK_LABEL(ctx->lbl_name), "Sem nome");
    
    if (email) gtk_label_set_text(GTK_LABEL(ctx->lbl_email), email);
    else gtk_label_set_text(GTK_LABEL(ctx->lbl_email), "Sem email");

    if (bio && *bio) gtk_label_set_text(GTK_LABEL(ctx->lbl_bio), bio);
    else gtk_label_set_text(GTK_LABEL(ctx->lbl_bio), "Clique para adicionar uma biografia...");

    /* Ensure editors are hidden */
    gtk_widget_hide(ctx->entry_name);
    gtk_widget_show(ctx->lbl_name);
    gtk_widget_hide(ctx->textview_bio);
    gtk_widget_show(ctx->lbl_bio);
    ctx->is_editing = FALSE;

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
                        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(path, 96, 96, TRUE, &err);
                        if (pix) {
                            gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                            g_object_unref(pix);
                        } else {
                            debug_log("profile_tab_load_user: falha ao carregar pixbuf para %s -> %s", 
                                     path, err?err->message:NULL);
                            if (err) g_error_free(err);
                        }
                        g_free(path);
                    }
                }
            }
        } else {
            if (g_file_test(avatar, G_FILE_TEST_EXISTS)) {
                GError *err = NULL;
                GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(avatar, 96, 96, TRUE, &err);
                if (pix) {
                    gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
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
                    GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(uploads_path, 96, 96, TRUE, &err);
                    if (pix) {
                        gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->avatar_image), pix);
                        g_object_unref(pix);
                    } else {
                        debug_log("profile_tab_load_user: falha ao carregar pixbuf para %s -> %s", 
                                 uploads_path, err?err->message:NULL);
                        if (err) g_error_free(err);
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
}

static void add_profile_tab(GtkNotebook *nb, EnvCtx *env) {
    const char *PROFILE_CSS = parse_CSS_file("profile_tab.css");

    ProfileTabCtx *ctx = g_new0(ProfileTabCtx, 1);
    ctx->env = env;
    ctx->user_id = env ? env->current_user_id : 0;
    ctx->is_editing = FALSE;

    /* Main container */
    GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(main_container, "profile-tab-container");

    /* Header */
    GtkWidget *header = gtk_label_new("MEU PERFIL");
    gtk_widget_set_name(header, "profile-header");
    gtk_box_pack_start(GTK_BOX(main_container), header, FALSE, FALSE, 0);

    /* Horizontal container for avatar and info */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(main_container), hbox, FALSE, FALSE, 0);

    /* Avatar section */
    GtkWidget *avatar_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(avatar_frame, "avatar-container");
    
    ctx->avatar_image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_name(ctx->avatar_image, "avatar-image");
    
    GtkWidget *avatar_event = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(avatar_event), ctx->avatar_image);
    gtk_widget_set_tooltip_text(avatar_event, "Clique para alterar o avatar");
    g_signal_connect(avatar_event, "button-press-event", 
                     G_CALLBACK(profile_tab_on_avatar_clicked), ctx);
    
    GtkWidget *avatar_label = gtk_label_new("Avatar");
    gtk_widget_set_name(avatar_label, "info-label");
    
    gtk_box_pack_start(GTK_BOX(avatar_frame), avatar_event, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(avatar_frame), avatar_label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hbox), avatar_frame, FALSE, FALSE, 0);

    /* Info section */
    GtkWidget *info_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(info_frame, "info-container");

    /* Name field */
    GtkWidget *name_label = gtk_label_new("Nome:");
    gtk_widget_set_name(name_label, "info-label");
    gtk_box_pack_start(GTK_BOX(info_frame), name_label, FALSE, FALSE, 0);

    ctx->lbl_name = gtk_label_new("Carregando...");
    gtk_widget_set_name(ctx->lbl_name, "editable-label");
    gtk_label_set_line_wrap(GTK_LABEL(ctx->lbl_name), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ctx->lbl_name), 30);
    
    GtkWidget *name_event = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(name_event), ctx->lbl_name);
    gtk_widget_set_tooltip_text(name_event, "Clique para editar");
    g_signal_connect(name_event, "button-press-event", 
                     G_CALLBACK(profile_tab_on_name_label_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(info_frame), name_event, FALSE, FALSE, 0);

    ctx->entry_name = gtk_entry_new();
    gtk_widget_set_name(ctx->entry_name, "editable-entry");
    g_signal_connect(ctx->entry_name, "focus-out-event",
                     G_CALLBACK(profile_tab_on_name_focus_out), ctx);
    gtk_widget_hide(ctx->entry_name);
    gtk_box_pack_start(GTK_BOX(info_frame), ctx->entry_name, FALSE, FALSE, 0);

    /* Email field */
    GtkWidget *email_label = gtk_label_new("Email:");
    gtk_widget_set_name(email_label, "info-label");
    gtk_box_pack_start(GTK_BOX(info_frame), email_label, FALSE, FALSE, 0);

    ctx->lbl_email = gtk_label_new("carregando@exemplo.com");
    gtk_widget_set_name(ctx->lbl_email, "email-display");
    gtk_box_pack_start(GTK_BOX(info_frame), ctx->lbl_email, FALSE, FALSE, 0);

    /* Bio field */
    GtkWidget *bio_label = gtk_label_new("Biografia:");
    gtk_widget_set_name(bio_label, "info-label");
    gtk_box_pack_start(GTK_BOX(info_frame), bio_label, FALSE, FALSE, 0);

    ctx->lbl_bio = gtk_label_new("Carregando...");
    gtk_widget_set_name(ctx->lbl_bio, "editable-label");
    gtk_label_set_line_wrap(GTK_LABEL(ctx->lbl_bio), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ctx->lbl_bio), 40);
    
    GtkWidget *bio_event = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(bio_event), ctx->lbl_bio);
    gtk_widget_set_tooltip_text(bio_event, "Clique para editar (Ctrl+Enter para salvar)");
    g_signal_connect(bio_event, "button-press-event", 
                     G_CALLBACK(profile_tab_on_bio_label_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(info_frame), bio_event, FALSE, FALSE, 0);

    ctx->textview_bio = gtk_text_view_new();
    gtk_widget_set_name(ctx->textview_bio, "editable-textview");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ctx->textview_bio), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(ctx->textview_bio, 300, 80);
    
    ctx->bio_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview_bio));
    g_signal_connect(ctx->textview_bio, "focus-out-event",
                     G_CALLBACK(profile_tab_on_bio_focus_out), ctx);
    g_signal_connect(ctx->textview_bio, "key-press-event",
                     G_CALLBACK(profile_tab_on_bio_key_press), ctx);
    gtk_widget_hide(ctx->textview_bio);
    gtk_box_pack_start(GTK_BOX(info_frame), ctx->textview_bio, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), info_frame, TRUE, TRUE, 0);

    /* Actions section */
    GtkWidget *actions_frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(actions_frame, "actions-container");

    ctx->btn_save = gtk_button_new_with_label("Salvar Alterações");
    gtk_widget_set_name(ctx->btn_save, "save-button");
    g_signal_connect(ctx->btn_save, "clicked", 
                     G_CALLBACK(profile_tab_on_save_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(actions_frame), ctx->btn_save, FALSE, FALSE, 0);

    ctx->status_label = gtk_label_new("");
    gtk_widget_set_name(ctx->status_label, "status-message");
    gtk_box_pack_start(GTK_BOX(actions_frame), ctx->status_label, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(main_container), actions_frame, FALSE, FALSE, 0);

    /* Wrap with CSS */
    GtkWidget *wrapped = wrap_CSS(PROFILE_CSS, "profile-tab-container", main_container, "profile-tab");

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