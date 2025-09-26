#include "../css/css.h"
#include "../backend/communicator.h"
#include "context.h"

#ifndef DATASETS_H
#define DATASETS_H

static void refresh_datasets_cb(GtkWidget *btn, gpointer user_data) {
    TabCtx *ctx = (TabCtx*)user_data;
    char *resp = NULL;

    if (!api_dump_table("dataset", &resp) || !resp) {
        fprintf(stderr, "Erro: api_dump_table retornou NULL\n");
        if (resp) free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        fprintf(stderr, "Erro: JSON inválido da API\n");
        return;
    }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        fprintf(stderr, "API retornou erro: %s\n", cJSON_IsString(msg) ? msg->valuestring : "(sem mensagem)");
        cJSON_Delete(root);
        return;
    }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) {
        fprintf(stderr, "Resposta sem 'columns' ou 'data' corretos\n");
        cJSON_Delete(root);
        return;
    }

    int ncols = cJSON_GetArraySize(columns);
    if (ncols <= 0) { cJSON_Delete(root); return; }

    /* cria um array de tipos (usar strings para exibição simples) */
    GType *types = g_new0(GType, ncols);
    for (int i = 0; i < ncols; ++i) types[i] = G_TYPE_STRING;

    /* cria novo store (ou substitui o anterior) */
    GtkListStore *new_store = gtk_list_store_newv(ncols, types);
    g_free(types);

    /* preenche as linhas (cada row é um array no JSON) */
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;
        GtkTreeIter iter;
        gtk_list_store_append(new_store, &iter);

        int colidx = 0;
        cJSON *cell;
        cJSON_ArrayForEach(cell, row) {
            char buf[1024];
            if (cJSON_IsString(cell)) {
                snprintf(buf, sizeof(buf), "%s", cell->valuestring);
            } else if (cJSON_IsNumber(cell)) {
                /* imprime inteiro quando possível, senão double */
                if (cell->valuedouble == cell->valueint)
                    snprintf(buf, sizeof(buf), "%d", cell->valueint);
                else
                    snprintf(buf, sizeof(buf), "%g", cell->valuedouble);
            } else if (cJSON_IsBool(cell)) {
                snprintf(buf, sizeof(buf), "%s", cJSON_IsTrue(cell) ? "1" : "0");
            } else if (cJSON_IsNull(cell)) {
                snprintf(buf, sizeof(buf), "");
            } else {
                snprintf(buf, sizeof(buf), "");
            }
            gtk_list_store_set(new_store, &iter, colidx, buf, -1);
            colidx++;
        }
    }

    /* troca o modelo no TreeView */
    if (ctx->store) {
        /* desconecta model antigo e libera */
        gtk_tree_view_set_model(ctx->view, NULL);
        g_object_unref(ctx->store);
        ctx->store = NULL;
    }
    ctx->store = new_store;
    gtk_tree_view_set_model(ctx->view, GTK_TREE_MODEL(ctx->store));

    /* cria colunas do TreeView se ainda não existirem (nomeadas conforme 'columns') */
    if (gtk_tree_view_get_n_columns(GTK_TREE_VIEW(ctx->view)) == 0) {
        int colidx = 0;
        cJSON *col;
        cJSON_ArrayForEach(col, columns) {
            const char *colname = cJSON_IsString(col) ? col->valuestring : "col";
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(ctx->view),
                                                       -1,
                                                       colname,
                                                       renderer,
                                                       "text", colidx,
                                                       NULL);
            colidx++;
        }
    }

    cJSON_Delete(root);
}

