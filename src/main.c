// gcc main.c -o main $(pkg-config --cflags --libs gtk+-3.0) -lcurl -lcjson

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

// ====== incluir seu comunicador (o código que você passou) ======
#include "interface/communicator.h"

// ====== Metal Theme CSS ======
static const char *METAL_CSS =
"/* Base window background (Metal-like brushed gray) */\n"
"window, dialog, .background {"
"  background-image: linear-gradient(to bottom, #d0d0d0, rgba(189, 189, 189, 1));"
"}\n"
"\n"
"/* Panels / frames with light bevel */\n"
".metal-panel {"
"  background-image: linear-gradient(to bottom, #cfcfcf, #b9b9b9);"
"  border: 1px solid #7f7f7f;"
"  box-shadow: inset 1px 1px 0px 0px #ffffff, inset -1px -1px 0px 0px #808080;"
"  padding: 10px;"
"  border-radius: 2px;"
"}\n"
"\n"
"/* Frames (group boxes) */\n"
"frame > label { font-weight: bold; padding: 0 4px; }\n"
"frame > border {"
"  border: 1px solid #7f7f7f;"
"  box-shadow: inset 1px 1px 0 0 #ffffff, inset -1px -1px 0 0 #808080;"
"  background-image: linear-gradient(to bottom, #cfcfcf, #b9b9b9);"
"}\n"
"/* Buttons: flat metal gradient with beveled edges */\n"
"button {"
"  background-image: linear-gradient(to bottom, #e7e7e7, #c9c9c9);"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #ffffff, inset -1px -1px 0 0 #808080;"
"  padding: 4px 10px;"
"}\n"
"button:hover {"
"  background-image: linear-gradient(to bottom, #f0f0f0, #d5d5d5);"
"}\n"
"button:active {"
"  background-image: linear-gradient(to bottom, #c9c9c9, #b8b8b8);"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"}\n"
"button:disabled {"
"  opacity: 0.6;"
"}\n"
"\n"
"/* Text entries: sunken look */\n"
"entry, spinbutton, combobox, combobox entry {"
"  background: #ffffff;"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"  padding: 4px;"
"}\n"
"entry:focus {"
"  border-color: #2a5db0;"
"}\n"
"\n"
"/* Notebook (tabs) */\n"
"notebook > header {"
"  background-image: linear-gradient(to bottom, #d2d2d2, #c2c2c2);"
"  border-bottom: 1px solid #7a7a7a;"
"}\n"
"notebook tab {"
"  background-image: linear-gradient(to bottom, #e2e2e2, #cdcdcd);"
"  border: 1px solid #7a7a7a;"
"  margin: 2px;"
"  padding: 4px 8px;"
"}\n"
"notebook tab:checked {"
"  background-image: linear-gradient(to bottom, #f2f2f2, #dbdbdb);"
"}\n"
"\n"
"/* TreeView header */\n"
"treeview header button {"
"  background-image: linear-gradient(to bottom, #e3e3e3, #cfcfcf);"
"  border: 1px solid #7a7a7a;"
"  padding: 4px 6px;"
"  font-weight: bold;"
"}\n"
"\n"
"/* Progressbar */\n"
"progressbar trough {"
"  border: 1px solid #7a7a7a;"
"  box-shadow: inset 1px 1px 0 0 #808080, inset -1px -1px 0 0 #ffffff;"
"}\n"
"progressbar progress {"
"  background-image: linear-gradient(to bottom, #9fbef7, #5582d0);"
"}\n"
"\n"
"/* Scrollbars a bit chunkier for desktop */\n"
"scrollbar slider {"
"  background-image: linear-gradient(to bottom, #dcdcdc, #c4c4c4);"
"  border: 1px solid #7a7a7a;"
"}\n";

// aplicar estilo
static void apply_metal_theme(void) {
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, METAL_CSS, -1, NULL);
    GdkScreen *scr = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(scr,
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(prov);
}

static GtkWidget* metal_wrap(GtkWidget *child, const char *name_opt) {
    // container que recebe a classe .metal-panel do seu CSS
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (name_opt && *name_opt) {
        gtk_widget_set_name(box, name_opt);   // opcional: dá um "name" para CSS
    }
    GtkStyleContext *sc = gtk_widget_get_style_context(box);
    gtk_style_context_add_class(sc, "metal-panel"); // usa a regra já definida no METAL_CSS
    gtk_container_set_border_width(GTK_CONTAINER(box), 6); // respiro
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0); // coloca o conteúdo dentro
    return box;
}

