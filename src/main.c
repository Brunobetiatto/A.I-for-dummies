#include <gtk/gtk.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "css/css.h"

#include "backend/backlogger.h"

#include "interface/context.h"
#include "interface/datasets.h"
#include "interface/environment.h"
#include "interface/login.h"
#include "interface/profile_tab.h"
#include "interface/profile.h"

static void open_main_after_login(GtkWidget *login_window, gpointer user_data);

static void env_free(gpointer p) {
    EnvCtx *env = (EnvCtx*)p;
    if (!env) return;
    debug_log("env_free(): freeing EnvCtx %p (user=%s)", env, env->current_user_name ? env->current_user_name : "(null)");
    if (env->current_user_name) g_free(env->current_user_name);
    if (env->current_user_email) g_free(env->current_user_email);
    if (env->token) g_free(env->token);
    g_free(env);
}

/* Handler do botão logout */
static void on_logout_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    EnvCtx *env = (EnvCtx*) user_data;
    if (!env) {
        debug_log("on_logout_clicked(): env is NULL");
        return;
    }

    debug_log("on_logout_clicked(): user_id=%d name=%s", env->current_user_id,
              env->current_user_name ? env->current_user_name : "(null)");

    /* 1) limpar token e log */
    communicator_clear_token();
    backlogger_log_line("User logged out: %s", env->current_user_name ? env->current_user_name : "(unknown)");

    /* 2) abrir a janela de login ANTES de destruir a main (assim o usuário não fica sem UI) */
    LoginHandlers h = { .on_success = open_main_after_login, .user_data = NULL };
    GtkWidget *login_win = create_login_window(&h);
    if (login_win) gtk_widget_show_all(login_win);

    /* 3) desconectar handler gtk_main_quit conectado à main window (se existir),
       para que destruir a janela principal não feche a aplicação. Isso remove a conexão
       com gtk_main_quit que foi feita em create_main_window(). */
    if (env->main_window) {
        g_signal_handlers_disconnect_by_func(G_OBJECT(env->main_window),
                                             G_CALLBACK(gtk_main_quit),
                                             NULL);
    }

    /* 4) agora é seguro destruir a janela principal — o env será liberado se você vinculou env_free via g_object_set_data_full */
    if (env->main_window) {
        gtk_widget_destroy(GTK_WIDGET(env->main_window));
    }
}


GtkWidget* create_main_window(UserSession *session) {
    EnvCtx *env = g_new0(EnvCtx, 1);

    /* guardar sessão no env para acesso por outras tabs */
    if (session) {
        env->current_user_id = session->id;
        env->current_user_name = session->nome ? g_strdup(session->nome) : NULL;
        env->current_user_email = session->email ? g_strdup(session->email) : NULL;
        env->token = session->token ? g_strdup(session->token) : NULL;
        /* a env passa a assumir a posse dos strings (g_strdup acima): liberá-los quando o env for destruído */
        /* liberar a struct session (somente a struct) — NÃO liberar as strings, pois já duplicadas */
        user_session_free(session);
    } else {
        env->current_user_id = 0;
        env->current_user_name = NULL;
        env->current_user_email = NULL;
        env->token = NULL;
    }

    GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_win), "AI For Dummies - Main");
    gtk_window_set_default_size(GTK_WINDOW(main_win), 800, 600);
    g_signal_connect(main_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    env->main_window = GTK_WINDOW(main_win);

    /* garantir que o env seja liberado quando a janela principal for destruída */
    g_object_set_data_full(G_OBJECT(main_win), "env", env, env_free);

    /* layout: vbox com header (user + logout) e notebook abaixo */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(header, 8);
    gtk_widget_set_margin_end(header, 8);
    gtk_widget_set_margin_top(header, 6);
    gtk_widget_set_margin_bottom(header, 6);

    /* label de usuário à esquerda */
    char user_label_text[256] = "";
    if (env->current_user_name && *env->current_user_name) {
        snprintf(user_label_text, sizeof(user_label_text), "Logged as: %s", env->current_user_name);
    } else if (env->current_user_email && *env->current_user_email) {
        snprintf(user_label_text, sizeof(user_label_text), "Logged as: %s", env->current_user_email);
    } else {
        snprintf(user_label_text, sizeof(user_label_text), "Logged in");
    }
    GtkWidget *lbl_user = gtk_label_new(user_label_text);
    gtk_label_set_xalign(GTK_LABEL(lbl_user), 0.0);

    /* botão de logout à direita */
    GtkWidget *btn_logout = gtk_button_new_with_label("Logout");
    g_signal_connect(btn_logout, "clicked", G_CALLBACK(on_logout_clicked), env);

    /* packing: label expande para empurrar o botão para a direita */
    gtk_box_pack_start(GTK_BOX(header), lbl_user, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header), btn_logout, FALSE, FALSE, 0);

    /* notebook com tabs */
    GtkWidget *nb = gtk_notebook_new();

    /* adicionar header e notebook ao vbox */
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);

    /* adicionar vbox na janela principal */
    gtk_container_add(GTK_CONTAINER(main_win), vbox);

    /* Criar abas */
    add_datasets_tab(GTK_NOTEBOOK(nb), env);
    add_environment_tab(GTK_NOTEBOOK(nb), env);
    add_profile_tab(GTK_NOTEBOOK(nb), env);

    gtk_widget_show_all(main_win);
    return main_win;
}

static void open_main_after_login(GtkWidget *login_window, gpointer user_data) {
    UserSession *session = (UserSession*) user_data;

    // set token in communicator (if session->token is present)
    if (session && session->token && *session->token) {
        communicator_set_token(session->token);
        debug_log("open_main_after_login: communicator_set_token called (len=%d)", (int)strlen(session->token));
    } else {
        communicator_clear_token();
        debug_log("open_main_after_login: no token in session; communicator_clear_token called");
    }

    GtkWidget *main_win = create_main_window(session);
    gtk_widget_show_all(main_win);

    if (login_window) gtk_widget_destroy(login_window);
}


int main(int argc, char **argv) {

    // Point to MSYS2’s compiled schemas (MINGW64 path shown; change if you use UCRT64)
    g_setenv("GSETTINGS_SCHEMA_DIR", "C:/msys64/mingw64/share/glib-2.0/schemas", FALSE);

    // Optional: clear any fatal-warnings if it was set system-wide
    g_unsetenv("G_DEBUG");

    gtk_init(&argc, &argv);

    backlogger_init("logs", "AI-for-dummies"); 

    backlogger_log_line("App starting with %d args", argc);

    LoginHandlers h = { .on_success = open_main_after_login, .user_data = NULL };
    GtkWidget *login = create_login_window(&h);
    gtk_widget_show_all(login);

    gtk_main();
    return 0;
}