static void populate_ds_combo_from_api(EnvCtx *ctx) {
    if (!ctx || !ctx->ds_combo) return;

    char *resp = NULL;
    if (!api_dump_table("dataset", &resp) || !resp) {
        fprintf(stderr, "populate_ds_combo_from_api: api_dump_table failed\n");
        if (resp) free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) { fprintf(stderr, "populate_ds_combo_from_api: invalid JSON\n"); return; }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        fprintf(stderr, "API error: %s\n", cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        cJSON_Delete(root);
        return;
    }

    cJSON *columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(columns) || !cJSON_IsArray(data)) { cJSON_Delete(root); return; }

    /* find 'nome' column index */
    int nome_idx = -1;
    int ncols = cJSON_GetArraySize(columns);
    for (int i = 0; i < ncols; ++i) {
        cJSON *col = cJSON_GetArrayItem(columns, i);
        if (cJSON_IsString(col) && strcmp(col->valuestring, "nome") == 0) { nome_idx = i; break; }
    }

    /* clear and append */
    gtk_combo_box_text_remove_all(ctx->ds_combo);
    cJSON *row;
    cJSON_ArrayForEach(row, data) {
        if (!cJSON_IsArray(row)) continue;
        cJSON *cell = (nome_idx >= 0) ? cJSON_GetArrayItem(row, nome_idx) : NULL;
        if (cell && cJSON_IsString(cell)) gtk_combo_box_text_append_text(ctx->ds_combo, cell->valuestring);
    }

    cJSON_Delete(root);
}

static void on_refresh_datasets(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    populate_ds_combo_from_api(ctx);
    gtk_label_set_text(GTK_LABEL(ctx->status), "Datasets refreshed");
}

static void on_load_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;
    const char *sel = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    if (!sel) {
        gtk_label_set_text(GTK_LABEL(ctx->status), "No dataset selected");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "Loaded dataset: %s", sel);
    gtk_label_set_text(GTK_LABEL(ctx->status), buf);
    g_free((gpointer)sel);
}

static TabCtx* add_datasets_tab(GtkNotebook *nb) {
    const char *DATASETS_CSS = parse_CSS_file("datasets.css");

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 6);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filtrar dataset...");
    gtk_box_pack_start(GTK_BOX(top), entry, TRUE, TRUE, 0);

    GtkWidget *btn_refresh = gtk_button_new();

    gtk_box_pack_start(GTK_BOX(top), btn_refresh, FALSE, FALSE, 0);
    GError *err = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("./assets/refresh_button.png", &err);
    if (!pixbuf) {
        fprintf(stderr, "Erro ao carregar a imagem: %s\n", err->message);
        g_error_free(err);
    } else {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 24, 24, GDK_INTERP_BILINEAR);
        if (scaled) {
            gtk_button_set_image(GTK_BUTTON(btn_refresh), gtk_image_new_from_pixbuf(scaled));
            g_object_unref(scaled);
        }
        g_object_unref(pixbuf);
    }

    GtkWidget *tree = gtk_tree_view_new();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    gtk_box_pack_start(GTK_BOX(outer), wrap_CSS(DATASETS_CSS, "metal-panel", top, "env_top"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), wrap_CSS(DATASETS_CSS, "metal-panel", scroll, "env_scroll"), TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new("Datasets");
    gtk_notebook_append_page(nb, outer, lbl);

    TabCtx *ctx = g_new0(TabCtx, 1);
    ctx->entry = GTK_ENTRY(entry);
    ctx->view  = GTK_TREE_VIEW(tree);
    ctx->store = NULL;

    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(refresh_datasets_cb), ctx);

    gtk_widget_show_all(outer);

    refresh_datasets_cb(NULL, ctx);

    return ctx;
}


// datasets locais: upload/download

#include <gio/gio.h>

typedef struct {
    GPtrArray *columns; /* GPtrArray<char*> nomes de colunas */
    GPtrArray *rows;    /* GPtrArray<GPtrArray<char*>> linhas, cada célula é string */
    char delim;
} CsvPreview;

/* --- Utils de CSV --- */
static char detect_delim(const char *header) {
    if (!header) return ',';
    int c = 0, s = 0, t = 0;
    for (const char *p = header; *p; ++p) {
        if (*p == ',') c++;
        else if (*p == ';') s++;
        else if (*p == '\t') t++;
    }
    if (t >= c && t >= s) return '\t';
    if (s >= c && s >= t) return ';';
    return ',';
}

/* parse [simples] que respeita aspas duplas básicas: "a,b",c -> [a,b] [c] */
static void split_csv_line(const char *line, char delim, GPtrArray *out_cells) {
    const char *p = line;
    GString *cell = g_string_new(NULL);
    gboolean in_quotes = FALSE;

    while (*p) {
        char ch = *p++;
        if (ch == '"') {
            if (in_quotes && *p == '"') { g_string_append_c(cell, '"'); p++; }
            else in_quotes = !in_quotes;
        } else if (ch == delim && !in_quotes) {
            g_ptr_array_add(out_cells, g_string_free(cell, FALSE));
            cell = g_string_new(NULL);
        } else if (ch == '\r') {
            /* ignore */
        } else if (ch == '\n' && !in_quotes) {
            break;
        } else {
            g_string_append_c(cell, ch);
        }
    }
    g_ptr_array_add(out_cells, g_string_free(cell, FALSE));
}