// ==== Contexto do Tab ====
typedef struct {
    GtkEntry     *entry;
    GtkTreeView  *view;
    GtkListStore *store;
} TabCtx;



/* forward declare EnvCtx (se já não existir) */
#ifndef ENVCTX_DEFINED
typedef struct {
    GtkComboBoxText *ds_combo;
    GtkComboBoxText *model_combo;
    GtkComboBoxText *algo_combo;
    GtkSpinButton   *train_spn;
    GtkSpinButton   *val_spn;
    GtkSpinButton   *test_spn;
    GtkEntry        *x_feat;
    GtkEntry        *y_feat;
    GtkCheckButton  *scale_chk;
    GtkCheckButton  *impute_chk;

    GtkButton       *btn_train;
    GtkButton       *btn_validate;
    GtkButton       *btn_test;
    GtkButton       *btn_refresh_ds;

    GtkNotebook     *right_nb;
    GtkTreeView     *preview_view;
    GtkImage        *plot_img;
    GtkTextView     *logs_view;
    GtkListStore    *preview_store;
    GtkButton       *btn_logout;
    GtkButton       *btn_play;

    GtkProgressBar  *progress;
    GtkLabel        *status;

    GtkStack        *stack;
    GtkWidget       *main_window;


    GtkScale        *split_scale;      /* slider (GtkRange) */
    GtkEntry        *split_entry;      /* text entry (accepts comma) */
    GtkLabel        *split_train_lbl;
    GtkLabel        *split_test_lbl;
    gboolean         split_lock;
} EnvCtx;
#define ENVCTX_DEFINED
#endif

/* Forward declarations of your existing functions (if not already visible) */
static TabCtx* add_datasets_tab(GtkNotebook *nb);
void add_environment_tab(GtkNotebook *nb, EnvCtx *ctx);


static GtkWidget* create_main_window(void) {
    /* apply metal theme for main window (already defined in your file) */
    apply_metal_theme();

    EnvCtx *env = g_new0(EnvCtx, 1);

    GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_win), "Metal API Client");
    gtk_window_set_default_size(GTK_WINDOW(main_win), 800, 600);
    g_signal_connect(main_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    env->main_window = main_win;

    GtkWidget *nb = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(main_win), nb);

    /* Datasets tab creates its own TabCtx internally */
    add_datasets_tab(GTK_NOTEBOOK(nb));
    /* Environment gets the EnvCtx pointer so callbacks can access main_window, status, etc. */
    add_environment_tab(GTK_NOTEBOOK(nb), env);

    gtk_widget_show_all(main_win);
    return main_win;
}


/* Simple login context */
typedef struct {
    GtkWidget *window;
    GtkWidget *email_entry;
    GtkWidget *pass_entry;
    GtkWidget *status_label;

     // aba cadastro
    GtkWidget *reg_nome_entry;
    GtkWidget *reg_email_entry;
    GtkWidget *reg_pass_entry;
    GtkWidget *reg_status_label;
} LoginCtx;

