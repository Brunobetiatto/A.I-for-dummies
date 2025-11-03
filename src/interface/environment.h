#include "../css/css.h"
#include "context.h"
#include "debug_window.h"
#include <glib/gstdio.h>
#include <sys/stat.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>   
#include <fcntl.h>  
#include <errno.h>
#include <process.h> 
#endif

#ifndef ENV_H
#define ENV_H

/* --------- Barra Win95 para a janela do Environment ---------- */

#include <gtk/gtk.h>

/* helper local: alterna maximizar/restaurar */
static void titlebar_on_max_clicked(GtkButton *btn, gpointer win_) {
    (void)btn;
    GtkWindow *win = GTK_WINDOW(win_);
    if (gtk_window_is_maximized(win)) gtk_window_unmaximize(win);
    else                               gtk_window_maximize(win);
}

/* instala a titlebar Win95 com logo + 3 botões PNG fixos */
static void install_env_w95_titlebar(GtkWindow *win, const char *title_text) {
    GtkWidget *hb = gtk_header_bar_new();
    gtk_widget_set_name(hb, "w95-titlebar");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), FALSE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), NULL);

    /* ESQUERDA: ícone + título */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GdkPixbuf *pb_logo =
        gdk_pixbuf_new_from_file_at_scale("assets/AI-for-dummies.png", 20, 20, TRUE, NULL);
    GtkWidget *logo = gtk_image_new_from_pixbuf(pb_logo);
    g_object_unref(pb_logo);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), logo, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new(title_text ? title_text : "AI for Dummies");
    gtk_widget_set_name(title, "w95-title");
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), left);
    
    GtkWidget *btn_min   = gtk_button_new();
    GtkWidget *btn_max   = gtk_button_new();
    GtkWidget *btn_close = gtk_button_new();

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_min),   "envbar-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_max),   "envbar-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "envbar-btn");


    GdkPixbuf *pb_min   = gdk_pixbuf_new_from_file_at_scale("assets/minimize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_max   = gdk_pixbuf_new_from_file_at_scale("assets/maximize.png", 12, 12, TRUE, NULL);
    GdkPixbuf *pb_close = gdk_pixbuf_new_from_file_at_scale("assets/close.png",    12, 12, TRUE, NULL);

    gtk_button_set_image(GTK_BUTTON(btn_min),   gtk_image_new_from_pixbuf(pb_min));
    gtk_button_set_image(GTK_BUTTON(btn_max),   gtk_image_new_from_pixbuf(pb_max));
    gtk_button_set_image(GTK_BUTTON(btn_close), gtk_image_new_from_pixbuf(pb_close));
    gtk_button_set_always_show_image(GTK_BUTTON(btn_min),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_max),   TRUE);
    gtk_button_set_always_show_image(GTK_BUTTON(btn_close), TRUE);

    g_object_unref(pb_min); g_object_unref(pb_max); g_object_unref(pb_close);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_close);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_max);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), btn_min);

    /* sinais (simples) */
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_close),   win);
    g_signal_connect_swapped(btn_min,   "clicked", G_CALLBACK(gtk_window_iconify), win);
    g_signal_connect        (btn_max,   "clicked", G_CALLBACK(titlebar_on_max_clicked), win);

    /* aplica como titlebar */
    gtk_window_set_titlebar(win, hb);
}

// ---- small helpers -------------------------------------------------
static void append_log(EnvCtx *ctx, const char *fmt, ...) {
    if (!ctx || !ctx->logs_view) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(ctx->logs_view);
    GtkTextIter end; gtk_text_buffer_get_end_iter(buf, &end);
    char line[1024];
    va_list ap; va_start(ap, fmt);
    g_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    gtk_text_buffer_insert(buf, &end, line, -1);
    gtk_text_buffer_insert(buf, &end, "\n", -1);
}

// --- helpers para colocar ícone no Logout já existente ---
static void set_button_icon_from_png(GtkButton *btn, const char *path, int w, int h) {
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, w, h, TRUE, NULL);
    GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb)
                        : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_MENU);
    if (pb) g_object_unref(pb);
    gtk_button_set_image(btn, img);
    gtk_button_set_always_show_image(btn, TRUE);
    gtk_button_set_image_position(btn, GTK_POS_LEFT);
}

static void try_iconize_logout_in_session(GtkBox *session_box) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(session_box));
    for (GList *l = children; l; l = l->next) {
        if (GTK_IS_BUTTON(l->data)) {
            const gchar *lbl = gtk_button_get_label(GTK_BUTTON(l->data));
            if (lbl && (g_strcmp0(lbl, "Logout") == 0 || g_strcmp0(lbl, "Sair") == 0)) {
                set_button_icon_from_png(GTK_BUTTON(l->data), "assets/logout.png", 16, 16);
                break;
            }
        }
    }
    g_list_free(children);
}

static gboolean idle_iconize_logout(gpointer user_data) {
    try_iconize_logout_in_session(GTK_BOX(user_data));
    return G_SOURCE_REMOVE;
}

static gchar *find_python(void) {
#if defined(G_OS_WIN32)
    const char *cands[] = {"python.exe", "py.exe", "python3.exe", NULL};
#else
    const char *cands[] = {"python3", "python", NULL};
#endif
    for (int i=0;cands[i];++i) {
        gchar *p = g_find_program_in_path(cands[i]);
        if (p) return p;
    }
    return NULL;
}

/* Guarda/obtém o pixbuf-fonte no próprio GtkImage (sem mexer em EnvCtx) */
static void set_plot_src(GtkImage *img, GdkPixbuf *pb) {
    if (!img) return;
    g_object_set_data_full(G_OBJECT(img), "aifd-plot-src",
                           pb ? g_object_ref(pb) : NULL,
                           (GDestroyNotify)g_object_unref);
}
static GdkPixbuf* get_plot_src(GtkImage *img) {
    return img ? (GdkPixbuf*)g_object_get_data(G_OBJECT(img), "aifd-plot-src") : NULL;
}

/* Reescala o pixbuf-fonte para caber no allocation atual do GtkImage */
static void update_plot_scaled(EnvCtx *ctx) {
    if (!ctx || !ctx->plot_img) return;
    GdkPixbuf *src = get_plot_src(ctx->plot_img);
    if (!src) return;

    GtkAllocation alloc;
    gtk_widget_get_allocation(GTK_WIDGET(ctx->plot_img), &alloc);

    /* fallback: tenta pai caso ainda não haja alocação válida */
    if (alloc.width <= 1 || alloc.height <= 1) {
        GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(ctx->plot_img));
        if (parent) gtk_widget_get_allocation(parent, &alloc);
    }

    int tw = MAX(32, alloc.width  - 6);
    int th = MAX(32, alloc.height - 6);

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(src, tw, th, GDK_INTERP_NEAREST);
    gtk_image_set_from_pixbuf(ctx->plot_img, scaled);
    g_object_unref(scaled);
}

/* Reescala instantaneamente sempre que o widget mudar de tamanho */
static void on_plot_size_allocate(GtkWidget *w, GtkAllocation *alloc, gpointer user_data) {
    (void)w; (void)alloc;
    update_plot_scaled((EnvCtx*)user_data);
}

/* Find the newest PNG whose name starts with prefix inside dir. Caller g_free()s the result. */
static gchar* find_latest_frame(const gchar *dir, const gchar *prefix) {
    if (!dir || !prefix) return NULL;
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d) return NULL;

    const gchar *name;
    time_t best_mtime = 0;
    gchar *best = NULL;

    while ((name = g_dir_read_name(d))) {
        if (!g_str_has_suffix(name, ".png")) continue;
        if (!g_str_has_prefix(name, prefix)) continue;

        gchar *full = g_build_filename(dir, name, NULL);
        GStatBuf st;
        if (g_stat(full, &st) == 0) {
            if (st.st_mtime >= best_mtime) {
                best_mtime = st.st_mtime;
                g_free(best);
                best = full;  /* keep */
            } else {
                g_free(full);
            }
        } else {
            g_free(full);
        }
    }
    g_dir_close(d);
    return best;
}

