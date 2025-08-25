// gcc gtk_live.c -o gtk_live $(pkg-config --cflags --libs gtk+-3.0) -lcurl -lcjson -pthread
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <string.h>

typedef struct {
    GtkWidget *list_box;   // container para a lista de usuários
    volatile gboolean running;
    gchar *url;
    GThread *thread;
} App;

struct MemBuf {
    char *ptr;
    size_t len;
};

static void membuf_init(struct MemBuf *s) {
    s->len = 0;
    s->ptr = (char*)g_malloc0(1);
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct MemBuf *s = (struct MemBuf *)userdata;
    size_t add = size * nmemb;
    s->ptr = (char*)g_realloc(s->ptr, s->len + add + 1);
    memcpy(s->ptr + s->len, ptr, add);
    s->len += add;
    s->ptr[s->len] = '\0';
    return add;
}

typedef struct {
    GtkWidget *list_box;
    GList *labels; // lista de labels criados dinamicamente
} UiUpdate;

static gboolean apply_update_cb(gpointer data) {
    UiUpdate *u = (UiUpdate *)data;

    // limpa a lista anterior
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(u->list_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // adiciona os novos labels
    for (iter = u->labels; iter != NULL; iter = g_list_next(iter)) {
        GtkWidget *row = gtk_label_new((char*)iter->data);
        gtk_box_pack_start(GTK_BOX(u->list_box), row, FALSE, FALSE, 4);
        gtk_widget_show(row);
        g_free(iter->data); // libera string
    }
    g_list_free(u->labels);
    g_free(u);

    return FALSE; // one-shot
}

static gpointer fetch_loop(gpointer user_data) {
    App *app = (App *)user_data;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();

    while (g_atomic_int_get((gint *)&app->running)) {
        if (curl) {
            struct MemBuf buf; membuf_init(&buf);
            curl_easy_setopt(curl, CURLOPT_URL, app->url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK && buf.len > 0) {
                cJSON *json = cJSON_Parse(buf.ptr);
                if (json && cJSON_IsArray(json)) {
                    UiUpdate *u = g_new0(UiUpdate, 1);
                    u->list_box = app->list_box;
                    u->labels = NULL;

                    int size = cJSON_GetArraySize(json);
                    for (int i = 0; i < size; i++) {
                        cJSON *item = cJSON_GetArrayItem(json, i);
                        cJSON *nome = cJSON_GetObjectItemCaseSensitive(item, "nome");
                        cJSON *email = cJSON_GetObjectItemCaseSensitive(item, "email");

                        gchar *line = g_strdup_printf("Nome: %s | Email: %s",
                            (cJSON_IsString(nome) && nome->valuestring) ? nome->valuestring : "—",
                            (cJSON_IsString(email) && email->valuestring) ? email->valuestring : "—"
                        );
                        u->labels = g_list_append(u->labels, line);
                    }
                    g_idle_add(apply_update_cb, u);
                    cJSON_Delete(json);
                }
            }
            g_free(buf.ptr);
        }
        g_usleep(1 * G_USEC_PER_SEC);
    }

    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
    return NULL;
}

static void on_destroy(GtkWidget *w, gpointer user_data) {
    App *app = (App *)user_data;
    app->running = FALSE;
    if (app->thread) g_thread_join(app->thread);
    g_free(app->url);
    gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    App *app = g_new0(App, 1);
    app->url = g_strdup("http://127.0.0.1:5000/dados");
    app->running = TRUE;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Live JSON Viewer (Lista)");
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), app);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(win), box);
    GtkWidget *title = gtk_label_new("Usuários do servidor (atualiza em tempo real)");
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    app->list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), app->list_box, TRUE, TRUE, 4);

    gtk_widget_show_all(win);

    app->thread = g_thread_new("fetch-loop", fetch_loop, app);

    gtk_main();

    g_free(app);
    return 0;
}
