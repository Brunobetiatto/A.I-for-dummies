#include "../css/css.h"
#include "context.h"
#include "debug_window.h"
#include <glib/gstdio.h>
#include <sys/stat.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>      /* _open_osfhandle */
#include <fcntl.h>   /* _O_RDONLY, _O_BINARY */
#include <errno.h>
#include <process.h> /* _spawnv, _P_NOWAIT */
#endif

#ifndef ENV_H
#define ENV_H


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

static gboolean poll_fit_image_cb(gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->fit_img_path || !ctx->plot_img) return G_SOURCE_CONTINUE;

    GStatBuf st;
    if (g_stat(ctx->fit_img_path, &st) != 0) return G_SOURCE_CONTINUE;

    /* Only refresh when size/mtime change */
    if (st.st_mtime != ctx->fit_img_mtime || st.st_size != ctx->fit_img_size) {
        ctx->fit_img_mtime = st.st_mtime;
        ctx->fit_img_size  = st.st_size;

        GError *err = NULL;
        GdkPixbuf *pb = gdk_pixbuf_new_from_file(ctx->fit_img_path, &err);
        if (pb) {
            gtk_image_set_from_pixbuf(ctx->plot_img, pb);
            g_object_unref(pb);

            /* Auto-switch to Plot whenever a fresh frame appears */
            if (ctx->right_nb && ctx->plot_page_idx >= 0)
                gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);
        }
        if (err) g_error_free(err);
    }
    return G_SOURCE_CONTINUE;
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
    if (g_str_has_prefix(t, "Linear")) return "linreg";
    if (g_str_has_prefix(t, "Ridge"))  return "ridge";
    if (g_str_has_prefix(t, "Lasso"))  return "lasso";
    if (g_str_has_prefix(t, "MLP (Reg")) return "mlp_reg";
    if (g_str_has_prefix(t, "Logistic")) return "logreg";
    if (g_str_has_prefix(t, "MLP (Cl"))  return "mlp_cls";
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

    gdouble train_pct = gtk_range_get_value(GTK_RANGE(ctx->split_scale)) / 100.0;
    gchar *train_s  = g_strdup_printf("%.3f", train_pct);

    gint epochs = gtk_spin_button_get_value_as_int(ctx->epochs_spin);
    gint frame_every = MAX(1, epochs / 40);
    gchar *epochs_s = g_strdup_printf("%d", epochs);
    gchar *frame_s  = g_strdup_printf("%d", frame_every);

    const gchar *proj  = proj_to_flag(ctx->proj_combo);
    const gchar *color = color_to_flag(ctx->colorby_combo);
    const gchar *algo  = algo_to_flag(ctx->algo_combo);

    gchar *out_plot = g_strdup(ctx->fit_img_path ? ctx->fit_img_path : "out_plot.png");
    gchar *out_metrics = g_strdup(ctx->metrics_path ? ctx->metrics_path : "metrics.json");

    gchar *argv[] = {
        python, script,
        "--csv",  (gchar*)ctx->current_dataset_path,
        "--x",    (gchar*)xname,
        "--y",    (gchar*)yname,
        "--x-label", (gchar*)xname,
        "--y-label", (gchar*)yname,
        "--model",    (gchar*)algo,
        "--epochs",   epochs_s,
        "--train-pct",train_s,
        "--proj",     (gchar*)proj,
        "--color-by", (gchar*)color,
        "--frame-every", frame_s,
        "--out-plot",    out_plot,
        "--out-metrics", out_metrics,
        NULL
    };
    append_log(ctx, "[debug] Spawning: %s %s", python, script);
    for (int i = 0; argv[i] != NULL; ++i) append_log(ctx, "[debug] argv[%d] = %s", i, argv[i]);

    gint out_fd = -1, err_fd = -1;
    GError *err = NULL; GPid pid = 0;
    GSpawnFlags flags = 0;
    if (!g_path_is_absolute(python)) flags = G_SPAWN_SEARCH_PATH;

    gboolean ok = g_spawn_async_with_pipes(
        NULL, argv, NULL,
        flags,
        NULL, NULL, &pid,
        NULL, &out_fd, &err_fd, &err
    );

    if (!ok) {
        append_log(ctx, "[error] spawn failed: %s", err ? err->message : "unknown");
        if (err) g_error_free(err);

#ifdef G_OS_WIN32
        /* Tentativa: usar CreateProcessW + pipes (mais robusto no Windows) */
        append_log(ctx, "[warn] spawn_async_with_pipes falhou; tentando CreateProcessW + pipes...");
        GError *werr = NULL;
        /* construir cmdline (colocar aspas onde necessário) */
        gchar *cmdline = g_strdup_printf("\"%s\" \"%s\" --csv \"%s\" --x \"%s\" --y \"%s\" --x-label \"%s\" --y-label \"%s\" --model \"%s\" --epochs %s --train-pct %s --proj \"%s\" --color-by \"%s\" --frame-every %s --out-plot \"%s\" --out-metrics \"%s\"",
                                         python, script,
                                         ctx->current_dataset_path,
                                         xname, yname, xname, yname,
                                         algo, epochs_s, train_s, proj, color, frame_s,
                                         out_plot, out_metrics);

        PROCESS_INFORMATION pi;
        int win_out_fd = -1, win_err_fd = -1;
        if (spawn_process_with_pipes_win(python, cmdline, &win_out_fd, &win_err_fd, &pi, &werr)) {
            /* criar GIOChannels a partir dos fds Windows */
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

            /* opcional: podes guardar pi.hProcess para esperar término mais tarde.
               Aqui fechamos handles do PROCESS_INFORMATION para evitar leak; se precisares
               de esperar pelo processo, guarda pi em ctx e fecha quando terminar. */
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            ctx->trainer_running = TRUE;
            append_log(ctx, "[info] started child via CreateProcessW (with pipes).");
            if (ctx->status) gtk_label_set_text(ctx->status, "Training…");
            if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
            if (ctx->right_nb && ctx->plot_page_idx >= 0) gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);

            g_free(cmdline);
            /* limpar alocações */
            g_free(train_s); g_free(epochs_s); g_free(frame_s);
            g_free(script);  g_free(python); g_free(cwd);
            g_free(out_plot); g_free(out_metrics);
            return TRUE;
        } else {
            append_log(ctx, "[error] CreateProcessW helper falhou: %s", werr ? werr->message : "(unknown)");
            if (werr) g_error_free(werr);
            g_free(cmdline);

            /* último recurso: tentar _spawnv (sem pipes), para pelo menos iniciar o processo */
            append_log(ctx, "[warn] tentando fallback _spawnv() no Windows (sem pipes)...");
            char **spawn_argv = g_new0(char*, 40);
            int ai = 0;
            for (int i = 0; argv[i] != NULL && ai < 38; ++i) spawn_argv[ai++] = argv[i];
            spawn_argv[ai] = NULL;

            intptr_t spawn_ret = _spawnv(_P_NOWAIT, (const char*)python, (const char * const*)spawn_argv);
            if (spawn_ret == -1) {
                append_log(ctx, "[error] _spawnv failed (errno=%d).", errno);
                g_free(spawn_argv);
                g_free(train_s); g_free(epochs_s); g_free(frame_s);
                g_free(script);  g_free(python); g_free(cwd);
                g_free(out_plot); g_free(out_metrics);
                return FALSE;
            } else {
                append_log(ctx, "[info] started child via _spawnv pid=%ld", (long)spawn_ret);
                ctx->trainer_running = TRUE;
                if (ctx->status) gtk_label_set_text(ctx->status, "Training…");
                if (ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
                if (ctx->right_nb && ctx->plot_page_idx >= 0) gtk_notebook_set_current_page(ctx->right_nb, ctx->plot_page_idx);
                g_free(spawn_argv);
                g_free(train_s); g_free(epochs_s); g_free(frame_s);
                g_free(script);  g_free(python); g_free(cwd);
                g_free(out_plot); g_free(out_metrics);
                return TRUE;
            }
        }
#else
        /* Em Unix, se falhou, abortamos */
        append_log(ctx, "[error] spawn_async_with_pipes falhou e não há fallback disponível neste OS.");
        g_free(train_s); g_free(epochs_s); g_free(frame_s);
        g_free(script);  g_free(python); g_free(cwd);
        g_free(out_plot); g_free(out_metrics);
        return FALSE;
#endif
    }

    /* Se spawn com pipes funcionou (POSIX ou Windows caso raro), cria canais e segue */
