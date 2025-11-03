#include <gtk/gtk.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "../css/css.h"
#include "debug_window.h"

/* Include the canonical communicator header (no conflicting extern prototypes) */
#include "../backend/communicator.h"

/* EnvCtx holds the current user session info (id, name, email).
   Ajuste o include se necessário.
*/
#include "context.h"


#ifndef DATASETS_UPLOAD_H
#define DATASETS_UPLOAD_H


/* helper */
static void upload_titlebar_on_max(GtkButton *btn, gpointer win_) {
    (void)btn;
    GtkWindow *win = GTK_WINDOW(win_);
    if (gtk_window_is_maximized(win)) gtk_window_unmaximize(win);
    else                               gtk_window_maximize(win);
}

static gboolean destroy_widget_cb(gpointer w) {
    if (GTK_IS_WIDGET(w)) gtk_widget_destroy(GTK_WIDGET(w));
    return G_SOURCE_REMOVE;
}

/* Titlebar Win95 para o diálogo de upload */
static void install_upload_w95_titlebar(GtkWindow *win, const char *title_text) {
    GtkWidget *hb = gtk_header_bar_new();
    gtk_widget_set_name(hb, "w95-titlebar");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), FALSE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), NULL);

    /* Esquerda: ícone + título */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GdkPixbuf *pb_logo = gdk_pixbuf_new_from_file_at_scale("assets/AI-for-dummies.png", 20, 20, TRUE, NULL);
    GtkWidget *logo = gtk_image_new_from_pixbuf(pb_logo);
    if (pb_logo) g_object_unref(pb_logo);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), logo, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new(title_text ? title_text : "Upload dataset");
    gtk_widget_set_name(title, "w95-title");
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), left);

    /* Direita: botões PNG 12px */
    GtkWidget *btn_min   = gtk_button_new();
    GtkWidget *btn_max   = gtk_button_new();
    GtkWidget *btn_close = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_min),   "envbar-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_max),   "envbar-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "envbar-btn");

    GdkPixbuf *pb_min   = gdk_pixbuf_new_from_file_at_scale("assets/minimize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_max   = gdk_pixbuf_new_from_file_at_scale("assets/maximize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_close = gdk_pixbuf_new_from_file_at_scale("assets/close.png",    12, 12, TRUE, NULL);
    if (pb_min)   gtk_button_set_image(GTK_BUTTON(btn_min),   gtk_image_new_from_pixbuf(pb_min));
    if (pb_max)   gtk_button_set_image(GTK_BUTTON(btn_max),   gtk_image_new_from_pixbuf(pb_max));
    if (pb_close) gtk_button_set_image(GTK_BUTTON(btn_close), gtk_image_new_from_pixbuf(pb_close));
    if (pb_min)   g_object_unref(pb_min);
    if (pb_max)   g_object_unref(pb_max);
    if (pb_close) g_object_unref(pb_close);

    gtk_button_set_always_show_image(GTK_BUTTON(btn_min),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_max),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_close), TRUE);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_close);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_max);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_min);

    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_close),   win);
    g_signal_connect_swapped(btn_min,   "clicked", G_CALLBACK(gtk_window_iconify), win);
    g_signal_connect        (btn_max,   "clicked", G_CALLBACK(upload_titlebar_on_max), win);

    gtk_window_set_titlebar(win, hb);
}

/* Public: open a modal dialog where the user can choose a CSV and fill metadata.
   parent: optional GtkWindow parent
   env: pointer to EnvCtx (may be NULL). If env provides current_user info, it will be used.
*/
static void show_dataset_upload_dialog(GtkWindow *parent, EnvCtx *env);

/* Implementation (static helpers) */

/* Structure holding widgets/state for the dialog */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *file_label;
    GtkWidget *btn_choose;
    GtkWidget *entry_nome;
    GtkWidget *entry_desc;

    /* session info copied from EnvCtx */
    int        user_id;
    char      *user_name;
    char      *user_email;

    GtkWidget *btn_upload;

    /* status area: icon + label (we show only short messages) */
    GtkWidget *status_box;
    GtkWidget *status_icon;
    GtkWidget *status_label;

    char *chosen_path;
} UploadUI;

