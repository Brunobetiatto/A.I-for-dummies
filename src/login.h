#ifndef LOGIN_H
#define LOGIN_H

#include <gtk/gtk.h>
#include <time.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *login_grid;         // guardar grid para anexar recovery_box dinamicamente
    GtkWidget *email_entry;
    GtkWidget *pass_entry;
    GtkWidget *status_label;

    // aba cadastro
    GtkWidget *reg_nome_entry;
    GtkWidget *reg_email_entry;
    GtkWidget *reg_pass_entry;
    GtkWidget *reg_status_label;

    // recuperação
    GtkWidget *recovery_box;           // toda a caixa (criada mas não anexada)
    GtkWidget *recovery_email_entry;
    GtkWidget *btn_recovery_request;
    GtkWidget *lbl_recovery_code;
    GtkWidget *recovery_code_entry;
    GtkWidget *lbl_recovery_new_pass;
    GtkWidget *recovery_new_pass_entry;
    GtkWidget *btn_recovery_verify;
    GtkWidget *recovery_status_label;

    // progress bar 

    GtkWidget *recovery_progress;
    guint      recovery_timer_id;
    time_t     recovery_expiry;          /* epoch seconds do fim */
    gint       recovery_total_seconds;   /* duração total (ex: 15*60) */

    char *recovery_token;
} LoginCtx;

GtkWidget* create_login_window(void);
void apply_login_css(void);
void on_login_button_clicked(GtkButton *btn, gpointer user_data);
void on_register_button_clicked(GtkButton *button, LoginCtx *ctx);
void on_forgot_clicked(GtkButton *btn, gpointer user_data);
void on_recovery_request(GtkButton *btn, LoginCtx *ctx);
void on_recovery_verify(GtkButton *btn, LoginCtx *ctx);




#endif