/* Periodic image refresher for Plot tab */
static gboolean tick_update_plot(gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->plot_img || !ctx->plot_dir || !ctx->plot_prefix) return TRUE;

    gchar *latest = find_latest_frame(ctx->plot_dir, ctx->plot_prefix);
    if (!latest) return TRUE;

    gboolean changed = (!ctx->plot_last || g_strcmp0(ctx->plot_last, latest) != 0);
    if (changed) {
        GError *err = NULL;
        /* Load and swap pixbuf (this does NOT keep the file open) */
        GdkPixbuf *pb = gdk_pixbuf_new_from_file(latest, &err);
        if (pb) {
            gtk_image_set_from_pixbuf(ctx->plot_img, pb);
            g_object_unref(pb);

            g_free(ctx->plot_last);
            ctx->plot_last = latest; latest = NULL;

            /* Auto-redirect to Plot page when a new frame appears */
            if (ctx->right_nb && ctx->plot_page_idx >= 0)
                gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);
        } else if (err) {
            g_error_free(err);
        }
    }
    g_free(latest);
    return TRUE; /* keep timer */
}

// ---- trainer stdout JSON lines ------------------------------------
static void trainer_read_stdout_cb(GObject *src, GAsyncResult *res, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    GError *err = NULL;
    gsize len = 0;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, &len, &err);
    if (err || !line) {
        // EOF or error: mark stopped
        ctx->trainer_running = FALSE;
        if (ctx->status) gtk_label_set_text(ctx->status, "Idle");
        return;
    }

    // parse one JSON line
    cJSON *js = cJSON_Parse(line);
    if (js) {
        cJSON *ev = cJSON_GetObjectItemCaseSensitive(js, "event");
        if (cJSON_IsString(ev)) {
            if (g_strcmp0(ev->valuestring, "begin")==0) {
                append_log(ctx, "[trainer] begin");
                if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
            } else if (g_strcmp0(ev->valuestring, "epoch")==0) {
                int   e     = cJSON_GetObjectItemCaseSensitive(js,"epoch")->valueint;
                int   epochs= cJSON_GetObjectItemCaseSensitive(js,"epochs")->valueint;
                double loss = cJSON_GetObjectItemCaseSensitive(js,"loss")->valuedouble;
                double score= cJSON_GetObjectItemCaseSensitive(js,"score")->valuedouble;
                GtkTreeIter it;
                gtk_list_store_append(ctx->fit_store, &it);
                gtk_list_store_set(ctx->fit_store, &it, 0, e, 1, loss, 2, score, -1);
                if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, (double)e/(double)epochs);
                if (ctx->status)   gtk_label_set_text(ctx->status, "Training…");
            } else if (g_strcmp0(ev->valuestring, "done")==0) {
                double sc = cJSON_GetObjectItemCaseSensitive(js,"score")->valuedouble;
                cJSON *p  = cJSON_GetObjectItemCaseSensitive(js,"path");
                append_log(ctx, "[trainer] done. score=%.4f saved=%s", sc, cJSON_IsString(p)?p->valuestring:"");
                if (ctx->status) gtk_label_set_text(ctx->status, "Done");
            }
        }
        cJSON_Delete(js);
    }
    g_free(line);

    // schedule next line
    g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(src), G_PRIORITY_DEFAULT, NULL,
                                        trainer_read_stdout_cb, ctx);
}

static void trainer_read_stderr_cb(GObject *src, GAsyncResult *res, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    GError *err = NULL;
    gsize len = 0;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, &len, &err);
    if (!line) return; // ignore EOF
    append_log(ctx, "%s", line);
    g_free(line);
    g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(src), G_PRIORITY_DEFAULT, NULL,
                                        trainer_read_stderr_cb, ctx);
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

/* Carrega o PNG do plot e escala para caber no widget ctx->plot_img,
   com look “pixelado” (NEAREST) para manter o estilo retro. */
static gboolean poll_fit_image_cb(gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->plot_img || !ctx->fit_img_path) return TRUE;

    /* tenta abrir a imagem atual */
    GError *err = NULL;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file(ctx->fit_img_path, &err);
    if (!pix) {
        if (err) g_error_free(err);
        return TRUE; /* sem imagem ainda; tenta de novo no próximo tick */
    }
    set_plot_src(ctx->plot_img, pix);
    /* tamanho disponível no widget da aba Plot */
    GtkAllocation alloc;
    gtk_widget_get_allocation(GTK_WIDGET(ctx->plot_img), &alloc);

    /* fallback para o pai caso a alocação do image ainda seja 0x0 */
    if (alloc.width <= 1 || alloc.height <= 1) {
        GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(ctx->plot_img));
        if (parent) gtk_widget_get_allocation(parent, &alloc);
    }

    /* borda mínima e evita zero */
    int tw = MAX(32, alloc.width  - 6);
    int th = MAX(32, alloc.height - 6);

    /* escala para caber no “quadrado” do Plot tab (preenche sem preservar AR) */
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, tw, th, GDK_INTERP_NEAREST);

    gtk_image_set_from_pixbuf(ctx->plot_img, scaled);

    g_object_unref(scaled);
    g_object_unref(pix);

    return TRUE; /* continua o timer */
}

static gboolean poll_metrics_cb(gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->metrics_view || !ctx->metrics_path) return G_SOURCE_CONTINUE;

    GStatBuf st;
    if (g_stat(ctx->metrics_path, &st) != 0) return G_SOURCE_CONTINUE;

    if (st.st_mtime == ctx->metrics_mtime && st.st_size == ctx->metrics_size)
        return G_SOURCE_CONTINUE;

    ctx->metrics_mtime = st.st_mtime;
    ctx->metrics_size  = st.st_size;

    gchar *text = NULL; gsize len = 0;
    if (g_file_get_contents(ctx->metrics_path, &text, &len, NULL)) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(ctx->metrics_view);
        gtk_text_buffer_set_text(buf, text, (gint)len);
        g_free(text);

        /* first time metrics show up? pop the tab to front once */
        static gboolean popped = FALSE;
        if (!popped) {
            gint idx = find_notebook_page_by_label(ctx->right_nb, "Metrics");
            if (idx >= 0) gtk_notebook_set_current_page(ctx->right_nb, idx);
            popped = TRUE;
        }
    }
    return G_SOURCE_CONTINUE;
}

static const char* algo_to_flag(GtkComboBoxText *c) {
    const gchar *t = gtk_combo_box_text_get_active_text(c);
    if (!t) return "linreg";

    /* regression (torch) */
    if (g_str_has_prefix(t, "Linear Regression"))  return "linreg";
    if (g_str_has_prefix(t, "Ridge"))              return "ridge";
    if (g_str_has_prefix(t, "Lasso"))              return "lasso";
    if (g_str_has_prefix(t, "MLP (Regression)"))   return "mlp_reg";

    /* classification (torch) */
    if (g_str_has_prefix(t, "Logistic (Classification)"))   return "logreg";
    if (g_str_has_prefix(t, "MLP (Classification)"))        return "mlp_cls";

    /* classical — classification */
    if (g_str_has_prefix(t, "Decision Tree (Classification)"))   return "dt_cls";
    if (g_str_has_prefix(t, "Random Forest (Classification)"))   return "rf_cls";
    if (g_str_has_prefix(t, "KNN (Classification)"))             return "knn_cls";
    if (g_str_has_prefix(t, "Naive Bayes (Classification)"))     return "nb_cls";
    if (g_str_has_prefix(t, "SVM (Classification)"))             return "svm_cls";
    if (g_str_has_prefix(t, "Gradient Boosting (Classification)")) return "gb_cls";

    /* classical — regression */
    if (g_str_has_prefix(t, "Decision Tree (Regression)"))   return "dt_reg";
    if (g_str_has_prefix(t, "Random Forest (Regression)"))   return "rf_reg";
    if (g_str_has_prefix(t, "KNN (Regression)"))             return "knn_reg";
    if (g_str_has_prefix(t, "Naive Bayes (Regression)"))     return "nb_reg";
    if (g_str_has_prefix(t, "SVM (Regression)"))             return "svm_reg";
    if (g_str_has_prefix(t, "Gradient Boosting (Regression)")) return "gb_reg";

    return "linreg";
}