/* Idle message used to update UI from main thread */
typedef struct {
    UploadUI *u;
    char     *msg;     /* short message (allocated) */
    gboolean  success; /* TRUE = success, FALSE = error */
} IdleMsg;

/* Utility: set a label to a path (frees previous) */
static void set_file_label(UploadUI *u, const char *path) {
    if (!u) return;
    if (u->chosen_path) { g_free(u->chosen_path); u->chosen_path = NULL; }
    if (path && *path) {
        u->chosen_path = g_strdup(path);
        gtk_label_set_text(GTK_LABEL(u->file_label), path);
    } else {
        gtk_label_set_text(GTK_LABEL(u->file_label), "(no file selected)");
    }
}

/* Choose file button callback */
static void on_choose_file_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    UploadUI *u = (UploadUI*)user_data;
    GtkWindow *parent = GTK_WINDOW(u->dialog);

    GtkWidget *fc = gtk_file_chooser_dialog_new("Select CSV",
                                                 parent,
                                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 "_Open", GTK_RESPONSE_ACCEPT,
                                                 NULL);
    GtkFileFilter *filt = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filt, "*.csv");
    gtk_file_filter_set_name(filt, "CSV files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filt);

    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        set_file_label(u, path);
        g_free(path);
    }
    gtk_widget_destroy(fc);
}

/* Called in main thread when upload finished (reenable button) */
static gboolean upload_finished_idle(gpointer data) {
    UploadUI *u = (UploadUI*)data;
    gtk_widget_set_sensitive(u->btn_upload, TRUE);
    return G_SOURCE_REMOVE;
}

/* Update UI (main thread) with short result message and icon */
static gboolean idle_set_progress_msg_free(gpointer data) {
    IdleMsg *m = (IdleMsg*)data;
    if (!m) return G_SOURCE_REMOVE;

    UploadUI *u = m->u;
    if (!u) {
        if (m->msg) g_free(m->msg);
        g_free(m);
        return G_SOURCE_REMOVE;
    }

    /* reset style classes first */
    GtkStyleContext *sc = gtk_widget_get_style_context(u->status_label);
    gtk_style_context_remove_class(sc, "upload-success");
    gtk_style_context_remove_class(sc, "upload-error");

    if (m->success) {
        /* success: green short message + ok icon */
        gtk_label_set_text(GTK_LABEL(u->status_label), m->msg ? m->msg : "Upload realizado com sucesso");
        gtk_image_set_from_icon_name(GTK_IMAGE(u->status_icon), "dialog-ok", GTK_ICON_SIZE_BUTTON);
        gtk_style_context_add_class(sc, "upload-success");

        /* opcional: fechar dialog automaticamente após 1.2s */
        g_timeout_add_seconds(1, destroy_widget_cb, u->dialog);
    } else {
        /* error: show reason (short) and red styling */
        gtk_label_set_text(GTK_LABEL(u->status_label), m->msg ? m->msg : "Falha no upload");
        gtk_image_set_from_icon_name(GTK_IMAGE(u->status_icon), "dialog-error", GTK_ICON_SIZE_BUTTON);
        gtk_style_context_add_class(sc, "upload-error");
    }

    if (m->msg) g_free(m->msg);
    g_free(m);
    return G_SOURCE_REMOVE;
}

