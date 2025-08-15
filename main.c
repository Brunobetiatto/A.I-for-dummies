#include <gtk/gtk.h>
#include "cmd_runner.h"

GtkListStore *store_global; // Armazena os dados da tabela

// Função para atualizar os dados do Python
gboolean update_table(gpointer user_data) {
    wchar_t *saida = run_cmd_out(L"python main.py");
    if (!saida) return TRUE;

    char *saida_utf8 = g_utf16_to_utf8((gunichar2*)saida, -1, NULL, NULL, NULL);
    free(saida);

    gtk_list_store_clear(store_global);

    char *linha = strtok(saida_utf8, "\n");
    int row_count = 0;
    while (linha != NULL) {
        if (linha[0] != 'I' && linha[0] != '-') {
            int id;
            char nome[256];
            if (sscanf(linha, "%d | %s", &id, nome) == 2) {
                GtkTreeIter iter;
                gtk_list_store_append(store_global, &iter);
                // Define a cor de fundo alternada
                const char *bg_color = (row_count % 2 == 0) ? "#f0f0f0" : "#ffffff";
                gtk_list_store_set(store_global, &iter,
                                   0, id,
                                   1, nome,
                                   2, bg_color,
                                   -1);
                row_count++;
            }
        }
        linha = strtok(NULL, "\n");
    }

    g_free(saida_utf8);
    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Janela principal
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tabela de Dados do Python");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Modelo da tabela: ID, NOME, COR
    store_global = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

    // TreeView
    GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store_global));

    // Coluna ID
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "font", "Sans 30", NULL); // aumenta fonte
    GtkTreeViewColumn *col_id = gtk_tree_view_column_new_with_attributes("ID", renderer,
                                                                         "text", 0,
                                                                         "cell-background", 2,
                                                                         NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col_id);

    // Coluna NOME
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "font", "Sans 30", NULL); // aumenta fonte
    GtkTreeViewColumn *col_nome = gtk_tree_view_column_new_with_attributes("NOME", renderer,
                                                                           "text", 1,
                                                                           "cell-background", 2,
                                                                           NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col_nome);

    // Scroll
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);
    gtk_container_add(GTK_CONTAINER(window), scrolled);

    gtk_widget_show_all(window);

    // Atualiza automaticamente a cada 2 segundos
    g_timeout_add(2000, update_table, NULL);

    gtk_main();
    return 0;
}
