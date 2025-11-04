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


    GtkCheckButton  *scale_chk;
    GtkCheckButton  *impute_chk;

    GtkEntry       *x_feat;
    GtkEntry       *y_feat;

    GtkSpinButton  *epochs_spin;

    // right panel

    GtkTreeView         *ds_preview_tv;    // singleton "Preview dataset" tab
    GtkTreeView         *fit_view;         // epochs table
    GtkListStore        *fit_store;

    GtkImage            *plot_img;
    GtkLabel            *status;

    GtkButton           *btn_logout;

    // runtime      
    gchar               *pause_flag_path;
    GtkWidget           *metrics_panel; /* painel da tabela de métricas */
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

    GtkNotebook     *right_nb;
    GtkTreeView     *preview_view;
    GtkTextView     *logs_view;
    GtkListStore    *preview_store;
    GtkButton       *btn_play;

    GtkProgressBar  *progress;

    GtkNotebook *left_nb;         
    GtkWidget   *preproc_box;     // vbox da aba "Pre-processing"
    GtkWidget   *model_box;       // vbox da aba "Model"
    GtkWidget   *model_params_box;// área dinâmica p/ hyperparams por modelo
    
    GtkButton       *btn_debug;
    GtkBox         *topbar;  
    GtkBox   *session_box;       
    GtkLabel       *session_label;

    GtkLabel        *split_test_lbl;
    GtkLabel        *split_train_lbl;
    GtkEntry        *split_entry;
    GtkScale        *split_scale;      /* slider (GtkRange) */
    gboolean         split_lock;

    char *current_dataset_path;  /* caminho absoluto do último dataset carregado */

    int *session;
    int current_user_id;
    char *current_user_name;
    char *current_user_email;

    char *token;
    char *avatar_image;


} EnvCtx;

#endif