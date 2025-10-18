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
    }

    GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_win), "AI For Dummies - Main");
    gtk_window_set_default_size(GTK_WINDOW(main_win), 800, 600);
    g_signal_connect(main_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    env->main_window = GTK_WINDOW(main_win);

    GtkWidget *nb = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(main_win), nb);

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