static void on_register_button_clicked(GtkButton *button, LoginCtx *ctx) {
    const char *nome = gtk_entry_get_text(GTK_ENTRY(ctx->reg_nome_entry));
    const char *email = gtk_entry_get_text(GTK_ENTRY(ctx->reg_email_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(ctx->reg_pass_entry));

    if(strlen(nome)==0 || strlen(email)==0 || strlen(password)==0) {
        gtk_label_set_text(GTK_LABEL(ctx->reg_status_label), "Preencha todos os campos!");
        return;
    }

    CURL *curl = curl_easy_init();
    if(curl) {
        CURLcode res;
        char data[512];
        snprintf(data, sizeof(data),
                 "{\"nome\":\"%s\",\"email\":\"%s\",\"password\":\"%s\"}",
                 nome, email, password);

        curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:5000/user");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            gtk_label_set_text(GTK_LABEL(ctx->reg_status_label), "Erro ao criar usuário!");
        } else {
            gtk_label_set_text(GTK_LABEL(ctx->reg_status_label), "Usuário criado com sucesso!");
            gtk_entry_set_text(GTK_ENTRY(ctx->reg_nome_entry), "");
            gtk_entry_set_text(GTK_ENTRY(ctx->reg_email_entry), "");
            gtk_entry_set_text(GTK_ENTRY(ctx->reg_pass_entry), "");
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}



/* Build and show login window. On success it creates the main window and destroys the login window. */
static void on_login_button_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    LoginCtx *ctx = (LoginCtx*)user_data;
    const char *email = gtk_entry_get_text(GTK_ENTRY(ctx->email_entry));
    const char *pass  = gtk_entry_get_text(GTK_ENTRY(ctx->pass_entry));

    if (!email || !*email || !pass || !*pass) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Preencha email e senha");
        return;
    }

    /* build JSON payload */
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"email\":\"%s\",\"password\":\"%s\"}", email, pass);

    char cmd_utf8[600];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "LOGIN %s", payload);

    /* convert to WCHAR */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    WCHAR *wcmd = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);

    /* run communicator */
    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);

    if (!wresp) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Erro: sem resposta do servidor");
        return;
    }

    /* convert back to UTF-8 */
    int rlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    char *resp = (char*)malloc(rlen);
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, rlen, NULL, NULL);
    free(wresp);

    /* communicator/process_api_response returns a string starting with "OK" on success */
    if (strncmp(resp, "OK", 2) == 0) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Login OK — abrindo app...");

        /* create main window and show it */
        GtkWidget *main_win = create_main_window();
        gtk_widget_show_all(main_win);

        /* hide (não destruir) a janela de login para não disparar gtk_main_quit */
        gtk_widget_hide(ctx->window);

        /* OBS: não fazemos g_free(ctx) aqui para evitar dangling pointer de signals ainda ligados
           (poderíamos desconectar sinais e liberar); se quiser, eu mostro a versão que desconecta e libera */
        free(resp);
        return;
    }

    /* otherwise parse error message (starts with ERR ...) */
    if (strncmp(resp, "ERR", 3) == 0) {
        const char *p = resp + 3;
        while (*p == ' ' || *p == '\t') ++p;
        gtk_label_set_text(GTK_LABEL(ctx->status_label), p);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Credenciais inválidas");
    }

    free(resp);
}


/* CSS extra para a tela de login (melhor tipografia, painel centralizado, botão maior) */
static const char *LOGIN_CSS =
"#login-window { background-image: linear-gradient(to bottom, #d8d8d8, #cfcfcf); }"
"#login-panel {"
"  border-radius: 6px;"
"  padding: 20px;"
"  min-width: 420px;"
"  max-width: 720px;"
"  box-shadow: 0 4px 12px rgba(0,0,0,0.15);"
"}"
"#login-window label.title {"
"  font-size: 20px;"
"  font-weight: bold;"
"  margin-bottom: 8px;"
"}"
"#login-window entry {"
"  min-height: 28px;"
"  padding: 6px;"
"}"
"#login-window button#login-btn {"
"  min-height: 36px;"
"  padding: 6px 14px;"
"  font-size: 14px;"
"}"
"#login-window label.status {"
"  color: #4a4a4a;"
"  font-size: 12px;"
"  margin-top: 6px;"
"}";

/* aplica somente o CSS extra para a tela de login (chame dentro create_login_window) */
static void apply_login_css(void) {
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, LOGIN_CSS, -1, NULL);
    GdkScreen *scr = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(scr,
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 5);
    g_object_unref(prov);
}