static const char* proj_to_flag(GtkComboBoxText *c) {
    const gchar *t = gtk_combo_box_text_get_active_text(c);
    if (!t) return "pca2";
    if (g_str_has_prefix(t, "PCA")) return "pca2";
    if (g_str_has_prefix(t, "t-SNE")) return "tsne2";
    return "none";
}

static const char* color_to_flag(GtkComboBoxText *c) {
    const gchar *t = gtk_combo_box_text_get_active_text(c);
    if (!t) return "residual";
    if (g_str_has_prefix(t, "Residual")) return "residual";
    return "none";
}

static gboolean on_python_stdout(GIOChannel *ch, GIOCondition cond, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return FALSE;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {          /* closed/error */
        if (ch) g_io_channel_unref(ch);
        append_log(ctx, "[trainer] process ended");
        return FALSE;
    }

    GError *err = NULL;
    gchar *line = NULL; gsize len = 0;
    GIOStatus st = g_io_channel_read_line(ch, &line, &len, NULL, &err);

    if (st == G_IO_STATUS_NORMAL && line) {
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[len-1] = '\0';
        append_log(ctx, "%s", line);
        g_free(line);
    }
    if (err) g_error_free(err);
    return TRUE;
}


#ifdef G_OS_WIN32
static gboolean spawn_process_with_pipes_win(const gchar *exe_utf8, const gchar *cmdline_utf8,
                                             int *out_fd_ptr, int *err_fd_ptr, PROCESS_INFORMATION *pi_out,
                                             GError **gerr)
{
    gboolean res = FALSE;
    HANDLE hOutRd = NULL, hOutWr = NULL;
    HANDLE hErrRd = NULL, hErrWr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    /* criar pipes para stdout/stderr */
    if (!CreatePipe(&hOutRd, &hOutWr, &sa, 0)) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "CreatePipe stdout failed (err=%lu)", GetLastError());
        goto done;
    }
    if (!CreatePipe(&hErrRd, &hErrWr, &sa, 0)) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "CreatePipe stderr failed (err=%lu)", GetLastError());
        goto done;
    }

    /* leitura não herdável (só os handles de escrita vão ser herdados pelo filho) */
    if (!SetHandleInformation(hOutRd, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(hErrRd, HANDLE_FLAG_INHERIT, 0)) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "SetHandleInformation failed (err=%lu)", GetLastError());
        goto done;
    }

    /* converter strings UTF-8 -> UTF-16 */
    WCHAR *exe_w = g_utf8_to_utf16(exe_utf8, -1, NULL, NULL, NULL);
    WCHAR *cmd_w = g_utf8_to_utf16(cmdline_utf8, -1, NULL, NULL, NULL);
    if (!exe_w || !cmd_w) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "UTF-8 -> UTF-16 conversion failed");
        g_free(exe_w); g_free(cmd_w);
        goto done;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE); /* não redirecionamos stdin */
    si.hStdOutput = hOutWr;
    si.hStdError  = hErrWr;

    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));

    /* criar processo sem console (sem janela), herdando handles de escrita */
    BOOL created = CreateProcessW(
        exe_w,      /* lpApplicationName */
        cmd_w,      /* lpCommandLine (modificável) */
        NULL, NULL, /* security attrs */
        TRUE,       /* bInheritHandles */
        CREATE_NO_WINDOW,
        NULL, NULL, /* environment, cwd */
        &si, &pi
    );

    g_free(exe_w); g_free(cmd_w);

    if (!created) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "CreateProcessW failed (err=%lu)", GetLastError());
        goto done;
    }

    /* parent fecha os handles de escrita; o filho tem os seus próprios */
    CloseHandle(hOutWr); hOutWr = NULL;
    CloseHandle(hErrWr); hErrWr = NULL;

    /* converter handles de leitura para descritores POSIX */
    intptr_t out_fd_os = _open_osfhandle((intptr_t)hOutRd, _O_RDONLY | _O_BINARY);
    if (out_fd_os == -1) {
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "_open_osfhandle stdout failed (errno=%d)", errno);
        goto done;
    }
    intptr_t err_fd_os = _open_osfhandle((intptr_t)hErrRd, _O_RDONLY | _O_BINARY);
    if (err_fd_os == -1) {
        _close((int)out_fd_os);
        g_set_error(gerr, G_FILE_ERROR, G_FILE_ERROR_FAILED, "_open_osfhandle stderr failed (errno=%d)", errno);
        goto done;
    }

    /* devolver fds e PROCESS_INFORMATION (quem chamar decide fechar pi.hProcess/hThread) */
    *out_fd_ptr = (int)out_fd_os;
    *err_fd_ptr = (int)err_fd_os;
    if (pi_out) {
        memcpy(pi_out, &pi, sizeof(pi));
    } else {
        /* se não queremos PI, fechamos handles para evitar leak */
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    res = TRUE;
    return res;

done:
    if (hOutRd) CloseHandle(hOutRd);
    if (hOutWr) CloseHandle(hOutWr);
    if (hErrRd) CloseHandle(hErrRd);
    if (hErrWr) CloseHandle(hErrWr);
    return FALSE;
}
#endif

static char* build_hparams_json(EnvCtx *ctx) {
    if (!ctx || !ctx->model_params_box) return NULL;
    cJSON *root = cJSON_CreateObject();

    GList *stack = NULL; stack = g_list_prepend(stack, ctx->model_params_box);
    while (stack) {
        GtkWidget *w = stack->data; stack = g_list_delete_link(stack, stack);

        const char *key = g_object_get_data(G_OBJECT(w), "hp-key");
        if (key) {
            if (GTK_IS_SPIN_BUTTON(w)) {
                int v = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
                cJSON_AddNumberToObject(root, key, v);
            } else if (GTK_IS_ENTRY(w)) {
                const char *t = gtk_entry_get_text(GTK_ENTRY(w));
                char *end = NULL; double d = g_ascii_strtod(t ? t : "", &end);
                if (t && end && *end == '\0') cJSON_AddNumberToObject(root, key, d);
                else cJSON_AddStringToObject(root, key, t ? t : "");
            } else if (GTK_IS_COMBO_BOX_TEXT(w)) {
                char *t = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));
                cJSON_AddStringToObject(root, key, t ? t : "");
                g_free(t);
            }
        }

        if (GTK_IS_CONTAINER(w)) {
            GList *ch = gtk_container_get_children(GTK_CONTAINER(w));
            for (GList *l = ch; l; l = l->next) stack = g_list_prepend(stack, l->data);
            g_list_free(ch);
        }
    }

    char *out = cJSON_PrintUnformatted(root); // malloc'ed
    cJSON_Delete(root);
    return out; // free() after spawn
}

