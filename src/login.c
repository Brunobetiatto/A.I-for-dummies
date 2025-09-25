#include "login.h"
#include "interface/communicator.h"
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#include <time.h>
#include "main.h"

// CSS para a tela de login
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

// Função para aplicar o CSS do login
void apply_login_css(void) {
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, LOGIN_CSS, -1, NULL);
    GdkScreen *scr = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(scr,
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 5);
    g_object_unref(prov);
}

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

// Função para destruir a janela de login
static void on_login_window_destroy(GtkWidget *widget, gpointer user_data) {
    LoginCtx *ctx = (LoginCtx *)user_data;
    g_free(ctx);
}

// Timer da recuperação de senha (mantido igual)
static gboolean recovery_timer_cb(gpointer data) {
    LoginCtx *ctx = (LoginCtx*)data;
    if (!ctx) return G_SOURCE_CONTINUE;

    time_t now = time(NULL);
    gint remaining = (gint)(ctx->recovery_expiry - now);
    if (remaining < 0) remaining = 0;

    double fraction = 0.0;
    if (ctx->recovery_total_seconds > 0) {
        fraction = (double)remaining / (double)ctx->recovery_total_seconds;
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->recovery_progress), fraction);

    int mm = remaining / 60;
    int ss = remaining % 60;
    char text[64];
    snprintf(text, sizeof(text), "%02d:%02d restante", mm, ss);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->recovery_progress), text);

    if (remaining <= 0) {
        if (ctx->recovery_timer_id != 0) {
            g_source_remove(ctx->recovery_timer_id);
            ctx->recovery_timer_id = 0;
        }
        if (ctx->recovery_status_label)
            gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Código expirado");
        
        if (ctx->lbl_recovery_code) gtk_widget_hide(ctx->lbl_recovery_code);
        if (ctx->recovery_code_entry) gtk_widget_hide(ctx->recovery_code_entry);
        if (ctx->lbl_recovery_new_pass) gtk_widget_hide(ctx->lbl_recovery_new_pass);
        if (ctx->recovery_new_pass_entry) gtk_widget_hide(ctx->recovery_new_pass_entry);
        if (ctx->btn_recovery_verify) gtk_widget_hide(ctx->btn_recovery_verify);
        
        if (ctx->recovery_email_entry) gtk_widget_show(ctx->recovery_email_entry);
        if (ctx->btn_recovery_request) gtk_widget_show(ctx->btn_recovery_request);

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/* Remove timer se existir */
static void stop_recovery_timer(LoginCtx *ctx) {
    if (!ctx) return;
    if (ctx->recovery_timer_id != 0) {
        g_source_remove(ctx->recovery_timer_id);
        ctx->recovery_timer_id = 0;
    }
    if (ctx->recovery_progress) {
        gtk_widget_hide(ctx->recovery_progress);
    }
    ctx->recovery_expiry = 0;
    ctx->recovery_total_seconds = 0;
}


// Iniciar timer de recuperação
static void start_recovery_timer(LoginCtx *ctx, gint seconds) {
    if (!ctx) return;
    
    if (ctx->recovery_timer_id != 0) {
        g_source_remove(ctx->recovery_timer_id);
        ctx->recovery_timer_id = 0;
    }

    ctx->recovery_total_seconds = seconds;
    ctx->recovery_expiry = time(NULL) + seconds;

    if (ctx->recovery_progress) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->recovery_progress), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->recovery_progress), "15:00 restante");
        gtk_widget_show(ctx->recovery_progress);
    }

    if (ctx->recovery_email_entry) gtk_widget_hide(ctx->recovery_email_entry);
    if (ctx->btn_recovery_request) gtk_widget_hide(ctx->btn_recovery_request);
    if (ctx->lbl_recovery_code) gtk_widget_show(ctx->lbl_recovery_code);
    if (ctx->recovery_code_entry) gtk_widget_show(ctx->recovery_code_entry);
    if (ctx->lbl_recovery_new_pass) gtk_widget_show(ctx->lbl_recovery_new_pass);
    if (ctx->recovery_new_pass_entry) gtk_widget_show(ctx->recovery_new_pass_entry);
    if (ctx->btn_recovery_verify) gtk_widget_show(ctx->btn_recovery_verify);

    ctx->recovery_timer_id = g_timeout_add_seconds(1, recovery_timer_cb, ctx);
}