static GtkWidget* create_login_window(void) {
    apply_metal_theme();
    apply_login_css();  // CSS extra do login

    LoginCtx *ctx = g_new0(LoginCtx, 1);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Login / Cadastro");

    // Tela cheia proporcional
    GdkScreen *screen = gdk_screen_get_default();
    gint sw = gdk_screen_get_width(screen);
    gint sh = gdk_screen_get_height(screen);
    gtk_window_set_default_size(GTK_WINDOW(win),
        CLAMP(sw*0.45, 420, 1200), CLAMP(sh*0.4, 320, 900));
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(win), 12);
    gtk_widget_set_name(win, "login-window");

    // Notebook para abas
    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(win), notebook);

    // ================= LOGIN TAB =================
    GtkWidget *login_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(login_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(login_grid), 12);

    GtkWidget *lbl_login_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_login_title),
                         "<span size='xx-large' weight='bold'>Login</span>");
    gtk_grid_attach(GTK_GRID(login_grid), lbl_login_title, 0, 0, 2, 1);
    gtk_widget_set_halign(lbl_login_title, GTK_ALIGN_CENTER);

    GtkWidget *lbl_email = gtk_label_new("Email:");
    ctx->email_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->email_entry), "seu@email.com");
    gtk_widget_set_hexpand(ctx->email_entry, TRUE);
    gtk_grid_attach(GTK_GRID(login_grid), lbl_email, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(login_grid), ctx->email_entry, 1, 1, 1, 1);

    GtkWidget *lbl_pass = gtk_label_new("Senha:");
    ctx->pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(ctx->pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->pass_entry), "••••••");
    gtk_widget_set_hexpand(ctx->pass_entry, TRUE);
    gtk_grid_attach(GTK_GRID(login_grid), lbl_pass, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(login_grid), ctx->pass_entry, 1, 2, 1, 1);

    GtkWidget *btn_login = gtk_button_new_with_label("Entrar");
    gtk_widget_set_name(btn_login, "login-btn");
    gtk_widget_set_hexpand(btn_login, TRUE);
    gtk_grid_attach(GTK_GRID(login_grid), btn_login, 0, 3, 2, 1);

    ctx->status_label = gtk_label_new("");
    gtk_widget_set_name(ctx->status_label, "status");
    gtk_grid_attach(GTK_GRID(login_grid), ctx->status_label, 0, 4, 2, 1);

    GtkWidget *login_panel = metal_wrap(login_grid, "login-panel");

    // Centralizar a aba
    GtkWidget *login_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(login_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(login_box, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(login_box), login_panel);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), login_box, gtk_label_new("Login"));

    // ================= REGISTER TAB =================
    GtkWidget *reg_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(reg_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(reg_grid), 12);

    GtkWidget *lbl_reg_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_reg_title),
                         "<span size='xx-large' weight='bold'>Cadastro</span>");
    gtk_grid_attach(GTK_GRID(reg_grid), lbl_reg_title, 0, 0, 2, 1);
    gtk_widget_set_halign(lbl_reg_title, GTK_ALIGN_CENTER);

    GtkWidget *reg_nome_label = gtk_label_new("Nome:");
    ctx->reg_nome_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->reg_nome_entry), "Seu nome completo");
    gtk_widget_set_hexpand(ctx->reg_nome_entry, TRUE);
    gtk_grid_attach(GTK_GRID(reg_grid), reg_nome_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(reg_grid), ctx->reg_nome_entry, 1, 1, 1, 1);

    GtkWidget *reg_email_label = gtk_label_new("Email:");
    ctx->reg_email_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->reg_email_entry), "email@dominio.com");
    gtk_widget_set_hexpand(ctx->reg_email_entry, TRUE);
    gtk_grid_attach(GTK_GRID(reg_grid), reg_email_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(reg_grid), ctx->reg_email_entry, 1, 2, 1, 1);

    GtkWidget *reg_pass_label = gtk_label_new("Senha:");
    ctx->reg_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(ctx->reg_pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->reg_pass_entry), "••••••");
    gtk_widget_set_hexpand(ctx->reg_pass_entry, TRUE);
    gtk_grid_attach(GTK_GRID(reg_grid), reg_pass_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(reg_grid), ctx->reg_pass_entry, 1, 3, 1, 1);

    GtkWidget *btn_register = gtk_button_new_with_label("Cadastrar");
    gtk_widget_set_hexpand(btn_register, TRUE);
    gtk_grid_attach(GTK_GRID(reg_grid), btn_register, 0, 4, 2, 1);

    ctx->reg_status_label = gtk_label_new("");
    gtk_widget_set_name(ctx->reg_status_label, "status");
    gtk_grid_attach(GTK_GRID(reg_grid), ctx->reg_status_label, 0, 5, 2, 1);

    GtkWidget *reg_panel = metal_wrap(reg_grid, "login-panel");

    GtkWidget *reg_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(reg_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(reg_box, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(reg_box), reg_panel);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), reg_box, gtk_label_new("Cadastro"));

    // ================= SIGNALS =================
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login_button_clicked), ctx);
    g_signal_connect(ctx->pass_entry, "activate", G_CALLBACK(on_login_button_clicked), ctx);
    g_signal_connect(btn_register, "clicked", G_CALLBACK(on_register_button_clicked), ctx);

    // Fecha app ao fechar janela
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(win);
    return win;
}




