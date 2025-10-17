// profile.h
#ifndef PROFILE_H
#define PROFILE_H

#include <gtk/gtk.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include "debug_window.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <wchar.h>
#endif

/*
 * profile.h - implementa uma janela de perfil do usuário que:
 *  - recebe JSON do usuário ({"status":"OK","user":{...}}) e mostra avatar, nome, email e bio
 *  - usa o communicator (run_api_command) para buscar também os datasets do usuário
 *  - lista datasets com botão que abre a URL no navegador padrão
 *
 * Requisitos:
 *   - a função WCHAR* run_api_command(const WCHAR *command) deve existir no communicator.
 *   - o communicator deve entender o comando "GET_USER_DATASETS_JSON <id>"
 *     e retornar o JSON cru: {"status":"OK","datasets":[{...}, ...]}
 *   - o communicator deve entender o comando "GET_USER_AVATAR <id>"
 *     e retornar um caminho local (WCHAR*) para o ficheiro temporário do avatar.
 */

/* forward do communicator (definido em outro módulo) */
extern WCHAR* run_api_command(const WCHAR *command);

/* callback para fechar a janela */
static void profile_close_cb(GtkWidget *btn, gpointer data) {
    GtkWidget *win = GTK_WIDGET(data);
    debug_log("profile_close_cb: closing profile window %p", win);
    gtk_widget_destroy(win);
}

/* callback que abre a URL do dataset no navegador padrão */
static void on_dataset_button_clicked(GtkButton *btn, gpointer user_data) {
    const char *url = (const char*) g_object_get_data(G_OBJECT(btn), "dataset-url");
    GtkWindow *parent = GTK_WINDOW(user_data);
    debug_log("on_dataset_button_clicked: url=%s, parent=%p", url ? url : "(null)", parent);
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
        debug_log("on_dataset_button_clicked: failed to open uri '%s' -> %s", url, err->message);
        g_error_free(err);
    } else {
        debug_log("on_dataset_button_clicked: opened URI ok");
    }
}

/* tenta criar um GtkImage a partir de avatar_url (arquivo local). */
static GtkWidget* make_avatar_widget(const char *avatar_url) {
    debug_log("make_avatar_widget: avatar_url=%s", avatar_url ? avatar_url : "(null)");
    GtkWidget *img = NULL;
    if (avatar_url && strlen(avatar_url) > 0 && g_file_test(avatar_url, G_FILE_TEST_EXISTS)) {
        GError *err = NULL;
        debug_log("make_avatar_widget: file exists, loading pixbuf from %s", avatar_url);
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(avatar_url, 96, 96, TRUE, &err);
        if (pix) {
            img = gtk_image_new_from_pixbuf(pix);
            g_object_unref(pix);
            debug_log("make_avatar_widget: pixbuf loaded and image created");
        } else {
            debug_log("make_avatar_widget: gdk_pixbuf_new_from_file_at_scale failed: %s", err ? err->message : "(no error)");
            if (err) g_error_free(err);
        }
    } else {
        debug_log("make_avatar_widget: avatar file missing or not provided");
    }
    if (!img) {
        /* fallback: um icon genérico - certifique-se de ter um ícone com este nome
           ou GTK usará um fallback do sistema */
        img = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
        debug_log("make_avatar_widget: using fallback avatar icon");
    }
    return img;
}

/* --- helpers de conversão UTF-8 <-> WCHAR (usa Win32 se disponível, senão GLib) --- */
static WCHAR* utf8_to_wchar_alloc(const char *utf8) {
    if (!utf8) return NULL;
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0) return NULL;
    WCHAR *w = (WCHAR*)malloc((size_t)wide_len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wide_len);
    debug_log("utf8_to_wchar_alloc: converted '%s' -> %d WCHARs", utf8, wide_len);
    return w;
#else
    gunichar2 *utf16 = g_utf8_to_utf16(utf8, -1, NULL, NULL, NULL);
    if (!utf16) return NULL;
    size_t wlen = g_utf16_len(utf16);
    WCHAR *w = (WCHAR*)malloc((wlen + 1) * sizeof(WCHAR));
    for (size_t i = 0; i < wlen; ++i) w[i] = (WCHAR)utf16[i];
    w[wlen] = 0;
    g_free(utf16);
    debug_log("utf8_to_wchar_alloc: converted '%s' -> %zu wchar elements", utf8, wlen);
    return w;