/* Worker that performs upload in separate thread and prepares a short IdleMsg */
static gpointer upload_worker(gpointer user_data) {
    UploadUI *u = (UploadUI*)user_data;

    /* gather values (copy strings because we'll be outside main thread) */
    char *path = u->chosen_path ? g_strdup(u->chosen_path) : NULL;
    char *nome = g_strdup(gtk_entry_get_text(GTK_ENTRY(u->entry_nome)));
    char *desc = g_strdup(gtk_entry_get_text(GTK_ENTRY(u->entry_desc)));

    /* copy session info (may be NULL) */
    int user_id = u->user_id;
    char *en_nome = u->user_name ? g_strdup(u->user_name) : NULL;
    char *en_email = u->user_email ? g_strdup(u->user_email) : NULL;

    gboolean ok = FALSE;
    char *api_resp = NULL;

    if (!path) {
        IdleMsg *im = g_new0(IdleMsg, 1);
        im->u = u;
        im->success = FALSE;
        im->msg = g_strdup("Por favor selecione um arquivo CSV primeiro.");
        g_idle_add(idle_set_progress_msg_free, im);
        goto cleanup;
    }

    const char *p_en_nome = (en_nome && *en_nome) ? en_nome : NULL;
    const char *p_en_email = (en_email && *en_email) ? en_email : NULL;
    const char *p_nome = (nome && *nome) ? nome : NULL;
    const char *p_desc = (desc && *desc) ? desc : NULL;

    /* call the uploader */
    debug_log("upload_worker: calling api_upload_csv_with_meta(path=%s,user_id=%d,nome=%s,desc=%s)",
              path ? path : "(null)", user_id,
              p_nome ? p_nome : "(null)", p_desc ? p_desc : "(null)");

    if (api_upload_csv_with_meta(path, user_id, p_en_nome, p_en_email, p_nome, p_desc, &api_resp)) {
        /* parse response JSON (shorten to nice message) */
        char *short_msg = g_strdup("Upload realizado com sucesso.");
        gboolean success = TRUE;

        if (api_resp && api_resp[0]) {
            cJSON *root = cJSON_Parse(api_resp);
            if (root) {
                cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
                cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
                if (cJSON_IsString(status) && strcmp(status->valuestring, "OK") == 0) {
                    /* prefer explicit message if present but keep short */
                    if (cJSON_IsString(message) && message->valuestring && strlen(message->valuestring) > 0) {
                        g_free(short_msg);
                        short_msg = g_strdup_printf("Upload realizado: %s", message->valuestring);
                    }
                } else {
                    success = FALSE;
                    if (cJSON_IsString(message) && message->valuestring && strlen(message->valuestring) > 0) {
                        g_free(short_msg);
                        short_msg = g_strdup_printf("Falha no upload: %s", message->valuestring);
                    } else {
                        g_free(short_msg);
                        short_msg = g_strdup("Falha no upload (resposta inválida).");
                    }
                }
                cJSON_Delete(root);
            } else {
                /* non-JSON fallback: interpret simple prefixes */
                if (strncmp(api_resp, "OK", 2) == 0) {
                    /* keep success short */
                    g_free(short_msg);
                    short_msg = g_strdup("Upload realizado com sucesso.");
                    success = TRUE;
                } else {
                    g_free(short_msg);
                    /* take up to 120 characters of raw text for reason */
                    size_t len = strlen(api_resp);
                    size_t cap = len > 120 ? 120 : len;
                    char buf[128];
                    strncpy(buf, api_resp, cap);
                    buf[cap] = '\0';
                    short_msg = g_strdup_printf("Falha no upload: %s", buf);
                    success = FALSE;
                }
            }
        }

        IdleMsg *im = g_new0(IdleMsg, 1);
        im->u = u;
        im->success = success;
        im->msg = short_msg;
        g_idle_add(idle_set_progress_msg_free, im);
        ok = success;
    } else {
        /* api_upload_csv_with_meta returned false; use api_resp if any for short reason */
        char *short_msg = g_strdup("Falha no upload (erro de comunicação).");
        if (api_resp && api_resp[0]) {
            /* try to parse JSON for message */
            cJSON *root = cJSON_Parse(api_resp);
            if (root) {
                cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
                if (cJSON_IsString(message) && message->valuestring && *message->valuestring) {
                    g_free(short_msg);
                    short_msg = g_strdup_printf("Falha no upload: %s", message->valuestring);
                }
                cJSON_Delete(root);
            } else {
                /* raw fallback */
                size_t len = strlen(api_resp);
                size_t cap = len > 120 ? 120 : len;
                char buf[128];
                strncpy(buf, api_resp, cap);
                buf[cap] = '\0';
                g_free(short_msg);
                short_msg = g_strdup_printf("Falha no upload: %s", buf);
            }
        }
        IdleMsg *im = g_new0(IdleMsg, 1);
        im->u = u;
        im->success = FALSE;
        im->msg = short_msg;
        g_idle_add(idle_set_progress_msg_free, im);
    }

cleanup:
    if (api_resp) free(api_resp);
    if (path) g_free(path);
    g_free(nome); g_free(desc);
    if (en_nome) g_free(en_nome);
    if (en_email) g_free(en_email);

    /* Re-enable upload button on main thread */
    g_idle_add(upload_finished_idle, u);

    return GINT_TO_POINTER(ok);
}