/* ---------- novo callback de refresh que popula o TreeView ---------- */
static void refresh_datasets_cb(GtkWidget *btn, gpointer user_data) {
    TabCtx *ctx = (TabCtx*)user_data;
    char *resp = NULL;

    if (!api_dump_table("dataset", &resp) || !resp) {
        fprintf(stderr, "Erro: api_dump_table retornou NULL\n");
        if (resp) free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        fprintf(stderr, "Erro: JSON inválido da API\n");
        return;
    }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        fprintf(stderr, "API retornou erro: %s\n", cJSON_IsString(msg) ? msg->valuestring : "(sem mensagem)");
        cJSON_Delete(root);
        return;
    }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) {
        fprintf(stderr, "Resposta sem 'columns' ou 'data' corretos\n");
        cJSON_Delete(root);
        return;
    }

    int ncols = cJSON_GetArraySize(columns);
    if (ncols <= 0) { cJSON_Delete(root); return; }

    /* cria um array de tipos (usar strings para exibição simples) */
    GType *types = g_new0(GType, ncols);
    for (int i = 0; i < ncols; ++i) types[i] = G_TYPE_STRING;

    /* cria novo store (ou substitui o anterior) */
    GtkListStore *new_store = gtk_list_store_newv(ncols, types);
    g_free(types);

    /* preenche as linhas (cada row é um array no JSON) */
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;
        GtkTreeIter iter;
        gtk_list_store_append(new_store, &iter);

        int colidx = 0;
        cJSON *cell;
        cJSON_ArrayForEach(cell, row) {
            char buf[1024];
            if (cJSON_IsString(cell)) {
                snprintf(buf, sizeof(buf), "%s", cell->valuestring);
            } else if (cJSON_IsNumber(cell)) {
                /* imprime inteiro quando possível, senão double */
                if (cell->valuedouble == cell->valueint)
                    snprintf(buf, sizeof(buf), "%d", cell->valueint);
                else
                    snprintf(buf, sizeof(buf), "%g", cell->valuedouble);
            } else if (cJSON_IsBool(cell)) {
                snprintf(buf, sizeof(buf), "%s", cJSON_IsTrue(cell) ? "1" : "0");
            } else if (cJSON_IsNull(cell)) {
                snprintf(buf, sizeof(buf), "");
            } else {
                snprintf(buf, sizeof(buf), "");
            }
            gtk_list_store_set(new_store, &iter, colidx, buf, -1);
            colidx++;
        }
    }

    /* troca o modelo no TreeView */
    if (ctx->store) {
        /* desconecta model antigo e libera */
        gtk_tree_view_set_model(ctx->view, NULL);
        g_object_unref(ctx->store);
        ctx->store = NULL;
    }
    ctx->store = new_store;
    gtk_tree_view_set_model(ctx->view, GTK_TREE_MODEL(ctx->store));

    /* cria colunas do TreeView se ainda não existirem (nomeadas conforme 'columns') */
    if (gtk_tree_view_get_n_columns(GTK_TREE_VIEW(ctx->view)) == 0) {
        int colidx = 0;
        cJSON *col;
        cJSON_ArrayForEach(col, columns) {
            const char *colname = cJSON_IsString(col) ? col->valuestring : "col";
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(ctx->view),
                                                       -1,
                                                       colname,
                                                       renderer,
                                                       "text", colidx,
                                                       NULL);
            colidx++;
        }
    }

    cJSON_Delete(root);
}

/* ---------- ajuste no add_datasets_tab: ligar o botão ao novo callback ---------- */
static TabCtx* add_datasets_tab(GtkNotebook *nb) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 6);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filtrar dataset...");
    gtk_box_pack_start(GTK_BOX(top), entry, TRUE, TRUE, 0);

    GtkWidget *btn_refresh = gtk_button_new_with_label("🔄 Atualizar");
    gtk_box_pack_start(GTK_BOX(top), btn_refresh, FALSE, FALSE, 0);

    GtkWidget *tree = gtk_tree_view_new();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    gtk_box_pack_start(GTK_BOX(outer), metal_wrap(top, "env_top"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), metal_wrap(scroll, "env_scroll"), TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, outer, lbl);

    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry = GTK_ENTRY(entry);
    ctx->view  = GTK_TREE_VIEW(tree);
    ctx->store = NULL;

    /* conectar ao novo callback */
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(refresh_datasets_cb), ctx);

    gtk_widget_show_all(outer);

    /* opcional: faz o primeiro refresh automático */
    refresh_datasets_cb(NULL, ctx);

    return ctx;
}

