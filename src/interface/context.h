#include <gtk/gtk.h>

#ifndef CONTEXT_H
#define CONTEXT_H

typedef struct {
    GtkEntry     *entry;
    GtkTreeView  *view;
    GtkListStore *store;
} TabCtx;

typedef struct {
    GtkWindow      *main_window;
    GtkStack       *stack;

    // left panel
    GtkComboBoxText *ds_combo;
    GtkButton      *btn_refresh_ds;
    GtkButton      *btn_start;
    GtkButton      *btn_pause;

    GtkComboBoxText *model_combo; // "Model"
    GtkComboBoxText *algo_combo;  // kept for later

    GtkScale       *split_scale;
    GtkLabel       *split_train_lbl;
    GtkLabel       *split_test_lbl;
    GtkEntry       *split_entry;
    gboolean        split_lock;

    GtkCheckButton  *scale_chk;
    GtkCheckButton  *impute_chk;

    GtkEntry       *x_feat;
    GtkEntry       *y_feat;

    GtkSpinButton  *epochs_spin;

    // right panel
    GtkNotebook         *right_nb;
    GtkTreeView         *preview_view;     // initial "Fitting" view was here; keep if you still use
    GtkTreeView         *ds_preview_tv;    // singleton "Preview dataset" tab
    GtkTreeView         *fit_view;         // epochs table
    GtkListStore        *fit_store;

    GtkTextView         *logs_view;
    GtkImage            *plot_img;
    GtkProgressBar      *progress;
    GtkLabel            *status;

    GtkButton           *btn_logout;

    // runtime      
    gchar               *current_dataset_path;
    gchar               *pause_flag_path;

    gchar               *fit_img_path; 
    guint               plot_timer_id; 
    time_t              fit_img_mtime; 
    goffset             fit_img_size;

    GtkTextView         *metrics_view;
    gchar               *metrics_path;       
    guint               metrics_timer_id;    
    time_t              metrics_mtime;       
    goffset             metrics_size;       

    /* plotting */  
    gchar               *plot_path;      /* original out_plot provided to Python */
    gchar               *plot_dir;       /* directory of plots */
    gchar               *plot_prefix;    /* basename without extension plus "_epoch" */
    gchar               *plot_last;      /* last frame path we showed */
    gint                plot_page_idx;   /* index of "Plot" page */

    GtkComboBoxText     *proj_combo;
    GtkComboBoxText     *colorby_combo;

    GSubprocess         *trainer;
    GDataInputStream    *trainer_out;
    GDataInputStream    *trainer_err;
    gboolean            trainer_running;
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
    
    GtkButton       *btn_debug;

    GtkScale        *split_scale;      /* slider (GtkRange) */
    GtkEntry        *split_entry;      /* text entry (accepts comma) */
    GtkLabel        *split_train_lbl;
    GtkLabel        *split_test_lbl;
    gboolean         split_lock;

    char *current_dataset_path;  /* caminho absoluto do último dataset carregado */
} EnvCtx;

#endif