/* --- Função spawn_python_training atualizada para usar o helper no Windows --- */
static gboolean spawn_python_training(EnvCtx *ctx) {
    if (!ctx || !ctx->current_dataset_path) return FALSE;

    const gchar *xname = gtk_entry_get_text(ctx->x_feat);
    const gchar *yname = gtk_entry_get_text(ctx->y_feat);
    if (!xname || !*xname || !yname || !*yname) {
        append_log(ctx, "[error] Please set X and Y features.");
        return FALSE;
    }

#ifdef G_OS_WIN32
    gchar *python = g_find_program_in_path("python");
    if (!python) python = g_find_program_in_path("py");
    append_log(ctx, "[info] Using Python interpreter: %s", python ? python : "(not found)");
#else
    gchar *python = g_find_program_in_path("python3");
    if (!python) python = g_find_program_in_path("python");
#endif
    if (!python) {
        append_log(ctx, "[error] Python not found in PATH");
        return FALSE;
    }

    gchar *cwd = g_get_current_dir();
    gchar *script = g_build_filename(cwd, "python", "models", "models.py", NULL);
    if (!g_file_test(script, G_FILE_TEST_EXISTS)) {
        append_log(ctx, "[error] Script não encontrado: %s", script);
        g_free(cwd); g_free(script); g_free(python);
        return FALSE;
    }
    append_log(ctx, "[debug] CWD=%s", cwd);

    /* train/test split slider and epoch controls */
    gdouble train_pct = gtk_range_get_value(GTK_RANGE(ctx->split_scale)) / 100.0;
    char tb[32];
    g_ascii_formatd(tb, sizeof tb, "%.3f", (double)train_pct);
    gchar *train_s = g_strdup(tb);

    gint epochs = gtk_spin_button_get_value_as_int(ctx->epochs_spin);
    gint frame_every = MAX(1, epochs / 40);
    gchar *epochs_s = g_strdup_printf("%d", epochs);
    gchar *frame_s  = g_strdup_printf("%d", frame_every);

    const gchar *proj  = proj_to_flag(ctx->proj_combo);
    const gchar *color = color_to_flag(ctx->colorby_combo);
    const gchar *algo  = algo_to_flag(GTK_COMBO_BOX_TEXT(ctx->algo_combo));

    gchar *out_plot   = g_strdup(ctx->fit_img_path   ? ctx->fit_img_path   : "out_plot.png");
    gchar *out_metrics= g_strdup(ctx->metrics_path   ? ctx->metrics_path   : "metrics.txt");

    /* collect hyperparameters JSON from the dynamic panel */
    char *hp_json = build_hparams_json(ctx); /* may be NULL or "" */

    /* ---- Data Treatment controls (from Pre-processing tab) ---- */
    GtkComboBoxText *cmb_scale  = g_object_get_data(G_OBJECT(ctx->preproc_box), "scale_combo");
    GtkComboBoxText *cmb_impute = g_object_get_data(G_OBJECT(ctx->preproc_box), "impute_combo");
    GtkToggleButton *chk_onehot = g_object_get_data(G_OBJECT(ctx->preproc_box), "onehot_check");

    gchar *scale_flag  = g_strdup("standard");
    gchar *impute_flag = g_strdup("mean");

    if (cmb_scale) {
        gchar *t = gtk_combo_box_text_get_active_text(cmb_scale);
        if (t) {
            if      (g_str_has_prefix(t, "Standard")) scale_flag = g_strdup("standard");
            else if (g_str_has_prefix(t, "Min-Max"))  scale_flag = g_strdup("minmax");
            else                                      scale_flag = g_strdup("none");
            g_free(t);
        }
    }
    if (cmb_impute) {
        gchar *t = gtk_combo_box_text_get_active_text(cmb_impute);
        if (t) {
            if      (g_str_has_suffix(t, "median"))        impute_flag = g_strdup("median");
            else if (g_str_has_suffix(t, "most_frequent")) impute_flag = g_strdup("most_frequent");
            else if (g_str_has_suffix(t, "zero"))          impute_flag = g_strdup("zero");
            else                                           impute_flag = g_strdup("mean");
            g_free(t);
        }
    }
    gboolean onehot_on = (chk_onehot && gtk_toggle_button_get_active(chk_onehot)) ? TRUE : FALSE;

    /* ---- Build argv dynamically so optional flags are easy ---- */
    GPtrArray *vec = g_ptr_array_new();
    g_ptr_array_add(vec, python);
    g_ptr_array_add(vec, script);

    g_ptr_array_add(vec, "--csv");         g_ptr_array_add(vec, (gchar*)ctx->current_dataset_path);
    g_ptr_array_add(vec, "--x");           g_ptr_array_add(vec, (gchar*)xname);
    g_ptr_array_add(vec, "--y");           g_ptr_array_add(vec, (gchar*)yname);
    g_ptr_array_add(vec, "--x-label");     g_ptr_array_add(vec, (gchar*)xname);
    g_ptr_array_add(vec, "--y-label");     g_ptr_array_add(vec, (gchar*)yname);
    g_ptr_array_add(vec, "--model");       g_ptr_array_add(vec, (gchar*)algo);
    g_ptr_array_add(vec, "--epochs");      g_ptr_array_add(vec, epochs_s);
    g_ptr_array_add(vec, "--train-pct");   g_ptr_array_add(vec, train_s);
    g_ptr_array_add(vec, "--proj");        g_ptr_array_add(vec, (gchar*)proj);
    g_ptr_array_add(vec, "--color-by");    g_ptr_array_add(vec, (gchar*)color);
    g_ptr_array_add(vec, "--frame-every"); g_ptr_array_add(vec, frame_s);
    g_ptr_array_add(vec, "--out-plot");    g_ptr_array_add(vec, out_plot);
    g_ptr_array_add(vec, "--out-metrics"); g_ptr_array_add(vec, out_metrics);

    /* data-treatment -> CLI */
    g_ptr_array_add(vec, "--scale");       g_ptr_array_add(vec, scale_flag);
    g_ptr_array_add(vec, "--impute");      g_ptr_array_add(vec, impute_flag);
    if (onehot_on) g_ptr_array_add(vec, "--onehot");

    /* only pass --hparams if we actually have JSON */
    if (hp_json && hp_json[0]) {
        g_ptr_array_add(vec, "--hparams");
        g_ptr_array_add(vec, hp_json);
    }

    /* NULL-terminate for g_spawn */
    g_ptr_array_add(vec, NULL);

    /* logging — now shows the *real* train pct */
    append_log(ctx, "[debug] TRAIN PCT: %.3f", train_pct);
    for (guint i = 0; i + 1 < vec->len; ++i) {
        append_log(ctx, "[debug] argv[%u] = %s", i, (gchar*)g_ptr_array_index(vec, i));
    }

    /* spawn with pipes */
    gint out_fd = -1, err_fd = -1;
    GError *err = NULL; GPid pid = 0;
    GSpawnFlags flags = 0;
    if (!g_path_is_absolute(python)) flags = G_SPAWN_SEARCH_PATH;

    gchar **argv = (gchar**)vec->pdata; /* already NULL-terminated */
    gboolean ok = g_spawn_async_with_pipes(
        NULL, argv, NULL, flags,
        NULL, NULL, &pid,
        NULL, &out_fd, &err_fd, &err
    );

    if (!ok) {
        append_log(ctx, "[error] spawn failed: %s", err ? err->message : "unknown");
        if (err) g_error_free(err);

#ifdef G_OS_WIN32
        /* Fallback: CreateProcessW with pipes */
        append_log(ctx, "[warn] spawn_async_with_pipes falhou; tentando CreateProcessW + pipes...");

        /* Build a safe cmdline. We must quote JSON because it contains quotes. */
        gchar *onehot_part = onehot_on ? " --onehot" : "";

        gchar *hp_part = NULL;
        if (hp_json && hp_json[0]) {
            /* quote and escape " -> \" */
            GString *qs = g_string_new("\"");
            for (const char *p = hp_json; *p; ++p) {
                if (*p == '\"') g_string_append(qs, "\\\"");
                else            g_string_append_c(qs, *p);
            }
            g_string_append_c(qs, '\"');
            hp_part = g_strdup_printf(" --hparams %s", qs->str);
            g_string_free(qs, TRUE);
        } else {
            hp_part = g_strdup("");
        }

        gchar *cmdline = g_strdup_printf(
            "\"%s\" \"%s\""
            " --csv \"%s\" --x \"%s\" --y \"%s\""
            " --x-label \"%s\" --y-label \"%s\""
            " --model \"%s\" --epochs %s --train-pct %s"
            " --proj \"%s\" --color-by \"%s\" --frame-every %s"
            " --out-plot \"%s\" --out-metrics \"%s\""
            " --scale %s --impute %s%s%s",
            python, script,
            ctx->current_dataset_path,
            xname, yname,
            xname, yname,
            algo, epochs_s, train_s,
            proj, color, frame_s,
            out_plot, out_metrics,
            scale_flag, impute_flag, onehot_part, hp_part
        );

        PROCESS_INFORMATION pi;
        int win_out_fd = -1, win_err_fd = -1;
        GError *werr = NULL;

        if (spawn_process_with_pipes_win(python, cmdline, &win_out_fd, &win_err_fd, &pi, &werr)) {
            /* stdout/stderr channels */
            GIOChannel *ch_out = (win_out_fd >= 0) ? g_io_channel_win32_new_fd(win_out_fd) : NULL;
            GIOChannel *ch_err = (win_err_fd >= 0) ? g_io_channel_win32_new_fd(win_err_fd) : NULL;
            if (ch_out) {
                g_io_channel_set_encoding(ch_out, NULL, NULL);
                g_io_add_watch(ch_out, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx);
            }
            if (ch_err) {
                g_io_channel_set_encoding(ch_err, NULL, NULL);
                g_io_add_watch(ch_err, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx);
            }

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            ctx->trainer_running = TRUE;
            append_log(ctx, "[info] started child via CreateProcessW (with pipes).");
            if (ctx->status)   gtk_label_set_text(ctx->status, "Training…");
            if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
            if (ctx->right_nb && ctx->plot_page_idx >= 0)
                gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);

            g_free(cmdline);
            g_free(hp_part);
            /* clean up after spawn */
            if (hp_json) free(hp_json);
            g_free(scale_flag);
            g_free(impute_flag);
            g_ptr_array_free(vec, TRUE);
            g_free(train_s); g_free(epochs_s); g_free(frame_s);
            g_free(script);  g_free(python); g_free(cwd);
            g_free(out_plot); g_free(out_metrics);
            return TRUE;

        } else {
            append_log(ctx, "[error] CreateProcessW helper falhou: %s", werr ? werr->message : "(unknown)");
            if (werr) g_error_free(werr);
            g_free(cmdline);
            g_free(hp_part);

            /* last resort: _spawnv (no pipes) */
            append_log(ctx, "[warn] tentando fallback _spawnv() no Windows (sem pipes)...");
            char **spawn_argv = g_new0(char*, (int)vec->len);
            int ai = 0;
            for (guint i = 0; i + 1 < vec->len; ++i) spawn_argv[ai++] = (char*)g_ptr_array_index(vec, i);
            spawn_argv[ai] = NULL;

            intptr_t spawn_ret = _spawnv(_P_NOWAIT, (const char*)python, (const char * const*)spawn_argv);
            if (spawn_ret == -1) {
                append_log(ctx, "[error] _spawnv failed (errno=%d).", errno);
                g_free(spawn_argv);
                if (hp_json) free(hp_json);
                g_free(scale_flag);
                g_free(impute_flag);
                g_ptr_array_free(vec, TRUE);
                g_free(train_s); g_free(epochs_s); g_free(frame_s);
                g_free(script);  g_free(python); g_free(cwd);
                g_free(out_plot); g_free(out_metrics);
                return FALSE;
            } else {
                append_log(ctx, "[info] started child via _spawnv pid=%ld", (long)spawn_ret);
                ctx->trainer_running = TRUE;
                if (ctx->status)   gtk_label_set_text(ctx->status, "Training…");
                if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
                if (ctx->right_nb && ctx->plot_page_idx >= 0)
                    gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);

                g_free(spawn_argv);
                if (hp_json) free(hp_json);
                g_free(scale_flag);
                g_free(impute_flag);
                g_ptr_array_free(vec, TRUE);
                g_free(train_s); g_free(epochs_s); g_free(frame_s);
                g_free(script);  g_free(python); g_free(cwd);
                g_free(out_plot); g_free(out_metrics);
                return TRUE;
            }
        }
