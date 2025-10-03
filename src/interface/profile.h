// profile.h

#include <gtk/gtk.h>
#include <cjson/cJSON.h>
#include <string.h>

#ifndef PROFILE_H
#define PROFILE_H

#ifdef _WIN32
  #include <windows.h>
#else
  /* Se não estiver no Windows, tentaremos usar a conversão de GLib.
     Caso prefira, posso trocar tudo para g_convert() por padrão. */
  #include <wchar.h>
#endif

/*
 * profile.h - implementa uma janela de perfil do usuário que:
 *  - recebe JSON do usuário ({"status":"OK","user":{...}}) e mostra avatar, nome, email e bio
 *  - usa o communicator (run_api_command) para buscar também os datasets do usuário
 *  - lista datasets com botão que abre a URL no navegador padrão
 *
 * Uso:
 *   #include "profile.h"
 *   profile_create_and_show_from_json(api_response_json, parent_window);
 *
 * Requisitos:
 *   - a função WCHAR* run_api_command(const WCHAR *command) deve existir no communicator.
 *   - o communicator deve entender o comando "GET_USER_DATASETS_JSON <id>"
 *     e retornar o JSON cru: {"status":"OK","datasets":[{...}, ...]}
 */

/* forward do communicator (definido em outro módulo) */
extern WCHAR* run_api_command(const WCHAR *command);

/* callback para fechar a janela */
static void profile_close_cb(GtkWidget *btn, gpointer data) {
    GtkWidget *win = GTK_WIDGET(data);
    gtk_widget_destroy(win);
}

/* callback que abre a URL do dataset no navegador padrão */
static void on_dataset_button_clicked(GtkButton *btn, gpointer user_data) {
    const char *url = (const char*) g_object_get_data(G_OBJECT(btn), "dataset-url");
    GtkWindow *parent = GTK_WINDOW(user_data);
    if (!url) return;

    GError *err = NULL;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_show_uri_on_window(parent, url, GDK_CURRENT_TIME, &err);
#else
    /* fallback — se não disponível, tenta g_app_info_launch_default_for_uri */
    GAppInfo *app = g_app_info_get_default_for_uri(url, FALSE);
    if (app) {
        g_app_info_launch_default_for_uri(url, NULL, &err);
        g_object_unref(app);
    }
#endif
    if (err) {
        g_warning("Failed to open URI '%s': %s", url, err->message);
        g_error_free(err);
    }
}

/* tenta criar um GtkImage a partir de avatar_url (arquivo local). */
static GtkWidget* make_avatar_widget(const char *avatar_url) {
    GtkWidget *img = NULL;
    if (avatar_url && strlen(avatar_url) > 0 && g_file_test(avatar_url, G_FILE_TEST_EXISTS)) {
        GError *err = NULL;
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(avatar_url, 96, 96, TRUE, &err);
        if (pix) {
            img = gtk_image_new_from_pixbuf(pix);
            g_object_unref(pix);
        } else {
            if (err) g_error_free(err);
        }
    }
    if (!img) {
        img = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
    }
    return img;
}

/* --- helpers de conversão UTF-8 <-> WCHAR (usa Win32 se disponível, senão GLib) --- */
static WCHAR* utf8_to_wchar_alloc(const char *utf8) {
    if (!utf8) return NULL;
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0) return NULL;
    WCHAR *w = (WCHAR*)malloc(wide_len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wide_len);
    return w;
#else
    /* Convert to UCS-2/UTF-16-like using glib (store as wchar_t if compatible) */
    gunichar2 *utf16 = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    if (!utf16) return NULL;
    size_t len = strlen(utf8) + 1;
    /* allocate wchar_t buffer and convert (best-effort) */
    size_t wlen = g_utf16_len(utf16);
    /* Note: this branch is a best-effort; platforms differ in wchar size */
    WCHAR *w = (WCHAR*)malloc((wlen + 1) * sizeof(WCHAR));
    for (size_t i = 0; i < wlen; ++i) w[i] = (WCHAR)utf16[i];
    w[wlen] = 0;
    g_free(utf16);
    return w;