/* --- Constrói preview em GtkTreeView --- */
static void tv_build_from_preview(GtkTreeView *tv, CsvPreview *pv, guint max_cols) {
    /* Limpa colunas antigas */
    GList *cols = gtk_tree_view_get_columns(tv);
    for (GList *l = cols; l; l = l->next) gtk_tree_view_remove_column(tv, GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(cols);

    /* Proteção */
    guint ncols = pv->columns ? pv->columns->len : 0;
    if (ncols == 0) return;
    if (max_cols && ncols > max_cols) ncols = max_cols;

    /* model: tudo string */
    GType *types = g_new0(GType, ncols);
    for (guint i=0;i<ncols;i++) types[i] = G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(ncols, types);
    g_free(types);

    /* cria colunas visuais */
    for (guint i=0;i<ncols;i++) {
        const char *title = i < pv->columns->len ? (const char*)pv->columns->pdata[i] : "";
        GtkCellRenderer *rend = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(title, rend, "text", i, NULL);
        gtk_tree_view_append_column(tv, col);
    }

    /* popula até ~200 linhas pra não pesar */
    guint shown = 0;
    for (guint r=0; r<pv->rows->len && shown < 200; r++, shown++) {
        GPtrArray *cells = pv->rows->pdata[r];
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        for (guint c=0;c<ncols;c++) {
            const char *val = (c < cells->len) ? (const char*)cells->pdata[c] : "";
            gtk_list_store_set(store, &it, c, val, -1);
        }
    }

    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    g_object_unref(store);
}


static void csv_preview_free(CsvPreview *pv) {
    if (!pv) return;
    if (pv->columns) g_ptr_array_free(pv->columns, TRUE);   /* cells freed by free_func */
    if (pv->rows)    g_ptr_array_free(pv->rows, TRUE);      /* each row unref'd; rows free their cells */
    g_free(pv);
}

typedef struct {
    gchar *path;
    GtkTreeView *target_tv;   // <— add this
} LoadTaskData;

static void task_read_preview(GTask *task, gpointer src, gpointer task_data, GCancellable *canc) {
    LoadTaskData *td = (LoadTaskData*)task_data;
    GError *err = NULL;

    GFile *gf = g_file_new_for_path(td->path);
    GFileInputStream *fis = g_file_read(gf, canc, &err);
    if (!fis) {
        g_task_return_error(task, err);
        g_object_unref(gf);
        return;
    }
    GDataInputStream *din = g_data_input_stream_new(G_INPUT_STREAM(fis));
    g_object_unref(fis);

    CsvPreview *pv = g_new0(CsvPreview, 1);
    pv->columns = g_ptr_array_new_with_free_func(g_free);
    pv->rows    = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);

    /* header */
    gsize len=0;
    gchar *line = g_data_input_stream_read_line(din, &len, canc, &err);
    if (!line) {
        g_clear_object(&din); g_object_unref(gf);
        if (!err) err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "Empty file");
        g_task_return_error(task, err);
        csv_preview_free(pv);
        return;
    }
    pv->delim = detect_delim(line);
    GPtrArray *hdr = g_ptr_array_new_with_free_func(g_free);
    split_csv_line(line, pv->delim, hdr);
    for (guint i=0;i<hdr->len;i++) g_ptr_array_add(pv->columns, g_strdup((char*)hdr->pdata[i]));
    g_ptr_array_free(hdr, TRUE);
    g_free(line);

    /* rows */
    guint cap = 10000; /* preview limit */
    for (guint r=0; r<cap; r++) {
        line = g_data_input_stream_read_line(din, &len, canc, &err);
        if (!line) break;
        GPtrArray *row = g_ptr_array_new_with_free_func(g_free);
        split_csv_line(line, pv->delim, row);
        g_ptr_array_add(pv->rows, row);
        g_free(line);
    }

    g_clear_object(&din);
    g_object_unref(gf);

    if (err) {
        csv_preview_free(pv);
        g_task_return_error(task, err);
        return;
    }
    g_task_return_pointer(task, pv, (GDestroyNotify)csv_preview_free);
}