static GtkWidget* group_panel(const char *title, GtkWidget *content) {
    GtkWidget *frame = gtk_frame_new(title);               // título do “box”
    gtk_frame_set_label_align(GTK_FRAME(frame), 0.02, 0.5); // título à esquerda
    gtk_container_set_border_width(GTK_CONTAINER(frame), 6);

    // põe sua moldura “metal” por dentro, pra dar padding e relevo
    GtkWidget *inner = metal_wrap(content, NULL);
    gtk_container_add(GTK_CONTAINER(frame), inner);

    // um respiro externo
    gtk_widget_set_margin_top   (frame, 4);
    gtk_widget_set_margin_bottom(frame, 4);
    return frame;
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

/* callbacks (minimal, non-functional as requested) */
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

/* show "not implemented" dialog (optional) */
static void not_impl_cb(GtkButton *b, gpointer ud) {
    GtkWidget *dlg = gtk_message_dialog_new(NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Função não implementada ainda.");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void set_split_ui(EnvCtx *ctx, double train) {
    if (!ctx) return;
    if (train < 0) { train = 0; }; if (train > 100) { train = 100; };
    ctx->split_lock = TRUE;

    gtk_range_set_value(GTK_RANGE(ctx->split_scale), train);

    char lbuf[32], ebuf[16];
    g_snprintf(lbuf, sizeof lbuf, "Train %.1f%%", train);
    gtk_label_set_text(ctx->split_train_lbl, lbuf);
    g_snprintf(lbuf, sizeof lbuf, "Test %.1f%%", 100.0 - train);
    gtk_label_set_text(ctx->split_test_lbl, lbuf);

    g_snprintf(ebuf, sizeof ebuf, "%.1f", train);
    for (char *p=ebuf; *p; ++p) if (*p=='.') *p=',';          /* ponto -> vírgula */
    gtk_entry_set_text(GTK_ENTRY(ctx->split_entry), ebuf);

    ctx->split_lock = FALSE;
}

static gboolean parse_percent_entry(const char *txt, double *out) {
    if (!txt) return FALSE;
    char buf[32]; g_strlcpy(buf, txt, sizeof buf);
    for (char *p=buf; *p; ++p) if (*p==',') *p='.';           /* vírgula -> ponto */
    char *end=NULL; double v = g_ascii_strtod(buf, &end);
    if (end==buf) return FALSE;
    if (v < 0) { v = 0; }; if (v > 100) { v = 100; };
    *out=v; return TRUE;
}


/* Entry mudou -> aplica no slider (aceita vírgula) */
static void on_split_entry_changed(GtkEditable *editable, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    if (ctx->split_lock) return;
    double train;
    if (!parse_percent_entry(gtk_entry_get_text(GTK_ENTRY(editable)), &train)) return;
    set_split_ui(ctx, train);
}

/* add_environment_tab implementation (style preserved) */
/* Converte texto com vírgula/ponto para double 0..100 */

/* Mantém slider, labels e entry sincronizados (com trava) */

/* Slider mudou -> atualiza labels e entry */
static void on_split_changed(GtkRange *range, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (ctx->split_lock) return;
    set_split_ui(ctx, gtk_range_get_value(range));
}

/* Entry mudou -> aplica no slider (aceita vírgula) */


static void on_train_clicked(GtkButton *b, gpointer user) {
    (void)b;
    EnvCtx *ctx = (EnvCtx*)user;
    /* Atualiza o label de status, se existir */
    if (ctx && ctx->status) {
        gtk_label_set_text(GTK_LABEL(ctx->status), "Em desenvolvimento");
    }

    /* Determina janela-pai (se disponível) para modal */
    GtkWindow *parent = NULL;
    if (ctx && ctx->main_window && GTK_IS_WINDOW(ctx->main_window)) {
        parent = GTK_WINDOW(ctx->main_window);
    } else if (ctx && ctx->status) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(ctx->status));
        if (toplevel && GTK_IS_WINDOW(toplevel)) parent = GTK_WINDOW(toplevel);
    }

    /* Dialog simples informando que está em desenvolvimento */
    GtkWidget *dlg = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Em desenvolvimento — recurso ainda não implementado.");
    gtk_window_set_title(GTK_WINDOW(dlg), "Aviso");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}