#endif
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
    /* best-effort: cast wchar_t to gunichar2 and convert */
    size_t len = w ? wcslen((const wchar_t*)w) : 0;
    gunichar2 *utf16 = g_new(gunichar2, len + 1);
    for (size_t i = 0; i < len; ++i) utf16[i] = (gunichar2) ((const wchar_t*)w)[i];
    utf16[len] = 0;
    char *out = g_utf16_to_utf8(utf16, len, NULL, NULL, NULL);
    g_free(utf16);
    return out;
#endif
}

/* cria e mostra janela do perfil a partir do JSON do usuário
   Também busca datasets pelo communicator e os exibe. */
static void profile_create_and_show_from_json(const char *user_json, GtkWindow *parent) {
    if (!user_json) return;

    cJSON *root = cJSON_Parse(user_json);
    if (!root) return;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    if (!user || !cJSON_IsObject(user)) {
        cJSON_Delete(root);
        return;
    }

    const char *nome = (cJSON_GetObjectItemCaseSensitive(user,"nome") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"nome")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"nome")->valuestring : "—";
    const char *email = (cJSON_GetObjectItemCaseSensitive(user,"email") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"email")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"email")->valuestring : "—";
    const char *bio = (cJSON_GetObjectItemCaseSensitive(user,"bio") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"bio")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"bio")->valuestring : NULL;
    const char *avatar = (cJSON_GetObjectItemCaseSensitive(user,"avatar_url") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"avatar_url")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"avatar_url")->valuestring : NULL;
    const char *dt = (cJSON_GetObjectItemCaseSensitive(user,"dataCadastro") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(user,"dataCadastro")))
                        ? cJSON_GetObjectItemCaseSensitive(user,"dataCadastro")->valuestring : NULL;

    int user_id = 0;
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(user, "id");
    if (id_item && cJSON_IsNumber(id_item)) user_id = id_item->valueint;

    // Build window
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    char title[256];
    snprintf(title, sizeof(title), "%s", nome);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 360);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent));

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);

    // Top: avatar + name/email
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *avatar_w = make_avatar_widget(avatar);
    gtk_box_pack_start(GTK_BOX(h), avatar_w, FALSE, FALSE, 0);

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *lbl_nome = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", nome);
    gtk_label_set_markup(GTK_LABEL(lbl_nome), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(v), lbl_nome, FALSE, FALSE, 0);

    GtkWidget *lbl_email = gtk_label_new(email);
    gtk_box_pack_start(GTK_BOX(v), lbl_email, FALSE, FALSE, 0);

    if (dt) {
        GtkWidget *lbl_dt = gtk_label_new(NULL);
        char *md = g_markup_printf_escaped("<small>Joined: %s</small>", dt);
        gtk_label_set_markup(GTK_LABEL(lbl_dt), md);
        g_free(md);
        gtk_box_pack_start(GTK_BOX(v), lbl_dt, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(h), v, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer), h, FALSE, FALSE, 0);

    // Bio / description
    GtkWidget *frame = gtk_frame_new("Bio");
    GtkWidget *frame_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(frame), frame_box);
    GtkWidget *lbl_bio = gtk_label_new(bio ? bio : "—");
    gtk_label_set_xalign(GTK_LABEL(lbl_bio), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_bio), TRUE);
    gtk_box_pack_start(GTK_BOX(frame_box), lbl_bio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);

    /* Datasets area (scrolled) */
    GtkWidget *ds_frame = gtk_frame_new("Datasets");
    GtkWidget *ds_box_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(ds_frame), ds_box_outer);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(sc, TRUE);
    GtkWidget *list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(sc), list);
    gtk_box_pack_start(GTK_BOX(ds_box_outer), sc, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer), ds_frame, TRUE, TRUE, 0);

    // Close button
    GtkWidget *btn_close = gtk_button_new_with_label("Close");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(profile_close_cb), win);
    gtk_box_pack_start(GTK_BOX(outer), btn_close, FALSE, FALSE, 0);

    // Apply metal-panel class to root box
    GtkStyleContext *ctx = gtk_widget_get_style_context(outer);
    gtk_style_context_add_class(ctx, "metal-panel");

    #if GTK_CHECK_VERSION(4,0,0)
        gtk_window_set_child(GTK_WINDOW(win), outer);
    #else
        gtk_container_add(GTK_CONTAINER(win), outer);
    #endif

    /* --- Agora: buscar datasets do usuário via communicator (GET_USER_DATASETS_JSON <id>) --- */
    if (user_id > 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "GET_USER_DATASETS_JSON %d", user_id);
        WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
        if (wcmd) {
            WCHAR *wres = run_api_command(wcmd); // caller must free
            free(wcmd);
            if (wres) {
                char *json_resp = wchar_to_utf8_alloc(wres);
                free(wres);
                if (json_resp) {
                    cJSON *r2 = cJSON_Parse(json_resp);
                    if (r2) {
                        cJSON *st = cJSON_GetObjectItemCaseSensitive(r2, "status");
                        cJSON *datasets = cJSON_GetObjectItemCaseSensitive(r2, "datasets");
                        if (st && cJSON_IsString(st) && strcmp(st->valuestring, "OK") == 0 && cJSON_IsArray(datasets)) {
                            cJSON *ds;
                            cJSON_ArrayForEach(ds, datasets) {
                                if (!cJSON_IsObject(ds)) continue;
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

                                // Row container
                                GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                                gtk_widget_set_margin_start(row, 6);
                                gtk_widget_set_margin_end(row, 6);
                                gtk_widget_set_margin_top(row, 4);
                                gtk_widget_set_margin_bottom(row, 4);

                                // Top: name button + meta label
                                GtkWidget *hrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                                GtkWidget *btn_name = gtk_button_new_with_label(ds_name);
                                if (ds_url) {
                                    // store url on object with automatic free
                                    g_object_set_data_full(G_OBJECT(btn_name), "dataset-url", g_strdup(ds_url), g_free);
                                    g_signal_connect(btn_name, "clicked", G_CALLBACK(on_dataset_button_clicked), win);
                                } else {
                                    gtk_widget_set_sensitive(btn_name, FALSE);
                                }
                                gtk_box_pack_start(GTK_BOX(hrow), btn_name, FALSE, FALSE, 0);

                                // meta: size + date
                                char meta[256] = "";
                                if (ds_size) snprintf(meta + strlen(meta), sizeof(meta) - strlen(meta), "%s", ds_size);
                                if (ds_dt) {
                                    if (strlen(meta) > 0) strncat(meta, " • ", sizeof(meta) - strlen(meta) - 1);
                                    strncat(meta, ds_dt, sizeof(meta) - strlen(meta) - 1);
                                }
                                GtkWidget *lbl_meta = gtk_label_new(meta);
                                gtk_label_set_xalign(GTK_LABEL(lbl_meta), 0.0);
                                gtk_box_pack_start(GTK_BOX(hrow), lbl_meta, TRUE, TRUE, 0);

                                gtk_box_pack_start(GTK_BOX(row), hrow, FALSE, FALSE, 0);

                                if (ds_desc && strlen(ds_desc) > 0) {
                                    GtkWidget *lbl_desc = gtk_label_new(ds_desc);
                                    gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
                                    gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
                                    gtk_box_pack_start(GTK_BOX(row), lbl_desc, FALSE, FALSE, 0);
                                }

                                GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                                gtk_box_pack_start(GTK_BOX(row), sep, FALSE, FALSE, 6);

                                GtkWidget *listrow = gtk_list_box_row_new();
                                gtk_container_add(GTK_CONTAINER(listrow), row);
                                gtk_list_box_insert(GTK_LIST_BOX(list), listrow, -1);
                            } // foreach dataset
                        } // status OK & array
                        cJSON_Delete(r2);
                    } // parsed r2
                    g_free(json_resp);
                } // json_resp
            } // wres
        } // wcmd
    } // if user_id > 0

    gtk_widget_show_all(GTK_WIDGET(win));
    cJSON_Delete(root);
}

#endif /* PROFILE_H */