// Função de login
void on_login_button_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    LoginCtx *ctx = (LoginCtx*)user_data;
    const char *email = gtk_entry_get_text(GTK_ENTRY(ctx->email_entry));
    const char *pass  = gtk_entry_get_text(GTK_ENTRY(ctx->pass_entry));

    if (!email || !*email || !pass || !*pass) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Preencha email e senha");
        return;
    }

    char payload[512];
    snprintf(payload, sizeof(payload), "{\"email\":\"%s\",\"password\":\"%s\"}", email, pass);

    char cmd_utf8[600];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "LOGIN %s", payload);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    WCHAR *wcmd = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);

    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);

    if (!wresp) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Erro: sem resposta do servidor");
        return;
    }

    int rlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    char *resp = (char*)malloc(rlen);
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, rlen, NULL, NULL);
    free(wresp);

    if (strncmp(resp, "OK", 2) == 0) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Login OK — abrindo app...");
        while (gtk_events_pending())
            gtk_main_iteration();

        // Esta função precisa ser definida no main.c
        extern GtkWidget* create_main_window(void);
        GtkWidget *main_win = create_main_window();
        gtk_widget_show_all(main_win);

        gtk_widget_destroy(ctx->window);
        free(resp);
        return;
    }

    if (strncmp(resp, "ERR", 3) == 0) {
        const char *p = resp + 3;
        while (*p == ' ' || *p == '\t') ++p;
        gtk_label_set_text(GTK_LABEL(ctx->status_label), p);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Credenciais inválidas");
    }

    free(resp);
}