#endif
}

static char* wchar_to_utf8_alloc(const WCHAR *w) {
    if (!w) return NULL;
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return NULL;
    char *out = (char*)malloc((size_t)size);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL);
    debug_log("wchar_to_utf8_alloc: converted WCHAR(%p) -> %d bytes", w, size);
    return out;
#else
    size_t len = w ? wcslen((const wchar_t*)w) : 0;
    gunichar2 *utf16 = g_new(gunichar2, len + 1);
    for (size_t i = 0; i < len; ++i) utf16[i] = (gunichar2) ((const wchar_t*)w)[i];
    utf16[len] = 0;
    char *out = g_utf16_to_utf8(utf16, len, NULL, NULL, NULL);
    g_free(utf16);
    debug_log("wchar_to_utf8_alloc: converted wchar len=%zu -> utf8 (%p)", len, out);
    return out;
#endif
}

/* --- cleanup automático de ficheiros temporários de avatar --- */
static GList *profile_temp_files = NULL;
static void profile_cleanup_temp_files(void) {
    debug_log("profile_cleanup_temp_files: cleaning %d files", profile_temp_files ? g_list_length(profile_temp_files) : 0);
    for (GList *l = profile_temp_files; l; l = l->next) {
        char *p = (char*)l->data;
        if (p && *p) {
            debug_log("profile_cleanup_temp_files: removing %s", p);
            /* tentativa silenciosa de remoção */
            remove(p);
        }
        g_free(p);
    }
    g_list_free(profile_temp_files);
    profile_temp_files = NULL;
}

/* cria e mostra janela do perfil a partir do JSON do usuário
   Também busca datasets pelo communicator (GET_USER_DATASETS_JSON <id>) e os exibe. */
