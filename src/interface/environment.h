#include "../css/css.h"
#include "context.h"
#include "debug_window.h"

#ifndef ENV_H
#define ENV_H


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

/* Slider mudou -> atualiza labels e entry */
static void on_split_changed(GtkRange *range, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (ctx->split_lock) return;
    set_split_ui(ctx, gtk_range_get_value(range));
}

static GtkWidget* group_panel(const char *title, GtkWidget *content) {
    const char *ENV_CSS = parse_CSS_file("environment.css");


    GtkWidget *frame = gtk_frame_new(title);               // título do “box”
    gtk_frame_set_label_align(GTK_FRAME(frame), 0.02, 0.5); // título à esquerda
    gtk_container_set_border_width(GTK_CONTAINER(frame), 6);

    // põe sua moldura “metal” por dentro, pra dar padding e relevo
    GtkWidget *inner = wrap_CSS(ENV_CSS, "metal-panel", content, NULL);
    gtk_container_add(GTK_CONTAINER(frame), inner);

    // um respiro externo
    gtk_widget_set_margin_top   (frame, 4);
    gtk_widget_set_margin_bottom(frame, 4);
    return frame;
}

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
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Em desenvolvimento — recurso ainda não implementado.");
    gtk_window_set_title(GTK_WINDOW(dlg), "Aviso");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
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

    /* botão debug - no canto esquerdo/ direito dependendo de pack_end/pack_start */

    
    ctx->btn_debug = GTK_BUTTON(gtk_button_new_with_label("Debug"));
    gtk_box_pack_end(GTK_BOX(toolbar), GTK_WIDGET(ctx->btn_debug), FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(GTK_WIDGET(ctx->btn_debug), "Abrir janela de debug/backlog");

    /* Conecta o clique para abrir a janela de debug */

    g_signal_connect(ctx->btn_debug, "clicked", G_CALLBACK(on_debug_button_clicked), ctx);
    
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
    const char *ENVIRONMENT_CSS = parse_CSS_file("environment.css");
    GtkWidget *left_panel = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", left_content, "env-left-panel");
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
    GtkWidget *right_panel = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", right_nb, "env-right-panel");
    gtk_paned_pack2(GTK_PANED(paned), right_panel, TRUE, FALSE);

    /* footer */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    ctx->status   = GTK_LABEL(gtk_label_new("Idle"));
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->progress), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->status), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), wrap_CSS(ENVIRONMENT_CSS, "metal-panel", footer, "env-footer"), FALSE, FALSE, 0);

    /* stack pages */
    gtk_stack_add_titled(ctx->stack, paned, "preproc",   "Pre-processing");
    gtk_stack_add_titled(ctx->stack, paned, "regressor", "Regression");
    gtk_stack_add_titled(ctx->stack, paned, "view",      "View");

    /* signals */
    g_signal_connect(ctx->btn_refresh_ds, "clicked", G_CALLBACK(on_refresh_local_datasets), ctx);
    g_signal_connect(btn_load,       "clicked", G_CALLBACK(on_load_local_dataset),     ctx);
    g_signal_connect(btn_play,       "clicked", G_CALLBACK(on_train_clicked),    ctx);

    /* mount into notebook */
    GtkWidget *tab_lbl = gtk_label_new("Environment");
    gtk_notebook_append_page(nb, outer, tab_lbl);
    gtk_widget_show_all(outer);

    /* initial population */
    on_refresh_local_datasets(GTK_BUTTON(ctx->btn_refresh_ds), ctx);
}

#endif