#else
        /* Unix: no extra fallback */
        append_log(ctx, "[error] spawn_async_with_pipes falhou e não há fallback disponível neste OS.");
        if (hp_json) free(hp_json);
        g_free(scale_flag);
        g_free(impute_flag);
        g_ptr_array_free(vec, TRUE);
        g_free(train_s); g_free(epochs_s); g_free(frame_s);
        g_free(script);  g_free(python); g_free(cwd);
        g_free(out_plot); g_free(out_metrics);
        return FALSE;
#endif
    }

    /* If spawn succeeded, hook stdout/stderr and update UI */
#ifdef G_OS_WIN32
    GIOChannel *ch_out = (out_fd >= 0) ? g_io_channel_win32_new_fd(out_fd) : NULL;
    GIOChannel *ch_err = (err_fd >= 0) ? g_io_channel_win32_new_fd(err_fd) : NULL;
#else
    GIOChannel *ch_out = (out_fd >= 0) ? g_io_channel_unix_new(out_fd) : NULL;
    GIOChannel *ch_err = (err_fd >= 0) ? g_io_channel_unix_new(err_fd) : NULL;
#endif
    if (ch_out) { g_io_channel_set_encoding(ch_out, NULL, NULL); g_io_add_watch(ch_out, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx); }
    if (ch_err) { g_io_channel_set_encoding(ch_err, NULL, NULL); g_io_add_watch(ch_err, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx); }

    if (hp_json) free(hp_json);
    g_free(scale_flag);
    g_free(impute_flag);
    g_ptr_array_free(vec, TRUE);

    g_free(train_s); g_free(epochs_s); g_free(frame_s);
    g_free(script);  g_free(python); g_free(cwd);
    g_free(out_plot); g_free(out_metrics);

    ctx->trainer_running = TRUE;
    append_log(ctx, "[start] model=%s  epochs=%d  train%%=%.1f  proj=%s  color=%s",
               algo, epochs, train_pct*100.0, proj, color);
    if (ctx->status)   gtk_label_set_text(ctx->status, "Training…");
    if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
    if (ctx->right_nb && ctx->plot_page_idx >= 0) gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);

    return TRUE;
}

static void on_start_clicked(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;

    /* Unpause (remove flag if present) */
    g_unlink(ctx->pause_flag_path);

    ctx->plot_timer_id = g_timeout_add(120, poll_fit_image_cb, ctx);

    /* Clear progress + status */
    if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
    if (ctx->status)   gtk_label_set_text(ctx->status, "Starting…");

    /* Spawn trainer (this will also jump to Plot) */
    spawn_python_training(ctx);
}

static void on_pause_clicked(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->pause_flag_path) return;
    if (!ctx->trainer_running) return;

    if (g_file_test(ctx->pause_flag_path, G_FILE_TEST_EXISTS)) {
        g_remove(ctx->pause_flag_path);
        gtk_button_set_label(btn, "Pause");
        if (ctx->status) gtk_label_set_text(ctx->status, "Training…");
        append_log(ctx, "[pause] resume");
    } else {
        FILE *f = fopen(ctx->pause_flag_path, "wb");
        if (f) fclose(f);
        gtk_button_set_label(btn, "Resume");
        if (ctx->status) gtk_label_set_text(ctx->status, "Paused");
        append_log(ctx, "[pause] paused");
    }
}


/* Slider mudou -> atualiza labels e entry */
static void on_split_changed(GtkRange *range, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (ctx->split_lock) return;
    set_split_ui(ctx, gtk_range_get_value(range));
}

static GtkWidget* group_panel(const char *title, GtkWidget *content) {
    // OUTER frame = the only visible box (e.g., "Regressor")
    GtkWidget *frame = gtk_frame_new(title);
    gtk_frame_set_label_align(GTK_FRAME(frame), 0.02, 0.5);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 6); // space between border and content

    // NO metal-panel wrapper. Add the content directly and give it breathing room.
    gtk_widget_set_margin_top(content, 4);
    gtk_widget_set_margin_bottom(content, 4);
    gtk_widget_set_margin_start(content, 6);
    gtk_widget_set_margin_end(content, 6);

    gtk_container_add(GTK_CONTAINER(frame), content);
    
    // outer spacing between sibling groups
    gtk_widget_set_margin_top(frame, 4);
    gtk_widget_set_margin_bottom(frame, 4);
    return frame;
}

/* Widget para usar como rótulo da aba: [icon png]  Texto */
static GtkWidget* make_tab_label(const char *icon_path, const char *text) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GError *err = NULL;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(icon_path, 16, 16, TRUE, &err);
    GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb)
                        : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_MENU);
    if (pb) g_object_unref(pb);
    if (err) g_error_free(err);

    GtkWidget *lab = gtk_label_new(text);
    gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lab, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lab, FALSE, FALSE, 0);

    gtk_widget_show_all(box);
    return box;
}

/* troca uma aba de rótulo textual por [icon + texto] */
static void set_icon_tab(GtkNotebook *nb, GtkWidget *page,
                         const char *icon_png, const char *text) {
    GtkWidget *tab = make_tab_label(icon_png, text);
    gtk_notebook_set_tab_label(nb, page, tab);
}