/* Upload button clicked: spawn thread to do the upload */
static void on_upload_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    UploadUI *u = (UploadUI*)user_data;
    if (!u) return;

    if (!u->chosen_path) {
        /* short immediate message */
        IdleMsg *im = g_new0(IdleMsg, 1);
        im->u = u;
        im->success = FALSE;
        im->msg = g_strdup("Por favor escolha um arquivo CSV antes de enviar.");
        g_idle_add(idle_set_progress_msg_free, im);
        return;
    }

    /* disable & start */
    gtk_widget_set_sensitive(u->btn_upload, FALSE);
    /* clear previous status */
    gtk_image_set_from_icon_name(GTK_IMAGE(u->status_icon), NULL, GTK_ICON_SIZE_BUTTON);
    gtk_label_set_text(GTK_LABEL(u->status_label), "Iniciando upload...");

    /* run in background thread */
    GThread *t = g_thread_new("upload_worker", upload_worker, u);
    if (!t) {
        IdleMsg *im = g_new0(IdleMsg, 1);
        im->u = u;
        im->success = FALSE;
        im->msg = g_strdup("Erro ao iniciar thread de upload.");
        g_idle_add(idle_set_progress_msg_free, im);
        gtk_widget_set_sensitive(u->btn_upload, TRUE);
    } else {
        g_thread_unref(t);
    }
}