#ifdef G_OS_WIN32
    GIOChannel *ch_out = (out_fd >= 0) ? g_io_channel_win32_new_fd(out_fd) : NULL;
    GIOChannel *ch_err = (err_fd >= 0) ? g_io_channel_win32_new_fd(err_fd) : NULL;
#else
    GIOChannel *ch_out = (out_fd >= 0) ? g_io_channel_unix_new(out_fd) : NULL;
    GIOChannel *ch_err = (err_fd >= 0) ? g_io_channel_unix_new(err_fd) : NULL;
#endif
    if (ch_out) { g_io_channel_set_encoding(ch_out, NULL, NULL); g_io_add_watch(ch_out, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx); }
    if (ch_err) { g_io_channel_set_encoding(ch_err, NULL, NULL); g_io_add_watch(ch_err, G_IO_IN | G_IO_HUP, (GIOFunc)on_python_stdout, ctx); }

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

    /* Speed up the single-file watcher */
    if (ctx->plot_timer_id) { g_source_remove(ctx->plot_timer_id); ctx->plot_timer_id = 0; }
    ctx->plot_timer_id = g_timeout_add(90, poll_fit_image_cb, ctx); /* was 120ms */

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


/* Build the Environment tab (LEFT controls | RIGHT notebook) */
void add_environment_tab(GtkNotebook *nb, EnvCtx *ctx) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* top switcher (kept) */
    ctx->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(ctx->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), ctx->stack);
    gtk_box_pack_start(GTK_BOX(outer), switcher, FALSE, FALSE, 0);

    /* Toolbar (debug/logout area) - placed above the paned area */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->btn_debug = GTK_BUTTON(gtk_button_new_with_label("Debug"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(ctx->btn_debug), "Abrir janela de debug/backlog");
    gtk_box_pack_end(GTK_BOX(toolbar), GTK_WIDGET(ctx->btn_debug), FALSE, FALSE, 0);
    g_signal_connect(ctx->btn_debug, "clicked", G_CALLBACK(on_debug_button_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), paned, TRUE, TRUE, 0);

    /* =============== LEFT controls =============== */
    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    /* Dataset row (single, not duplicated) */
    {
        GtkWidget *ds_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->ds_combo       = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        ctx->btn_refresh_ds = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
        GtkWidget *btn_open = gtk_button_new_with_label("Open");
        GtkWidget *btn_load = gtk_button_new_with_label("Load");

        gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->ds_combo), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(ds_row), GTK_WIDGET(ctx->btn_refresh_ds), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ds_row), btn_open, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ds_row), btn_load, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Dataset", ds_row), FALSE, FALSE, 0);

        g_signal_connect(ctx->btn_refresh_ds, "clicked", G_CALLBACK(on_refresh_local_datasets), ctx);
        g_signal_connect(btn_open,            "clicked", G_CALLBACK(on_load_local_dataset),     ctx);
        g_signal_connect(btn_load,            "clicked", G_CALLBACK(on_load_selected_dataset),  ctx);
    }

    /* trainees row */
    {
        GtkWidget *tr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ctx->model_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        gtk_combo_box_text_append_text(ctx->model_combo, "(new)");
        gtk_box_pack_start(GTK_BOX(tr_row), gtk_label_new(""), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tr_row), GTK_WIDGET(ctx->model_combo), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Trainee", tr_row), FALSE, FALSE, 0);
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
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->algo_combo), 0);

        GtkWidget *lab_ep = gtk_label_new("Epochs:");
        gtk_widget_set_name(lab_ep, "epochs-label");
        GtkAdjustment *ep_adj = gtk_adjustment_new(100, 1, 100000, 1, 10, 0);
        ctx->epochs_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(ep_adj, 1, 0));
        gtk_spin_button_set_numeric(ctx->epochs_spin, TRUE);

        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->algo_combo), TRUE, TRUE, 0);
        gtk_box_pack_end  (GTK_BOX(row), GTK_WIDGET(ctx->epochs_spin), FALSE, FALSE, 0);
        gtk_box_pack_end  (GTK_BOX(row), lab_ep, FALSE, FALSE, 6);

        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Regressor", row), FALSE, FALSE, 0);
    }

    /* Projection / Color */
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

        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Projection & Color", row), FALSE, FALSE, 0);
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
        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Split (Train/Test)", box), FALSE, FALSE, 0);
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
        ctx->btn_start = GTK_BUTTON(gtk_button_new_with_label("Start"));
        ctx->btn_pause = GTK_BUTTON(gtk_button_new_with_label("Pause"));
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->btn_start), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(ctx->btn_pause), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(left_col), group_panel("Actions", row), FALSE, FALSE, 0);
        g_signal_connect(ctx->btn_start, "clicked", G_CALLBACK(on_start_clicked), ctx);
        g_signal_connect(ctx->btn_pause, "clicked", G_CALLBACK(on_pause_clicked), ctx);
    }

    /* Wrap left in metal panel + pack */
    char *ENVIRONMENT_CSS = parse_CSS_file("environment.css");
    GtkWidget *left_panel  = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", left_col,  "env-left-panel");
    gtk_paned_pack1(GTK_PANED(paned), left_panel, FALSE, FALSE);

    /* =============== RIGHT: notebook =============== */
    GtkWidget *right_nb = gtk_notebook_new();
    ctx->right_nb = GTK_NOTEBOOK(right_nb);

    /* Logs */
    ctx->logs_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(ctx->logs_view, FALSE);
    GtkWidget *sc_log = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sc_log), GTK_WIDGET(ctx->logs_view));
    gtk_notebook_append_page(ctx->right_nb, sc_log, gtk_label_new("Logs"));

    /* Plot */
    ctx->plot_img = GTK_IMAGE(gtk_image_new());
    GtkWidget *plot_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(plot_box), GTK_WIDGET(ctx->plot_img), TRUE, TRUE, 0);
    ctx->plot_page_idx = gtk_notebook_append_page(ctx->right_nb, plot_box, gtk_label_new("Plot"));

    /* Metrics */
    GtkWidget *metrics_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(metrics_view), FALSE);
    gtk_notebook_append_page(ctx->right_nb, metrics_view, gtk_label_new("Metrics"));
    ctx->metrics_view = GTK_TEXT_VIEW(metrics_view);

    /* Wrap right too (consistent depth) */
    GtkWidget *right_panel = wrap_CSS(ENVIRONMENT_CSS, "metal-panel", right_nb, "env-right-panel");
    gtk_paned_pack2(GTK_PANED(paned), right_panel, TRUE, FALSE);

    /* free CSS buffer returned by parse_CSS_file (wrap_CSS already applied it) */
    if (ENVIRONMENT_CSS) {
        free(ENVIRONMENT_CSS);
        ENVIRONMENT_CSS = NULL;
    }

    /* Footer */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    ctx->status   = GTK_LABEL(gtk_label_new("Idle"));
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->progress), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(footer), GTK_WIDGET(ctx->status),   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), footer, FALSE, FALSE, 0);

    /* Finalize */
    gtk_stack_add_titled(ctx->stack, paned, "environment", "Environment");
    gtk_notebook_append_page(nb, outer, gtk_label_new("Environment"));
    gtk_widget_show_all(outer);

    /* Default temp paths + pollers */
    if (!ctx->fit_img_path) { gchar *tmp = g_get_tmp_dir(); ctx->fit_img_path = g_build_filename(tmp, "aifd_fit.png",     NULL); }
    if (!ctx->metrics_path) { gchar *tmp = g_get_tmp_dir(); ctx->metrics_path = g_build_filename(tmp, "aifd_metrics.txt", NULL); }

    /* Use the single-file poller; metrics poller pops the tab once */
    ctx->plot_timer_id    = g_timeout_add(120, poll_fit_image_cb, ctx);
    ctx->metrics_timer_id = g_timeout_add(500,  poll_metrics_cb,  ctx);

    /* Populate datasets combo */
    on_refresh_local_datasets(GTK_BUTTON(ctx->btn_refresh_ds), ctx);
}

#endif