/* quando qualquer página é adicionada ao notebook da direita */
static void on_right_nb_page_added(GtkNotebook *nb, GtkWidget *child,
                                   guint page_num, gpointer user_data) {
    GtkWidget *tabw = gtk_notebook_get_tab_label(nb, child);
    if (GTK_IS_LABEL(tabw)) {
        const gchar *t = gtk_label_get_text(GTK_LABEL(tabw));
        if (g_strcmp0(t, "Preview dataset") == 0) {
            set_icon_tab(nb, child, "assets/preview.png", "Preview Dataset");
        }
    }
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

// remove todos os filhos de um container (útil pra reconstruir hyperparams)
static void destroy_all_children(GtkWidget *box) {
    GList *ch = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
}

// reconstrói a UI de hyperparâmetros conforme o modelo escolhido
static void rebuild_hparams_ui(EnvCtx *ctx) {
    if (!ctx || !ctx->model_params_box || !ctx->algo_combo) return;
    destroy_all_children(ctx->model_params_box);

    const char *flag = algo_to_flag(ctx->algo_combo);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    int r = 0;

    /* Torch MLPs */
    if (g_strcmp0(flag, "mlp_cls") == 0 || g_strcmp0(flag, "mlp_reg") == 0) {
        GtkWidget *sp_hidden = gtk_spin_button_new_with_range(1, 4096, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_hidden), 64);
        GtkWidget *sp_layers = gtk_spin_button_new_with_range(1, 8, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_layers), 2);
        GtkWidget *ent_lr = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ent_lr), "0.001");
        GtkWidget *cb_act = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_act), "relu");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_act), "tanh");
        gtk_combo_box_set_active(GTK_COMBO_BOX(cb_act), 0);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Hidden units"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_hidden,                    1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Layers"),      0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_layers,                    1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Learning rate"),0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), ent_lr,                       1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Activation"),  0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), cb_act,                       1, r++, 1, 1);

        g_object_set_data(G_OBJECT(sp_hidden), "hp-key", "hidden");
        g_object_set_data(G_OBJECT(sp_layers), "hp-key", "layers");
        g_object_set_data(G_OBJECT(ent_lr),    "hp-key", "lr");
        g_object_set_data(G_OBJECT(cb_act),    "hp-key", "activation");

    } else if (g_strcmp0(flag, "logreg") == 0) {
        GtkWidget *ent_C = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ent_C), "1.0");
        GtkWidget *cb_pen = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_pen), "L2");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_pen), "L1");
        gtk_combo_box_set_active(GTK_COMBO_BOX(cb_pen), 0);
        GtkWidget *sp_maxit = gtk_spin_button_new_with_range(10, 10000, 10);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_maxit), 200);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("C"),             0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), ent_C,                          1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Penalty"),       0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), cb_pen,                         1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Max iterations"),0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_maxit,                       1, r++, 1, 1);

        g_object_set_data(G_OBJECT(ent_C),    "hp-key", "C");
        g_object_set_data(G_OBJECT(cb_pen),   "hp-key", "penalty");
        g_object_set_data(G_OBJECT(sp_maxit), "hp-key", "max_iter");

    /* Classical models – show only the knobs `models.py` actually reads */
    } else if (g_strcmp0(flag, "rf_cls") == 0 || g_strcmp0(flag, "rf_reg") == 0) {
        GtkWidget *sp_n = gtk_spin_button_new_with_range(1, 10000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_n), 200);
        GtkWidget *sp_seed = gtk_spin_button_new_with_range(0, 999999, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_seed), 42);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("n_estimators"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_n,                          1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("seed"),         0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_seed,                       1, r++, 1, 1);

        g_object_set_data(G_OBJECT(sp_n),    "hp-key", "n_estimators");
        g_object_set_data(G_OBJECT(sp_seed), "hp-key", "seed");

    } else if (g_strcmp0(flag, "dt_cls") == 0 || g_strcmp0(flag, "dt_reg") == 0
            || g_strcmp0(flag, "gb_cls") == 0 || g_strcmp0(flag, "gb_reg") == 0) {
        GtkWidget *sp_seed = gtk_spin_button_new_with_range(0, 999999, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_seed), 42);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("seed"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_seed,               1, r++, 1, 1);

        g_object_set_data(G_OBJECT(sp_seed), "hp-key", "seed");

    } else if (g_strcmp0(flag, "knn_cls") == 0 || g_strcmp0(flag, "knn_reg") == 0) {
        GtkWidget *sp_k = gtk_spin_button_new_with_range(1, 2048, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_k), 7);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("n_neighbors"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_k,                         1, r++, 1, 1);

        g_object_set_data(G_OBJECT(sp_k), "hp-key", "n_neighbors");

    } else if (g_strcmp0(flag, "nb_cls") == 0) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("No specific hyperparameters."), 0, r++, 2, 1);

    } else if (g_strcmp0(flag, "nb_reg") == 0) {
        GtkWidget *sp_bins = gtk_spin_button_new_with_range(2, 1000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_bins), 10);

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("nb_reg_bins"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), sp_bins,                      1, r++, 1, 1);

        g_object_set_data(G_OBJECT(sp_bins), "hp-key", "nb_reg_bins");

    } else if (g_strcmp0(flag, "svm_cls") == 0 || g_strcmp0(flag, "svm_reg") == 0) {
        GtkWidget *cb_kernel = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_kernel), "rbf");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_kernel), "linear");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_kernel), "poly");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_kernel), "sigmoid");
        gtk_combo_box_set_active(GTK_COMBO_BOX(cb_kernel), 0);

        GtkWidget *ent_C = gtk_entry_new();     gtk_entry_set_text(GTK_ENTRY(ent_C), "1.0");
        GtkWidget *ent_g = gtk_entry_new();     gtk_entry_set_text(GTK_ENTRY(ent_g), "scale");

        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("kernel"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), cb_kernel,               1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("C"),      0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), ent_C,                   1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("gamma"),  0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), ent_g,                   1, r++, 1, 1);

        g_object_set_data(G_OBJECT(cb_kernel), "hp-key", "kernel");
        g_object_set_data(G_OBJECT(ent_C),     "hp-key", "C");
        g_object_set_data(G_OBJECT(ent_g),     "hp-key", "gamma");

        if (g_strcmp0(flag, "svm_reg") == 0) {
            GtkWidget *ent_eps = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(ent_eps), "0.1");
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new("epsilon"), 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), ent_eps,                  1, r++, 1, 1);
            g_object_set_data(G_OBJECT(ent_eps), "hp-key", "epsilon");
        }

    } else if (g_strcmp0(flag, "ridge") == 0 || g_strcmp0(flag, "lasso") == 0) {
        GtkWidget *ent_alpha = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ent_alpha), "1.0");
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("alpha"), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), ent_alpha,              1, r++, 1, 1);
        g_object_set_data(G_OBJECT(ent_alpha), "hp-key", "alpha");

    } else {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("No specific hyperparameters."), 0, r++, 2, 1);
    }

    gtk_box_pack_start(GTK_BOX(ctx->model_params_box), grid, FALSE, FALSE, 0);
    gtk_widget_show_all(ctx->model_params_box);
}

static void on_algo_changed(GtkComboBox *box, gpointer user_data) {
    rebuild_hparams_ui((EnvCtx*)user_data);
}