/* Build the Environment tab */
void add_environment_tab(GtkNotebook *nb, EnvCtx *ctx) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* stack switcher (top) */
    ctx->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(ctx->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), ctx->stack);
    gtk_box_pack_start(GTK_BOX(outer), switcher, FALSE, FALSE, 0);

    /* main split */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), paned, TRUE, TRUE, 0);

    /* LEFT: controls inside a Metal panel */
    GtkWidget *left_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // Adicione o botão de logout na barra de ferramentas
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);
    
    // Botão de logout (inicialmente escondido)
    ctx->btn_logout = GTK_BUTTON(gtk_button_new_with_label("Logout"));
    gtk_box_pack_end(GTK_BOX(toolbar), GTK_WIDGET(ctx->btn_logout), FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(GTK_WIDGET(ctx->btn_logout), "Sair da conta");
    gtk_widget_hide(GTK_WIDGET(ctx->btn_logout));
    
    /* dataset row + refresh + load */
    GtkWidget *ds_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->ds_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    ctx->btn_refresh_ds = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
    GtkWidget *btn_load = gtk_button_new_with_label("Load");
    gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->ds_combo), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->btn_refresh_ds), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ds_row), btn_load, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), ds_row, FALSE, FALSE, 0);

    /* trainees row */
    GtkWidget *tr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->model_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(ctx->model_combo, "(new)");
    gtk_box_pack_start(GTK_BOX(tr_row), gtk_label_new(""), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tr_row), GTK_WIDGET(ctx->model_combo), TRUE, TRUE, 0);
    /* ERA: gtk_box_pack_start(GTK_BOX(left_content), tr_row, FALSE, FALSE, 0); */
    gtk_box_pack_start(GTK_BOX(left_content), group_panel("Trainee", tr_row), FALSE, FALSE, 0);

    /* algo + params */
    GtkWidget *algo_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->algo_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(ctx->algo_combo, "linreg");
    gtk_combo_box_text_append_text(ctx->algo_combo, "ridge");
    gtk_combo_box_text_append_text(ctx->algo_combo, "lasso");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->algo_combo), 0);
    gtk_box_pack_start(GTK_BOX(algo_row), gtk_label_new(""), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(algo_row), GTK_WIDGET(ctx->algo_combo), TRUE, TRUE, 0);
    /* ERA: gtk_box_pack_start(GTK_BOX(left_content), algo_row, FALSE, FALSE, 0); */
    gtk_box_pack_start(GTK_BOX(left_content), group_panel("Regressor", algo_row), FALSE, FALSE, 0);

    /* split sliders */
    /* split slider (Train/Test) – soma sempre 100% */
/* === Split (Train/Test) com entry central + slider ===================== */
GtkWidget *split_box    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

/* linha superior: [Train ...] [ entry ] [ ... Test] */
GtkWidget *split_labels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
GtkWidget *lab_tr       = gtk_label_new("Train 70,0%");
GtkWidget *entry        = gtk_entry_new();
gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
gtk_widget_set_size_request(entry, 70, -1);
gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "70,0");
GtkWidget *lab_te       = gtk_label_new("Test 30,0%");

ctx->split_train_lbl = GTK_LABEL(lab_tr);
ctx->split_test_lbl  = GTK_LABEL(lab_te);
ctx->split_entry     = GTK_ENTRY(entry);

gtk_box_pack_start(GTK_BOX(split_labels), lab_tr, FALSE, FALSE, 0);
gtk_box_pack_end  (GTK_BOX(split_labels), lab_te, FALSE, FALSE, 0);
gtk_box_set_center_widget(GTK_BOX(split_labels), entry);

/* slider (0..100) = Train% com passo 0.1 */
GtkAdjustment *split_adj = gtk_adjustment_new(70.0, 0.0, 100.0, 0.1, 1.0, 0.0);
GtkWidget     *split_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, split_adj);
ctx->split_scale = GTK_SCALE(split_scale);
gtk_scale_set_draw_value(GTK_SCALE(split_scale), FALSE);
gtk_scale_add_mark(GTK_SCALE(split_scale), 50.0, GTK_POS_BOTTOM, NULL);
gtk_widget_set_hexpand(split_scale, TRUE);

g_signal_connect(split_scale, "value-changed", G_CALLBACK(on_split_changed), ctx);
g_signal_connect(entry,       "changed",       G_CALLBACK(on_split_entry_changed), ctx);

/* monta painel e inicializa em 70/30 */
gtk_box_pack_start(GTK_BOX(split_box), split_labels, FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(split_box), split_scale,  FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(left_content),
                   group_panel("Split (Train%/Test%)", split_box),
                   FALSE, FALSE, 0);