// Função de registro
void on_register_button_clicked(GtkButton *button, LoginCtx *ctx) {
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

// Função "Esqueci minha senha"
void on_forgot_clicked(GtkButton *btn, gpointer user_data) {
    LoginCtx *ctx = (LoginCtx*) user_data;
    if (!ctx) return;

    if (gtk_widget_get_parent(ctx->recovery_box) == NULL) {
        if (ctx->login_grid != NULL) {
            gtk_grid_attach(GTK_GRID(ctx->login_grid), ctx->recovery_box, 0, 6, 2, 1);
            gtk_widget_show_all(ctx->login_grid);
        } else {
            gtk_container_add(GTK_CONTAINER(ctx->window), ctx->recovery_box);
            gtk_widget_show_all(ctx->window);
        }
    } else {
        if (gtk_widget_get_visible(ctx->recovery_box))
            gtk_widget_hide(ctx->recovery_box);
        else
            gtk_widget_show(ctx->recovery_box);
    }

    if (ctx->lbl_recovery_code) gtk_widget_hide(ctx->lbl_recovery_code);
    if (ctx->recovery_code_entry) gtk_widget_hide(ctx->recovery_code_entry);
    if (ctx->lbl_recovery_new_pass) gtk_widget_hide(ctx->lbl_recovery_new_pass);
    if (ctx->recovery_new_pass_entry) gtk_widget_hide(ctx->recovery_new_pass_entry);
    if (ctx->btn_recovery_verify) gtk_widget_hide(ctx->btn_recovery_verify);
    if (ctx->recovery_progress) gtk_widget_hide(ctx->recovery_progress);

    if (ctx->recovery_email_entry) gtk_widget_grab_focus(ctx->recovery_email_entry);
}

// Função para solicitar código de recuperação
void on_recovery_request(GtkButton *btn, LoginCtx *ctx) {
    (void)btn;
    const char *email = gtk_entry_get_text(GTK_ENTRY(ctx->recovery_email_entry));
    
    if (!email || !*email) {
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Email é obrigatório");
        return;
    }
    
    // Construir payload para API
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"email\":\"%s\"}", email);
    
    char cmd_utf8[600];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "FORGOT_PASSWORD %s", payload);
    
    // Converter para wide char
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    WCHAR *wcmd = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);
    
    // Executar comando
    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);
    
    if (!wresp) {
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro: sem resposta do servidor");
        return;
    }
    
    // Converter resposta para UTF-8
    int rlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    if (rlen <= 0) {
        free(wresp);
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro na conversão da resposta");
        return;
    }

    char *resp = (char*)malloc(rlen);
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, rlen, NULL, NULL);
    free(wresp);

    if (!resp || strlen(resp) == 0) {
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro: resposta vazia do servidor");
        free(resp);
        return;
    }

    // ----- Clean: remove caracteres de controle indesejáveis e trim -----
    size_t resp_len = strlen(resp);
    char *clean = (char*)malloc(resp_len + 1);
    size_t ci = 0;
    for (size_t i = 0; i < resp_len; ++i) {
        unsigned char c = (unsigned char)resp[i];
        if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
            clean[ci++] = resp[i];
        } else {
            clean[ci++] = ' ';
        }
    }
    while (ci > 0 && (clean[ci-1] == ' ' || clean[ci-1] == '\n' || clean[ci-1] == '\r' || clean[ci-1] == '\t'))
        ci--;
    clean[ci] = '\0';

    char *clean_start = clean;
    while (*clean_start && (*clean_start == ' ' || *clean_start == '\n' || *clean_start == '\r' || *clean_start == '\t'))
        clean_start++;

    size_t max_show = 1024;
    char *san = (char*)malloc(max_show + 1);
    strncpy(san, clean_start, max_show);
    san[max_show] = '\0';

    // ----- Tentar parsear JSON primeiro -----
    cJSON *root = cJSON_Parse(clean_start);

    const char *status_str = NULL;
    const char *message_str = NULL;

    if (root) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
        cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_IsString(status) && status->valuestring) status_str = status->valuestring;
        if (cJSON_IsString(message) && message->valuestring) message_str = message->valuestring;
        cJSON_Delete(root);
    } else {
        // Fallback para formato legado "OK <mensagem>" ou "ERROR <mensagem>"
        if (strncmp(clean_start, "OK ", 3) == 0) {
            status_str = "OK";
            message_str = clean_start + 3;
        } else if (strncmp(clean_start, "ERROR ", 6) == 0) {
            status_str = "ERROR";
            message_str = clean_start + 6;
        } else if (strncmp(clean_start, "ERR ", 4) == 0) {
            status_str = "ERROR";
            message_str = clean_start + 4;
        } else if (strcmp(clean_start, "OK") == 0) {
            status_str = "OK";
            message_str = NULL;
        } else {
            // não reconhecido: mostrar raw para debug
            char shown_label[1400];
            snprintf(shown_label, sizeof(shown_label), "Recebido (raw): %s", san);
            gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), shown_label);
            fprintf(stderr, "DEBUG: resposta não JSON nem formato legado: '%s'\n", clean_start);
            free(clean);
            free(san);
            free(resp);
            return;
        }
    }

    // ----- Se status == OK: mostrar "Código enviado" (solicitado) -----
    if (status_str && strcmp(status_str, "OK") == 0) {
        // depois de confirmar status == OK no parse da resposta
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Código enviado");

        /* mostrar campos e iniciar timer de 15 minutos = 900s */
        start_recovery_timer(ctx, 15 * 60);
        /* guardar token se houver */
        if (message_str && message_str[0] != '\0') {
            /* caso o servidor retorne token em message, salve */
            if (ctx->recovery_token) g_free(ctx->recovery_token);
            ctx->recovery_token = g_strdup(message_str);
        }

        // mostrar os inputs de verificação
        gtk_widget_show(ctx->lbl_recovery_code);
        gtk_widget_show(ctx->recovery_code_entry);
        gtk_widget_show(ctx->lbl_recovery_new_pass);
        gtk_widget_show(ctx->recovery_new_pass_entry);
        gtk_widget_show(ctx->btn_recovery_verify);

        // esconder email + enviar se quiser evitar re-enviar
        gtk_widget_hide(ctx->recovery_email_entry);
        gtk_widget_hide(ctx->btn_recovery_request);

        // opcional: foco no entry do código
        gtk_widget_grab_focus(ctx->recovery_code_entry);

    } else {
        // Monta label informativa para outros casos (raw + parsed)
        char final_label[2048];
        snprintf(final_label, sizeof(final_label), "Recebido (raw): %s\nStatus: %s", san, status_str ? status_str : "(n/a)");
        if (message_str && message_str[0] != '\0') {
            strncat(final_label, "\nMessage: ", sizeof(final_label) - strlen(final_label) - 1);
            strncat(final_label, message_str, sizeof(final_label) - strlen(final_label) - 1);
        }
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), final_label);
    }

    free(clean);
    free(san);
    free(resp);
}



