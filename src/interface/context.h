#include <gtk/gtk.h>

#ifndef CONTEXT_H
#define CONTEXT_H

typedef struct {
    GtkEntry     *entry;
    GtkTreeView  *view;
    GtkListStore *store;
} TabCtx;

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

    char *current_dataset_path;  /* caminho absoluto do último dataset carregado */
} EnvCtx;

#endif