set_split_ui(ctx, 70.0);

    /* features + preproc */
    GtkWidget *xy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->x_feat = GTK_ENTRY(gtk_entry_new());
    ctx->y_feat = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(ctx->x_feat, "X feature (e.g., sepal_length)");
    gtk_entry_set_placeholder_text(ctx->y_feat, "Y target (e.g., price)");
    gtk_box_pack_start(GTK_BOX(xy_row), GTK_WIDGET(ctx->x_feat), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(xy_row), GTK_WIDGET(ctx->y_feat), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), xy_row, FALSE, FALSE, 0);

    ctx->impute_chk = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Impute (median)"));
    ctx->scale_chk  = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Scale (standard)"));
    gtk_box_pack_start(GTK_BOX(left_content), GTK_WIDGET(ctx->impute_chk), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), GTK_WIDGET(ctx->scale_chk),  FALSE, FALSE, 0);

    /* action buttons */
    /* === Botão único Play =================================================== */
    GtkWidget *act_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_play = gtk_button_new_with_label("▶ Run");
    ctx->btn_play = GTK_BUTTON(btn_play);
    gtk_box_pack_start(GTK_BOX(act_row), btn_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_content), act_row, FALSE, FALSE, 0);


    /* Wrap left in metal panel and pack into paned */
    GtkWidget *left_panel = metal_wrap(left_content, "env-left-panel");
    gtk_paned_pack1(GTK_PANED(paned), left_panel, FALSE, FALSE);

    /* RIGHT: notebook content; the theme styles tabs */
    GtkWidget *right_nb = gtk_notebook_new();
    ctx->right_nb = GTK_NOTEBOOK(right_nb);

    /* Preview */
    GtkWidget *tv = gtk_tree_view_new();
    ctx->preview_view = GTK_TREE_VIEW(tv);
    GtkWidget *scroller_preview = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroller_preview), tv);
    gtk_notebook_append_page(ctx->right_nb, scroller_preview, gtk_label_new("Preview"));

    /* Logs */
    ctx->logs_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(ctx->logs_view, FALSE);
    GtkWidget *scroller_logs = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroller_logs), GTK_WIDGET(ctx->logs_view));
    gtk_notebook_append_page(ctx->right_nb, scroller_logs, gtk_label_new("Logs"));

    /* Plot */
    ctx->plot_img = GTK_IMAGE(gtk_image_new());
    GtkWidget *plot_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(plot_box), GTK_WIDGET(ctx->plot_img), TRUE, TRUE, 0);
    gtk_notebook_append_page(ctx->right_nb, plot_box, gtk_label_new("Plot"));

    /* Metrics */
    GtkWidget *metrics_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(metrics_view), FALSE);
    gtk_notebook_append_page(ctx->right_nb, metrics_view, gtk_label_new("Metrics"));

    /* Wrap right in panel for consistent depth */
    GtkWidget *right_panel = metal_wrap(right_nb, "env-right-panel");
    gtk_paned_pack2(GTK_PANED(paned), right_panel, TRUE, FALSE);

    /* footer */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    ctx->status   = GTK_LABEL(gtk_label_new("Idle"));
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->progress), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->status), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), metal_wrap(footer, "env-footer"), FALSE, FALSE, 0);

    /* stack pages */
    gtk_stack_add_titled(ctx->stack, paned, "preproc",   "Pre-processing");
    gtk_stack_add_titled(ctx->stack, paned, "regressor", "Regression");
    gtk_stack_add_titled(ctx->stack, paned, "view",      "View");

    /* signals */
    g_signal_connect(ctx->btn_refresh_ds, "clicked", G_CALLBACK(on_refresh_datasets), ctx);
    g_signal_connect(btn_load,       "clicked", G_CALLBACK(on_load_dataset),     ctx);
    g_signal_connect(btn_play,       "clicked", G_CALLBACK(on_train_clicked),    ctx);

    /* mount into notebook */
    GtkWidget *tab_lbl = gtk_label_new("Environment");
    gtk_notebook_append_page(nb, outer, tab_lbl);
    gtk_widget_show_all(outer);

    /* initial population */
    on_refresh_datasets(GTK_BUTTON(ctx->btn_refresh_ds), ctx);
}

// ==== Main Window ====
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    /* NOTA: aplicamos tema metal na criação da main window; o login tem CSS próprio */
    /* show login first */
    GtkWidget *login = create_login_window();
    gtk_widget_show_all(login);

    gtk_main();
    return 0;
}