// Função para verificar o código e redefinir a senha
void on_recovery_verify(GtkButton *btn, LoginCtx *ctx) {
    (void)btn;
    const char *email = gtk_entry_get_text(GTK_ENTRY(ctx->recovery_email_entry));
    const char *code = gtk_entry_get_text(GTK_ENTRY(ctx->recovery_code_entry));
    const char *new_pass = gtk_entry_get_text(GTK_ENTRY(ctx->recovery_new_pass_entry));
    
    if (!email || !*email || !code || !*code || !new_pass || !*new_pass) {
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Todos os campos são obrigatórios");
        return;
    }
    
    // Construir payload para API
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"email\":\"%s\",\"code\":\"%s\"}", email, code);
    
    char cmd_utf8[700];
    snprintf(cmd_utf8, sizeof(cmd_utf8), "VERIFY_RESET_CODE %s", payload);
    fprintf(stderr, "DEBUG: sending command: %s\n", cmd_utf8);
    
    // Converter para wide char
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    WCHAR *wcmd = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, wlen);
    
    // Executar comando
    WCHAR *wresp = run_api_command(wcmd);
    free(wcmd);
    
    if (!wresp) {
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro: sem resposta do servidor");
        fprintf(stderr, "DEBUG: run_api_command returned NULL\n");
        return;
    }
    
    // Converter resposta para UTF-8
    int rlen = WideCharToMultiByte(CP_UTF8, 0, wresp, -1, NULL, 0, NULL, NULL);
    if (rlen <= 0) {
        free(wresp);
        gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro na conversão da resposta");
        fprintf(stderr, "DEBUG: WideCharToMultiByte rlen <= 0\n");
        return;
    }

    char *resp = (char*)malloc((size_t)rlen);
    WideCharToMultiByte(CP_UTF8, 0, wresp, -1, resp, rlen, NULL, NULL);
    free(wresp);

    // sanitize: remove control chars que quebram a label (ex: \x04)
    size_t resp_len = strlen(resp);
    char *clean = (char*)malloc(resp_len + 1);
    size_t ci = 0;
    for (size_t i = 0; i < resp_len; ++i) {
        unsigned char c = (unsigned char)resp[i];
        if (c >= 32 || c == '\n' || c == '\r' || c == '\t')
            clean[ci++] = resp[i];
        else
            clean[ci++] = ' ';
    }
    while (ci > 0 && (clean[ci-1] == ' ' || clean[ci-1] == '\n' || clean[ci-1] == '\r' || clean[ci-1] == '\t')) ci--;
    clean[ci] = '\0';

    // Mostra raw (sanitized) na label e no stderr pra debug
    char shown_label[1200];
    snprintf(shown_label, sizeof(shown_label), "Recebido (raw): %s", clean);
    gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), shown_label);
    fprintf(stderr, "DEBUG: verify resp raw: '%s'\n", clean);

    // Tenta parsear JSON
    cJSON *root = cJSON_Parse(clean);
    const char *reset_token_str = NULL;
    const char *status_str = NULL;
    const char *message_str = NULL;

    if (root) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
        cJSON *reset_token = cJSON_GetObjectItemCaseSensitive(root, "reset_token");
        cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_IsString(status) && status->valuestring) status_str = status->valuestring;
        if (cJSON_IsString(reset_token) && reset_token->valuestring) reset_token_str = reset_token->valuestring;
        if (cJSON_IsString(message) && message->valuestring) message_str = message->valuestring;
        cJSON_Delete(root);
    } else {
        // fallback: formato legado "OK <token>" ou "ERROR <mensagem>"
        if (strncmp(clean, "OK ", 3) == 0) {
            status_str = "OK";
            // token pode vir após "OK "
            reset_token_str = clean + 3;
        } else if (strncmp(clean, "ERROR ", 6) == 0 || strncmp(clean, "ERR ", 4) == 0) {
            status_str = "ERROR";
            message_str = (strstr(clean, "ERROR ") == clean) ? (clean + 6) : (clean + 4);
        }
    }

    // Se obtivemos token -> prosseguir com RESET_PASSWORD
    if (status_str && strcmp(status_str, "OK") == 0 && reset_token_str && reset_token_str[0] != '\0') {
        fprintf(stderr, "DEBUG: got reset_token: %s\n", reset_token_str);
        // Salva token no contexto (cópia)
        if (ctx->recovery_token) g_free(ctx->recovery_token);
        ctx->recovery_token = g_strdup(reset_token_str);

        // Agora faz RESET_PASSWORD
        char reset_payload[700];
        snprintf(reset_payload, sizeof(reset_payload), "{\"reset_token\":\"%s\",\"new_password\":\"%s\"}", reset_token_str, new_pass);
        char reset_cmd_utf8[900];
        snprintf(reset_cmd_utf8, sizeof(reset_cmd_utf8), "RESET_PASSWORD %s", reset_payload);
        fprintf(stderr, "DEBUG: sending reset command: %s\n", reset_cmd_utf8);

        // converter e enviar
        int rwlen = MultiByteToWideChar(CP_UTF8, 0, reset_cmd_utf8, -1, NULL, 0);
        WCHAR *rwcmd = (WCHAR*)malloc((size_t)rwlen * sizeof(WCHAR));
        MultiByteToWideChar(CP_UTF8, 0, reset_cmd_utf8, -1, rwcmd, rwlen);
        WCHAR *rwresp = run_api_command(rwcmd);
        free(rwcmd);

        if (!rwresp) {
            gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro: sem resposta do servidor ao reset");
            fprintf(stderr, "DEBUG: run_api_command reset returned NULL\n");
            free(clean);
            free(resp);
            return;
        }

        int rrlen = WideCharToMultiByte(CP_UTF8, 0, rwresp, -1, NULL, 0, NULL, NULL);
        char *rresp = (char*)malloc((size_t)rrlen);
        WideCharToMultiByte(CP_UTF8, 0, rwresp, -1, rresp, rrlen, NULL, NULL);
        free(rwresp);

        // sanitize reset response
        size_t rresp_len = strlen(rresp);
        char *rclean = (char*)malloc(rresp_len + 1);
        size_t rci = 0;
        for (size_t i = 0; i < rresp_len; ++i) {
            unsigned char c = (unsigned char)rresp[i];
            if (c >= 32 || c == '\n' || c == '\r' || c == '\t') rclean[rci++] = rresp[i];
            else rclean[rci++] = ' ';
        }
        while (rci > 0 && (rclean[rci-1] == ' ' || rclean[rci-1] == '\n' || rclean[rci-1] == '\r' || rclean[rci-1] == '\t')) rci--;
        rclean[rci] = '\0';
        fprintf(stderr, "DEBUG: reset response raw: '%s'\n", rclean);

        cJSON *rroot = cJSON_Parse(rclean);
        if (rroot) {
            cJSON *rstatus = cJSON_GetObjectItemCaseSensitive(rroot, "status");
            cJSON *rmessage = cJSON_GetObjectItemCaseSensitive(rroot, "message");
            const char *rstatus_str = (cJSON_IsString(rstatus) && rstatus->valuestring) ? rstatus->valuestring : NULL;
            const char *rmessage_str = (cJSON_IsString(rmessage) && rmessage->valuestring) ? rmessage->valuestring : NULL;

            if (rstatus_str && strcmp(rstatus_str, "OK") == 0) {
                gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Senha redefinida com sucesso");
                gtk_entry_set_text(GTK_ENTRY(ctx->recovery_email_entry), "");
                gtk_entry_set_text(GTK_ENTRY(ctx->recovery_code_entry), "");
                gtk_entry_set_text(GTK_ENTRY(ctx->recovery_new_pass_entry), "");
                gtk_widget_hide(ctx->recovery_code_entry);
                gtk_widget_hide(ctx->recovery_new_pass_entry);
            } else {
                gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), rmessage_str ? rmessage_str : "Erro ao redefinir senha");
            }
            cJSON_Delete(rroot);
        } else {
            // fallback: se reset retornou "OK" legada
            if (strncmp(rclean, "OK", 2) == 0) {
                gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Senha redefinida com sucesso");
                // Limpar entradas
                if (ctx->recovery_email_entry) gtk_entry_set_text(GTK_ENTRY(ctx->recovery_email_entry), "");
                if (ctx->recovery_code_entry)  gtk_entry_set_text(GTK_ENTRY(ctx->recovery_code_entry), "");
                if (ctx->recovery_new_pass_entry) gtk_entry_set_text(GTK_ENTRY(ctx->recovery_new_pass_entry), "");

                // Esconder campos individuais (labels/entries/botões)
                if (ctx->lbl_recovery_code)        gtk_widget_hide(ctx->lbl_recovery_code);
                if (ctx->recovery_code_entry)      gtk_widget_hide(ctx->recovery_code_entry);
                if (ctx->lbl_recovery_new_pass)    gtk_widget_hide(ctx->lbl_recovery_new_pass);
                if (ctx->recovery_new_pass_entry)  gtk_widget_hide(ctx->recovery_new_pass_entry);
                if (ctx->btn_recovery_verify)      gtk_widget_hide(ctx->btn_recovery_verify);
                if (ctx->btn_recovery_request)     gtk_widget_hide(ctx->btn_recovery_request);
                if (ctx->recovery_email_entry)     gtk_widget_hide(ctx->recovery_email_entry);
                if (ctx->recovery_status_label)    gtk_widget_hide(ctx->recovery_status_label);

                // Esconder toda a caixa de recuperação (se estiver anexada)
                if (ctx->recovery_box) {
                    if (gtk_widget_get_parent(ctx->recovery_box)) {
                        gtk_widget_unparent(ctx->recovery_box);
                    }
                }

                // Liberar token salvo no contexto (se existir)
                if (ctx->recovery_token) {
                    g_free(ctx->recovery_token);
                    ctx->recovery_token = NULL;
                }

                // Opcional: voltar o foco para o login
                if (ctx->email_entry) gtk_widget_grab_focus(ctx->email_entry);
                stop_recovery_timer(ctx);
                
            } else {
                // mostra resposta bruta
                char tmp[1200];
                snprintf(tmp, sizeof(tmp), "Resposta reset (raw): %s", rclean);
                gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), tmp);
            }
        }

        free(rclean);
        free(rresp);
    } else {
        // não obteve token -> mostra a mensagem de erro do verify
        if (message_str && message_str[0] != '\0') {
            gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), message_str);
        } else {
            gtk_label_set_text(GTK_LABEL(ctx->recovery_status_label), "Erro ao verificar código");
        }
    }

    free(clean);
    free(resp);
}