static void profile_create_and_show_from_json(const char *user_json, GtkWindow *parent) {
    debug_log("profile_create_and_show_from_json: called with user_json pointer=%p", user_json);
    if (!user_json) {
        debug_log("profile_create_and_show_from_json: user_json is NULL, returning");
        return;
    }

    cJSON *root = cJSON_Parse(user_json);
    if (!root) {
        debug_log("profile_create_and_show_from_json: failed to parse user_json");
        return;
    }
    debug_log("profile_create_and_show_from_json: parsed JSON root=%p", root);

    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    if (!user || !cJSON_IsObject(user)) {
        debug_log("profile_create_and_show_from_json: no 'user' object in JSON");
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

    debug_log("profile_create_and_show_from_json: extracted fields nome='%s' email='%s' avatar='%s'", nome, email, avatar ? avatar : "(null)");

    int user_id = 0;
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(user, "id");
    if (id_item && cJSON_IsNumber(id_item)) user_id = id_item->valueint;
    debug_log("profile_create_and_show_from_json: user_id=%d", user_id);

    /* Resolve avatar local: usa avatar do JSON se existir; senão pede ao communicator */
    char *local_avatar = NULL;
    if (avatar && *avatar && g_file_test(avatar, G_FILE_TEST_EXISTS)) {
        debug_log("profile: avatar from JSON exists on disk: %s", avatar);
        local_avatar = g_strdup(avatar);
    } else if (user_id > 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "GET_USER_AVATAR %d", user_id);
        debug_log("profile: requesting avatar via communicator with command: %s", cmd);
        WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
        if (wcmd) {
            debug_log("profile: calling run_api_command for avatar (WCHAR ptr=%p)", wcmd);
            WCHAR *wres = run_api_command(wcmd); /* communicator retorna um caminho local (WCHAR*) */
            debug_log("profile: run_api_command returned WCHAR ptr=%p", wres);
            free(wcmd);
            if (wres) {
                char *tmp_path = wchar_to_utf8_alloc(wres);
                debug_log("profile: converted WCHAR avatar path to utf8: %s", tmp_path ? tmp_path : "(null)");
                free(wres);
                if (tmp_path && *tmp_path) {
                    if (g_file_test(tmp_path, G_FILE_TEST_EXISTS)) {
                        local_avatar = g_strdup(tmp_path);
                        debug_log("profile: avatar temp file exists and will be used: %s", tmp_path);
                        /* regista para limpeza no exit */
                        profile_temp_files = g_list_prepend(profile_temp_files, g_strdup(tmp_path));
                        static int profile_cleanup_registered = 0;
                        if (!profile_cleanup_registered) {
                            atexit(profile_cleanup_temp_files);
                            profile_cleanup_registered = 1;
                            debug_log("profile: registered atexit cleanup for avatar temp files");
                        }
                    } else {
                        debug_log("profile: avatar temp file returned does not exist: %s", tmp_path);
                    }
                } else {
                    debug_log("profile: run_api_command returned empty path");
                }
                if (tmp_path) g_free(tmp_path);
            } else {
                debug_log("profile: run_api_command returned NULL for avatar");
            }
        } else {
            debug_log("profile: utf8_to_wchar_alloc failed for command: %s", cmd);
        }
    } else {
        debug_log("profile: no avatar provided and no user_id available");
    }

    // Build window
    debug_log("profile: creating GTK window for user '%s'", nome);
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
    GtkWidget *avatar_w = make_avatar_widget(local_avatar);
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

    // Apply metal-panel class to root box (if you use that CSS)
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
        debug_log("profile: requesting datasets with command: %s", cmd);
        WCHAR *wcmd = utf8_to_wchar_alloc(cmd);
        if (wcmd) {
            debug_log("profile: calling run_api_command for datasets (WCHAR ptr=%p)", wcmd);
            WCHAR *wres = run_api_command(wcmd); // caller must free
            debug_log("profile: run_api_command returned for datasets: %p", wres);
            free(wcmd);
            if (wres) {
                char *json_resp = wchar_to_utf8_alloc(wres);
                debug_log("profile: converted WCHAR datasets response to utf8: %s", json_resp ? json_resp : "(null)");
                free(wres);
                if (json_resp) {
                    cJSON *r2 = cJSON_Parse(json_resp);
                    if (r2) {
                        cJSON *st = cJSON_GetObjectItemCaseSensitive(r2, "status");
                        cJSON *datasets = cJSON_GetObjectItemCaseSensitive(r2, "datasets");
                        debug_log("profile: parsed datasets JSON status=%s, datasets=%p",
                                  (st && cJSON_IsString(st)) ? st->valuestring : "(no status)", datasets);
                        if (st && cJSON_IsString(st) && strcmp(st->valuestring, "OK") == 0 && cJSON_IsArray(datasets)) {
                            cJSON *ds;
                            int ds_index = 0;
                            cJSON_ArrayForEach(ds, datasets) {
                                ds_index++;
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

                                debug_log("profile: dataset #%d id=%s name=%s url=%s", ds_index,
                                          idd && cJSON_IsNumber(idd) ? g_strdup_printf("%d", idd->valueint) : "(no id)",
                                          ds_name, ds_url ? ds_url : "(no url)");

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
                                    debug_log("profile: dataset button created and connected for '%s'", ds_name);
                                } else {
                                    gtk_widget_set_sensitive(btn_name, FALSE);
                                    debug_log("profile: dataset '%s' has no URL, button disabled", ds_name);
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
                            debug_log("profile: finished adding %d dataset rows", ds_index);
                        } else {
                            debug_log("profile: datasets request returned non-OK or no array");
                        }
                        cJSON_Delete(r2);
                    } else {
                        debug_log("profile: failed to parse datasets JSON: %s", json_resp);
                    }
                    g_free(json_resp);
                } else {
                    debug_log("profile: wchar_to_utf8_alloc returned NULL json_resp");
                }
            } else {
                debug_log("profile: run_api_command returned NULL for datasets");
            }
        } else {
            debug_log("profile: utf8_to_wchar_alloc failed for datasets command");
        }
    } else {
        debug_log("profile: user_id <= 0, skipping datasets fetch");
    }

    gtk_widget_show_all(GTK_WIDGET(win));
    debug_log("profile: window shown (win=%p)", win);

    /* cleanup local_avatar var (UI keeps the pixbuf inside the widget) */
    if (local_avatar) {
        debug_log("profile: freeing local_avatar string %s", local_avatar);
        g_free(local_avatar);
    }
    cJSON_Delete(root);
    debug_log("profile_create_and_show_from_json: done");
}

#endif /* PROFILE_H */