/* Build the Environment tab (LEFT controls | RIGHT notebook) */
/* Build the Environment tab (LEFT controls | RIGHT notebook) */
void add_environment_tab(GtkNotebook *nb, EnvCtx *ctx) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(outer, "env-window");

    /* === Carrega o CSS cedo para poder embrulhar a barra === */
    char *ENVIRONMENT_CSS = parse_CSS_file("environment.css");

    /* Instala a titlebar Win95 na janela toplevel (como antes) */
    GtkWidget *tl = gtk_widget_get_toplevel(GTK_WIDGET(nb));
    if (GTK_IS_WINDOW(tl)) {
        GtkWindow *w = GTK_WINDOW(tl);
        install_env_w95_titlebar(w, "AI for Dummies");
        gtk_window_set_resizable(w, TRUE);

        GdkScreen *screen = gdk_screen_get_default();
        gint sw = gdk_screen_get_width(screen);
        gint sh = gdk_screen_get_height(screen);
        gtk_window_set_default_size(
            w,
            CLAMP(sw * 0.45, 420, 1200),
            CLAMP(sh * 0.4,  320, 900)
        );
        gtk_window_set_position(w, GTK_WIN_POS_CENTER);
        gtk_container_set_border_width(GTK_CONTAINER(w), 12);
    }

    /* === TOP ROW: StackSwitcher (esq) + filler + sessão + Debug (dir) === */
    GtkWidget *toprow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(toprow, "env-toolbar-row");        /* nome interno da linha */
    ctx->topbar = GTK_BOX(toprow);

    /* Switcher à esquerda */
    ctx->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(ctx->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), ctx->stack);
    gtk_box_pack_start(GTK_BOX(toprow), switcher, FALSE, FALSE, 0);

    /* Filler expansível */
    GtkWidget *filler = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(filler, TRUE);
    gtk_box_pack_start(GTK_BOX(toprow), filler, TRUE, TRUE, 0);

    /* Slot de sessão: aqui o main.c injeta "Logged as …" e "Logout" */
    GtkWidget *session_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(session_box, "env-session");
    ctx->session_box = GTK_BOX(session_box);
    g_idle_add(idle_iconize_logout, ctx->session_box);

    /* Botão Debug (sempre o último à direita) */
    ctx->btn_debug = GTK_BUTTON(gtk_button_new_with_label("Debug"));
    {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale("assets/debug.png", 16, 16, TRUE, NULL);
        GtkWidget *img = gtk_image_new_from_pixbuf(pb);
        if (pb) g_object_unref(pb);
        gtk_button_set_image(ctx->btn_debug, img);
        gtk_button_set_always_show_image(ctx->btn_debug, TRUE);
        gtk_button_set_image_position(ctx->btn_debug, GTK_POS_LEFT);
    }
    gtk_widget_set_tooltip_text(GTK_WIDGET(ctx->btn_debug), "Abrir janela de debug/backlog");
    g_signal_connect(ctx->btn_debug, "clicked", G_CALLBACK(on_debug_button_clicked), ctx);

    /* Ordem entre os pack_end: Debug POR ÚLTIMO, sessão imediatamente à esquerda */
    gtk_box_pack_end(GTK_BOX(toprow), GTK_WIDGET(ctx->btn_debug), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toprow), session_box,               FALSE, FALSE, 0);

    /* === EMBRULHA a linha no painel cinza Win95 ===
       Dê o ID "env-toolbar" ao wrapper para casar com o seu CSS (#env-toolbar) */
    GtkWidget *topbar_panel = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", toprow, "env-toolbar");
    gtk_box_pack_start(GTK_BOX(outer), topbar_panel, FALSE, FALSE, 0);

    /* === Miolo: paned esquerda|direita === */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), paned, TRUE, TRUE, 0);

    /* ======== LEFT controls ======== */
    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    /* Notebook à esquerda (Pre-processing / Model) */
    GtkWidget *pre_box  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *model_box= gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *left_nb  = gtk_notebook_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(left_nb), "w95-notebook");
    gtk_notebook_append_page(GTK_NOTEBOOK(left_nb), pre_box,  gtk_label_new("Pre-processing"));
    gtk_notebook_append_page(GTK_NOTEBOOK(left_nb), model_box, gtk_label_new("Model"));
    ctx->left_nb     = GTK_NOTEBOOK(left_nb);
    ctx->preproc_box = pre_box;
    ctx->model_box   = model_box;

    /* Dataset row */
    {
        GtkWidget *ds_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->ds_combo       = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        ctx->btn_refresh_ds = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
        GtkWidget *btn_open = gtk_button_new_with_label("Open");
        GtkWidget *btn_load = gtk_button_new_with_label("Load");

        gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->ds_combo),       TRUE,  TRUE,  0);
        gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->btn_refresh_ds), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ds_row), btn_open,                        FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ds_row), btn_load,                        FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Dataset", ds_row), FALSE, FALSE, 0);

        g_signal_connect(ctx->btn_refresh_ds, "clicked", G_CALLBACK(on_refresh_local_datasets), ctx);
        g_signal_connect(btn_open,            "clicked", G_CALLBACK(on_load_local_dataset),     ctx);
        g_signal_connect(btn_load,            "clicked", G_CALLBACK(on_load_selected_dataset),  ctx);
    }

    /* Coloca o notebook no topo da coluna esquerda */
    gtk_box_pack_start(GTK_BOX(left_col), left_nb, TRUE, TRUE, 0);

    /* Trainee */
    {
        GtkWidget *tr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->model_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(ctx->model_combo, "(new)");
        gtk_box_pack_start(GTK_BOX(tr_row), gtk_label_new(""), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tr_row), GTK_WIDGET(ctx->model_combo), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(model_box), group_panel("Trainee", tr_row), FALSE, FALSE, 0);
    }

    /* Regressor + epochs */
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->algo_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(ctx->algo_combo, "Linear Regression");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Ridge (L2)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Lasso (L1)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "MLP (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Logistic (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "MLP (Classification)");

        /* classical — classification */
        gtk_combo_box_text_append_text(ctx->algo_combo, "Decision Tree (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Random Forest (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "KNN (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Naive Bayes (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "SVM (Classification)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Gradient Boosting (Classification)");

        /* classical — regression */
        gtk_combo_box_text_append_text(ctx->algo_combo, "Decision Tree (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Random Forest (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "KNN (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Naive Bayes (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "SVM (Regression)");
        gtk_combo_box_text_append_text(ctx->algo_combo, "Gradient Boosting (Regression)");

        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->algo_combo), 0);

        GtkWidget *lab_ep = gtk_label_new("Epochs:");
        gtk_widget_set_name(lab_ep, "epochs-label");
        GtkAdjustment *ep_adj = gtk_adjustment_new(100, 1, 100000, 1, 10, 0);
        ctx->epochs_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(ep_adj, 1, 0));
        gtk_spin_button_set_numeric(ctx->epochs_spin, TRUE);

        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->algo_combo), TRUE, TRUE, 0);
        gtk_box_pack_end  (GTK_BOX(row), GTK_WIDGET(ctx->epochs_spin), FALSE, FALSE, 0);
        gtk_box_pack_end  (GTK_BOX(row), lab_ep, FALSE, FALSE, 6);

        gtk_box_pack_start(GTK_BOX(model_box), group_panel("Model", row), FALSE, FALSE, 0);

        /* Hyperparameters dinâmicos */
        ctx->model_params_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(model_box), group_panel("Hyperparameters", ctx->model_params_box), FALSE, FALSE, 0);
        g_signal_connect(ctx->algo_combo, "changed", G_CALLBACK(on_algo_changed), ctx);
        rebuild_hparams_ui(ctx);
    }

    /* Projection/Color */
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->proj_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(ctx->proj_combo, "PCA (2D)");
        gtk_combo_box_text_append_text(ctx->proj_combo, "t-SNE (2D)");
        gtk_combo_box_text_append_text(ctx->proj_combo, "Off");
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->proj_combo), 0);

        ctx->colorby_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(ctx->colorby_combo, "Residual");
        gtk_combo_box_text_append_text(ctx->colorby_combo, "Off");
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->colorby_combo), 0);

        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->proj_combo), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->colorby_combo), TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(pre_box), group_panel("Projection & Color", row), FALSE, FALSE, 0);
    }

    /* Data Treatment */
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkComboBoxText *cmb_scale  = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(cmb_scale, "Standard Scale");
        gtk_combo_box_text_append_text(cmb_scale, "Min-Max Scale");
        gtk_combo_box_text_append_text(cmb_scale, "No Scaling");
        gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_scale), 0);

        GtkComboBoxText *cmb_impute = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(cmb_impute, "Impute: mean");
        gtk_combo_box_text_append_text(cmb_impute, "Impute: median");
        gtk_combo_box_text_append_text(cmb_impute, "Impute: most_frequent");
        gtk_combo_box_text_append_text(cmb_impute, "Impute: zero");
        gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_impute), 0);

        GtkWidget *chk_onehot = gtk_check_button_new_with_label("One-hot encode categoricals");

        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(cmb_scale),  TRUE,  TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(cmb_impute), TRUE,  TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), chk_onehot,             FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(pre_box), group_panel("Data Treatment", row), FALSE, FALSE, 0);

        g_object_set_data(G_OBJECT(pre_box), "scale_combo",  cmb_scale);
        g_object_set_data(G_OBJECT(pre_box), "impute_combo", cmb_impute);
        g_object_set_data(G_OBJECT(pre_box), "onehot_check", chk_onehot);
    }

    /* Split controls */
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *lab_tr = gtk_label_new("Train 70,0%");
        GtkWidget *lab_te = gtk_label_new("Test 30,0%");
        GtkWidget *entry  = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
        gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "70,0");

        ctx->split_train_lbl = GTK_LABEL(lab_tr);
        ctx->split_test_lbl  = GTK_LABEL(lab_te);
        ctx->split_entry     = GTK_ENTRY(entry);

        gtk_box_pack_start(GTK_BOX(labels), lab_tr, FALSE, FALSE, 0);
        gtk_box_pack_end  (GTK_BOX(labels), lab_te, FALSE, FALSE, 0);
        gtk_box_set_center_widget(GTK_BOX(labels), entry);

        GtkAdjustment *adj = gtk_adjustment_new(70.0, 0.0, 100.0, 0.1, 1.0, 0.0);
        GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
        ctx->split_scale = GTK_SCALE(scale);
        gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
        gtk_scale_add_mark(GTK_SCALE(scale), 50.0, GTK_POS_BOTTOM, NULL);
        gtk_widget_set_name(scale,  "split-scale");

        g_signal_connect(scale, "value-changed", G_CALLBACK(on_split_changed),       ctx);
        g_signal_connect(entry, "changed",       G_CALLBACK(on_split_entry_changed), ctx);

        gtk_box_pack_start(GTK_BOX(box), labels, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), scale,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(pre_box), group_panel("Split (Train/Test)", box), FALSE, FALSE, 0);
        gtk_widget_set_name(lab_tr, "split-train-label");
        gtk_widget_set_name(lab_te, "split-test-label");
        set_split_ui(ctx, 70.0);
    }

    /* Features */
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->x_feat = GTK_ENTRY(gtk_entry_new());
        ctx->y_feat = GTK_ENTRY(gtk_entry_new());
        gtk_entry_set_placeholder_text(ctx->x_feat, "X feature (e.g., radius_mean)");
        gtk_entry_set_placeholder_text(ctx->y_feat, "Y target (e.g., area_mean)");
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->x_feat), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->y_feat), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Features", row), FALSE, FALSE, 0);
    }

    /* Actions */
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->btn_start = GTK_BUTTON(gtk_button_new_with_label("Start ▶"));
        ctx->btn_pause = GTK_BUTTON(gtk_button_new_with_label("Pause ⏸"));
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->btn_start), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->btn_pause), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Actions", row), FALSE, FALSE, 0);
        g_signal_connect(ctx->btn_start, "clicked", G_CALLBACK(on_start_clicked), ctx);
        g_signal_connect(ctx->btn_pause, "clicked", G_CALLBACK(on_pause_clicked), ctx);
    }

    /* Wrap left/right com o mesmo look */
    GtkWidget *left_panel  = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", left_col,  "env-left-panel");
    gtk_paned_pack1(GTK_PANED(paned), left_panel, FALSE, TRUE);

    /* ======== RIGHT notebook ======== */
    GtkWidget *right_nb = gtk_notebook_new();
    ctx->right_nb = GTK_NOTEBOOK(right_nb);
    g_signal_connect(ctx->right_nb, "page-added", G_CALLBACK(on_right_nb_page_added), ctx);

    /* Logs */
    ctx->logs_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(ctx->logs_view, FALSE);
    GtkWidget *sc_log = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sc_log), GTK_WIDGET(ctx->logs_view));
    GtkWidget *logs_tab = make_tab_label("assets/logs.png", "Logs");
    gtk_notebook_append_page(ctx->right_nb, sc_log, logs_tab);

    /* Plot */
    ctx->plot_img = GTK_IMAGE(gtk_image_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->plot_img), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(ctx->plot_img), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(ctx->plot_img), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(ctx->plot_img), GTK_ALIGN_FILL);
    gtk_widget_set_size_request(GTK_WIDGET(ctx->plot_img), 1, 1);
    g_signal_connect(ctx->plot_img, "size-allocate", G_CALLBACK(on_plot_size_allocate), ctx);
    GtkWidget *plot_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(plot_inner), GTK_WIDGET(ctx->plot_img), TRUE, TRUE, 0);
    GtkWidget *plot_box = wrap_CSS(ENVIRONMENT_CSS, "w95-plot", plot_inner, "env-plot");
    GtkWidget *plot_tab = make_tab_label("assets/plot.png", "Plot");
    ctx->plot_page_idx = gtk_notebook_append_page(ctx->right_nb, plot_box, plot_tab);

    /* Metrics */
    GtkWidget *metrics_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(metrics_view), FALSE);
    GtkWidget *metrics_tab = make_tab_label("assets/metrics.png", "Metrics");
    gtk_notebook_append_page(ctx->right_nb, metrics_view, metrics_tab);
    ctx->metrics_view = GTK_TEXT_VIEW(metrics_view);

    GtkWidget *right_panel = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", right_nb, "env-right-panel");
    gtk_paned_pack2(GTK_PANED(paned), right_panel, TRUE, TRUE);

    /* Footer */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    ctx->status   = GTK_LABEL(gtk_label_new("Idle"));
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->progress), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->status),   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), footer, FALSE, FALSE, 0);

    /* Finalize */
    GtkWidget *paned_as_stack_page = paned; /* o paned é a página "Environment" da stack */
    gtk_stack_add_titled(ctx->stack, paned_as_stack_page, "environment", "Environment");

    GtkWidget *env_tab = make_tab_label("assets/environment.png", "Environment");
    gtk_notebook_append_page(nb, outer, env_tab);

    /* Substitui label “Datasets” da outra aba por [ícone + texto] se necessário */
    for (gint i = 0; i < gtk_notebook_get_n_pages(nb); ++i) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        GtkWidget *tabw = gtk_notebook_get_tab_label(nb, page);
        if (GTK_IS_LABEL(tabw)) {
            const gchar *t = gtk_label_get_text(GTK_LABEL(tabw));
            if (g_strcmp0(t, "Datasets") == 0 || g_strcmp0(t, "Dataset") == 0) {
                GtkWidget *ds_tab = make_tab_label("assets/datasets.png", "Datasets");
                gtk_notebook_set_tab_label(nb, page, ds_tab);
                break;
            }
        }
    }

    gtk_widget_show_all(outer);

    /* Caminhos e timers */
    if (!ctx->fit_img_path) { gchar *tmp = g_get_tmp_dir(); ctx->fit_img_path = g_build_filename(tmp, "aifd_fit.png",     NULL); }
    if (!ctx->metrics_path) { gchar *tmp = g_get_tmp_dir(); ctx->metrics_path = g_build_filename(tmp, "aifd_metrics.txt", NULL); }

    /* Frame Win95 inicial do Plot */
    {
        gchar *cmd = g_strdup_printf("python python/models.py --win95-mode area --win95-out \"%s\"", ctx->fit_img_path);
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);
    }

    g_setenv("AIFD_PLOT_STYLE", "retro95", TRUE);

    ctx->plot_timer_id    = g_timeout_add(120, poll_fit_image_cb, ctx);
    ctx->metrics_timer_id = g_timeout_add(500,  poll_metrics_cb,  ctx);

    /* Popular datasets */
    on_refresh_local_datasets(GTK_BUTTON(ctx->btn_refresh_ds), ctx);

    /* Libera o buffer CSS (já aplicado) */
    if (ENVIRONMENT_CSS) { free(ENVIRONMENT_CSS); ENVIRONMENT_CSS = NULL; }
}

#endif