// Criação da janela de login
GtkWidget* create_login_window(void) {
    apply_metal_theme();
    apply_login_css();

    LoginCtx *ctx = g_new0(LoginCtx, 1);

    GtkWidget *login_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ctx->window = login_win;  // Atribuir a janela ao contexto

    gtk_window_set_title(GTK_WINDOW(login_win), "Login / Cadastro");

    // Tela cheia proporcional
    GdkScreen *screen = gdk_screen_get_default();
    gint sw = gdk_screen_get_width(screen);
    gint sh = gdk_screen_get_height(screen);
    gtk_window_set_default_size(GTK_WINDOW(login_win),
        CLAMP(sw*0.45, 420, 1200), CLAMP(sh*0.4, 320, 900));
    gtk_window_set_position(GTK_WINDOW(login_win), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(login_win), 12);
    gtk_widget_set_name(login_win, "login-window");

    // Notebook para abas
    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(login_win), notebook);

    // ================= LOGIN TAB =================
    GtkWidget *login_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(login_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(login_grid), 12);

    ctx->login_grid = login_grid;

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

    // BOTÃO "Esqueci minha senha"
    GtkWidget *forgot_btn = gtk_button_new_with_label("Esqueci minha senha");
    gtk_button_set_relief(GTK_BUTTON(forgot_btn), GTK_RELIEF_NONE);
    gtk_widget_set_name(forgot_btn, "link-like-button");
    gtk_grid_attach(GTK_GRID(login_grid), forgot_btn, 0, 5, 2, 1);
    gtk_widget_set_halign(forgot_btn, GTK_ALIGN_CENTER);

    // ----- Criar a caixa de recuperação, mas NÃO anexar ao grid ainda -----
    ctx->recovery_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(ctx->recovery_box, 10);

    // Email
    GtkWidget *lbl_recovery_email = gtk_label_new("Email:");
    ctx->recovery_email_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->recovery_email_entry), "seu@email.com");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), lbl_recovery_email, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->recovery_email_entry, FALSE, FALSE, 0);

    // Botão pedir código (guarde no ctx)
    ctx->btn_recovery_request = gtk_button_new_with_label("Enviar Código");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->btn_recovery_request, FALSE, FALSE, 0);

    // Código e nova senha (crie e guarde no ctx)
    ctx->lbl_recovery_code = gtk_label_new("Código:");
    ctx->recovery_code_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->recovery_code_entry), "Código recebido");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->lbl_recovery_code, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->recovery_code_entry, FALSE, FALSE, 0);

    ctx->lbl_recovery_new_pass = gtk_label_new("Nova senha:");
    ctx->recovery_new_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(ctx->recovery_new_pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->recovery_new_pass_entry), "••••••");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->lbl_recovery_new_pass, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->recovery_new_pass_entry, FALSE, FALSE, 0);

    ctx->btn_recovery_verify = gtk_button_new_with_label("Redefinir Senha");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->btn_recovery_verify, FALSE, FALSE, 0);

    // Status da recuperação
    ctx->recovery_status_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->recovery_status_label, FALSE, FALSE, 0);

    // Barra de progresso do token (inicialmente escondida)
    ctx->recovery_progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ctx->recovery_progress), TRUE);
    gtk_widget_set_hexpand(ctx->recovery_progress, TRUE);
    gtk_box_pack_start(GTK_BOX(ctx->recovery_box), ctx->recovery_progress, FALSE, FALSE, 0);
    gtk_widget_hide(ctx->recovery_progress);

    // Esconder os inputs de código/senha nova inicialmente
    gtk_widget_hide(ctx->lbl_recovery_code);
    gtk_widget_hide(ctx->recovery_code_entry);
    gtk_widget_hide(ctx->lbl_recovery_new_pass);
    gtk_widget_hide(ctx->recovery_new_pass_entry);
    gtk_widget_hide(ctx->btn_recovery_verify);
    

    // ================= SIGNALS =================
    g_signal_connect(forgot_btn, "clicked", G_CALLBACK(on_forgot_clicked), ctx);
    g_signal_connect(ctx->btn_recovery_request, "clicked", G_CALLBACK(on_recovery_request), ctx);
    g_signal_connect(ctx->btn_recovery_verify, "clicked", G_CALLBACK(on_recovery_verify), ctx);

    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login_button_clicked), ctx);
    g_signal_connect(ctx->pass_entry, "activate", G_CALLBACK(on_login_button_clicked), ctx);
    g_signal_connect(btn_register, "clicked", G_CALLBACK(on_register_button_clicked), ctx);
    
    // Conectar o sinal destroy para liberar o contexto
    g_signal_connect(login_win, "destroy", G_CALLBACK(on_login_window_destroy), ctx);

    gtk_widget_show(login_win);
    return login_win;
}
