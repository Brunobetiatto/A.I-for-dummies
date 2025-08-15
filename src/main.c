// src/main.c  — dynamic single-table view (no hardcoded columns)
#include "interface/interface.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AI For Dummies - Datasets");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // filter entry used by update_table()/update_table_cmd()
    entry_global = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(entry_global, "Filter (matches any column)...");
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(entry_global), FALSE, FALSE, 0);

    // tree view — DO NOT add columns or model here; the updater will do it
    GtkWidget *treeview = gtk_tree_view_new();
    view_global = GTK_TREE_VIEW(treeview);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    gtk_widget_show_all(window);

    // First fill + periodic refresh (SECONDS(...) comes from communicator.h)
    update_table(NULL);
    g_timeout_add(SECONDS(2), update_table, NULL);

    gtk_main();
    return 0;
}