/* Build and show the dialog */
static void show_dataset_upload_dialog(GtkWindow *parent, EnvCtx *env) {
    (void)parent;
    const char *UPLOAD_CSS = parse_CSS_file("datasets_upload.css");

    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Upload dataset");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 320);

    /* titlebar Win95 com PNGs */
    install_upload_w95_titlebar(GTK_WINDOW(dialog), "Upload dataset");

    /* === CRIE O CONTENT, O BOX VERTICAL E O GRID AQUI === */
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *action  = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS

    gtk_widget_set_no_show_all(action, TRUE);
    gtk_widget_hide(action);

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(content), 0);
    if (GTK_IS_BOX(content)) gtk_box_set_spacing(GTK_BOX(content), 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing   (GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    /* ==================================================== */

    /* file chooser row */
    GtkWidget *lbl_file   = gtk_label_new("Arquivo:");
    GtkWidget *file_label = gtk_label_new("(nenhum arquivo selecionado)");
    gtk_label_set_xalign(GTK_LABEL(file_label), 0.0);
    GtkWidget *btn_choose = gtk_button_new_with_label("Escolher CSV...");

    gtk_grid_attach(GTK_GRID(grid), lbl_file,    0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), file_label,  1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_choose,  2, 0, 1, 1);

    /* metadata rows */
    GtkWidget *lbl_nome  = gtk_label_new("Nome do dataset:");
    GtkWidget *entry_nome = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), lbl_nome,   0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_nome, 1, 1, 2, 1);

    GtkWidget *lbl_desc  = gtk_label_new("Descrição:");
    GtkWidget *entry_desc = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_desc), "Breve descrição (opcional)");
    gtk_grid_attach(GTK_GRID(grid), lbl_desc,   0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_desc, 1, 2, 2, 1);

    /* uploader info */
    int uid = 0; const char *uname = NULL; const char *uemail = NULL;
    if (env) { uid = env->current_user_id; uname = env->current_user_name; uemail = env->current_user_email; }
    char uploader_info[256];
    if (uid > 0) {
        if (uname && uemail) snprintf(uploader_info, sizeof(uploader_info), "Enviando como: %s %s", uname, uemail);
        else if (uname)      snprintf(uploader_info, sizeof(uploader_info), "Enviando como: %s", uname);
        else if (uemail)     snprintf(uploader_info, sizeof(uploader_info), "Enviando como: %s", uemail);
        else                 snprintf(uploader_info, sizeof(uploader_info), "Enviando como usuário %d", uid);
    } else snprintf(uploader_info, sizeof(uploader_info), "Não autenticado");

    GtkWidget *lbl_uploader = gtk_label_new(uploader_info);
    gtk_label_set_xalign(GTK_LABEL(lbl_uploader), 0.0);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(" "), 0, 3, 1, 1); /* spacer */
    gtk_grid_attach(GTK_GRID(grid), lbl_uploader,       1, 3, 2, 1);

    /* status (ícone + label) */
    GtkWidget *status_box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *status_icon  = gtk_image_new();
    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_set_name(status_box, "status-box");
    gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "status-box");
    gtk_box_pack_start(GTK_BOX(status_box), status_icon,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE,  TRUE,  0);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Status:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), status_box,               1, 4, 2, 1);

    /* ações: Enviar (esq) + Fechar (dir) dentro da barra cinza */
    GtkWidget *h_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(h_actions, "upload-actions");
    GtkWidget *btn_upload = gtk_button_new_with_label("Enviar");
    GtkWidget *btn_close  = gtk_button_new_with_label("Fechar");
    gtk_box_pack_start(GTK_BOX(h_actions), btn_upload, FALSE, FALSE, 0);
    gtk_box_pack_end  (GTK_BOX(h_actions), btn_close,  FALSE, FALSE, 0);

    /* monta o container vertical */
    gtk_box_pack_start(GTK_BOX(v), grid,       TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(v), h_actions,  FALSE, FALSE, 0);

    /* aplica CSS escopado e adiciona ao content area */
    GtkWidget *wrapped = wrap_CSS(UPLOAD_CSS, "metal-panel", v, "upload-window");

    gtk_box_pack_start(GTK_BOX(content), wrapped, TRUE, TRUE, 0);
    gtk_widget_set_hexpand(wrapped, TRUE);
    gtk_widget_set_vexpand(wrapped, TRUE);

    /* opcional, ajuda também */
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_vexpand(v, TRUE);

    /* wiring/estado */
    UploadUI *u = g_new0(UploadUI, 1);
    u->dialog      = dialog;
    u->file_label  = file_label;
    u->btn_choose  = btn_choose;
    u->entry_nome  = entry_nome;
    u->entry_desc  = entry_desc;
    u->user_id     = uid;
    u->user_name   = uname ? g_strdup(uname)  : NULL;
    u->user_email  = uemail ? g_strdup(uemail): NULL;
    u->btn_upload  = btn_upload;
    u->status_box  = status_box;
    u->status_icon = status_icon;
    u->status_label= status_label;

    g_signal_connect(btn_choose, "clicked", G_CALLBACK(on_choose_file_clicked), u);
    g_signal_connect(btn_upload, "clicked", G_CALLBACK(on_upload_clicked),      u);
    g_signal_connect_swapped(btn_close,  "clicked", G_CALLBACK(gtk_widget_destroy), dialog);

    if (u->user_name && *u->user_name) {
        char hint[256]; snprintf(hint, sizeof(hint), "%s_dataset", u->user_name);
        gtk_entry_set_text(GTK_ENTRY(entry_nome), hint);
    }

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));

    if (u->chosen_path) g_free(u->chosen_path);
    if (u->user_name)   g_free(u->user_name);
    if (u->user_email)  g_free(u->user_email);
    g_free(u);
    gtk_widget_destroy(dialog);
}


#endif