/* --- Callback após worker --- */
/* REPLACE the whole on_task_done(...) with: */
static void on_task_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    GError *err = NULL;
    CsvPreview *pv = g_task_propagate_pointer(G_TASK(res), &err);
    LoadTaskData *td = (LoadTaskData*)g_task_get_task_data(G_TASK(res));

    if (ctx && ctx->progress) gtk_progress_bar_set_fraction(ctx->progress, 0.0);
    if (ctx && ctx->status)   gtk_label_set_text(ctx->status, err ? "Load failed" : "Loaded");

    if (err) {
        GtkWidget *dlg = gtk_message_dialog_new(
            (ctx && ctx->main_window) ? GTK_WINDOW(ctx->main_window) : NULL,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Erro ao ler dataset: %s", err->message);
        gtk_window_set_title(GTK_WINDOW(dlg), "Erro");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        g_error_free(err);
        return;
    }

    if (td && td->target_tv) {
        tv_build_from_preview(td->target_tv, pv, 64);
    }

    //csv_preview_free(pv);
}

static void free_load_task_data(LoadTaskData *td) {
    if (!td) return;
    g_free(td->path);
    g_free(td);
}

/* helper: find a notebook page by visible label text */
static gint find_notebook_page_by_label(GtkNotebook *nb, const char *label) {
    if (!nb || !label) return -1;
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i=0; i<n; ++i) {
        GtkWidget *child = gtk_notebook_get_nth_page(nb, i);
        GtkWidget *tab   = gtk_notebook_get_tab_label(nb, child);
        if (GTK_IS_LABEL(tab)) {
            const gchar *txt = NULL;
            g_object_get(tab, "label", &txt, NULL); // works in GTK3
            if (txt && g_strcmp0(txt, label) == 0) return i;
        }
    }
    return -1;
}

static void start_load_file(EnvCtx *ctx, const char *path) {
    if (!ctx || !path || !*path) return;

    g_free(ctx->current_dataset_path);
    ctx->current_dataset_path = g_strdup(path);

    if (ctx->status)   gtk_label_set_text(ctx->status, "Loading…");
    if (ctx->progress) gtk_progress_bar_pulse(ctx->progress);

    // Reuse or create the singleton “Preview dataset” page
    const char *TAB_NAME = "Preview dataset";
    GtkTreeView *tv = ctx->ds_preview_tv;
    gint page_idx = find_notebook_page_by_label(ctx->right_nb, TAB_NAME);

    if (!tv || page_idx < 0) {
        // first time: create
        GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *tvw = gtk_tree_view_new();
        gtk_container_add(GTK_CONTAINER(sc), tvw);
        ctx->ds_preview_tv = GTK_TREE_VIEW(tvw);

        gint page = gtk_notebook_append_page(ctx->right_nb, sc, gtk_label_new(TAB_NAME));
        gtk_widget_show_all(sc);
        gtk_notebook_set_current_page(ctx->right_nb, page);
    } else {
        // reuse: just switch to it (model/cols are rebuilt in on_task_done)
        gtk_notebook_set_current_page(ctx->right_nb, page_idx);
    }

    // Launch worker and tell it where to render
    LoadTaskData *td = g_new0(LoadTaskData, 1);
    td->path = g_strdup(path);
    td->target_tv = ctx->ds_preview_tv;

    GTask *t = g_task_new(NULL, NULL, on_task_done, ctx);
    g_task_set_task_data(t, td, (GDestroyNotify)free_load_task_data);
    g_task_run_in_thread(t, task_read_preview);
    g_object_unref(t);
}


static void on_load_selected_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    gchar *path = gtk_combo_box_text_get_active_text(ctx->ds_combo);
    if (!path) {
        if (ctx->status) gtk_label_set_text(ctx->status, "No dataset selected");
        return;
    }
    if (ctx->status) gtk_label_set_text(ctx->status, "Loading…");
    start_load_file(ctx, path);
    g_free(path);
}

/* --- File Chooser (Load) --- */
static void on_load_local_dataset(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx) return;

    GtkWindow *parent = NULL;
    if (ctx->main_window && GTK_IS_WINDOW(ctx->main_window))
        parent = GTK_WINDOW(ctx->main_window);

#if defined(G_OS_WIN32)
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *fc = GTK_FILE_CHOOSER(dlg);
#else
#  if GTK_CHECK_VERSION(3,20,0)
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Choose a dataset", parent,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open", "_Cancel");
    GtkFileChooser *fc = GTK_FILE_CHOOSER(native);
#  else
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *fc = GTK_FILE_CHOOSER(dlg);
#  endif
#endif

    /* filtros */
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, "Data files (CSV/TSV)");
    gtk_file_filter_add_pattern(flt, "*.csv");
    gtk_file_filter_add_pattern(flt, "*.tsv");
    gtk_file_filter_add_mime_type(flt, "text/csv");
    gtk_file_filter_add_mime_type(flt, "text/tab-separated-values");
    gtk_file_chooser_add_filter(fc, flt);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(fc, all);

    /* diretório sugerido: ./datasets ao lado do executável */
    gchar *cwd = g_get_current_dir();
    gchar *datasets_dir = g_build_filename(cwd, "datasets", NULL);
    if (g_file_test(datasets_dir, G_FILE_TEST_IS_DIR))
        gtk_file_chooser_set_current_folder(fc, datasets_dir);
    g_free(datasets_dir);
    g_free(cwd);

    /* run + fallback se necessário */
    gboolean accepted = FALSE;
    char *path = NULL;

#if defined(G_OS_WIN32)
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    }
    gtk_widget_destroy(dlg);
#else
#  if GTK_CHECK_VERSION(3,20,0)
    gint resp = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    } else if (resp == GTK_RESPONSE_DELETE_EVENT) {
        /* Fallback explícito pro Dialog se o Native falhar/fechar de forma anômala */
        GtkWidget *dlg2 = gtk_file_chooser_dialog_new(
            "Choose a dataset", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
        GtkFileChooser *fc2 = GTK_FILE_CHOOSER(dlg2);
        gint r2 = gtk_dialog_run(GTK_DIALOG(dlg2));
        if (r2 == GTK_RESPONSE_ACCEPT) {
            path = gtk_file_chooser_get_filename(fc2);
            accepted = (path != NULL);
        }
        gtk_widget_destroy(dlg2);
    }
    g_object_unref(native);
#  else
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        path = gtk_file_chooser_get_filename(fc);
        accepted = (path != NULL);
    }
    gtk_widget_destroy(dlg);
#  endif
#endif

    if (!accepted || !path) {
        if (ctx->status) gtk_label_set_text(ctx->status, "Cancelled");
        if (path) g_free(path);
        return;
    }

    if (ctx->ds_combo) {
        gtk_combo_box_text_append_text(ctx->ds_combo, path);
        GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(ctx->ds_combo));
        if (m) {
            gint n = gtk_tree_model_iter_n_children(m, NULL);
            if (n > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), n - 1);
        }
    }
    if (ctx->status) gtk_label_set_text(ctx->status, "Ready to load");
    g_free(path);
}


/* --- Popular combo de datasets a partir de ./datasets --- */
static void on_refresh_local_datasets(GtkButton *btn, gpointer user_data) {
    EnvCtx *ctx = (EnvCtx*)user_data;
    if (!ctx || !ctx->ds_combo) return;

    /* limpa combo de forma correta e sem leaks */
    gtk_combo_box_text_remove_all(ctx->ds_combo);

    gchar *cwd = g_get_current_dir();
    gchar *datasets_dir = g_build_filename(cwd, "datasets", NULL);
    if (g_file_test(datasets_dir, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(datasets_dir, 0, NULL);
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            if (g_str_has_suffix(name, ".csv") || g_str_has_suffix(name, ".tsv")) {
                gchar *full = g_build_filename(datasets_dir, name, NULL);
                gtk_combo_box_text_append_text(ctx->ds_combo, full);
                g_free(full);
            }
        }
        g_dir_close(dir);
    }
    g_free(datasets_dir);
    g_free(cwd);

    /* seleciona o primeiro, se houver */
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(ctx->ds_combo));
    if (m && gtk_tree_model_iter_n_children(m, NULL) > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->ds_combo), 0);
    }

    if (ctx->status) gtk_label_set_text(ctx->status, "Datasets refreshed");
}


#endif