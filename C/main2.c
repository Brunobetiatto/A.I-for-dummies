#include <gtk/gtk.h>
#include "stack.h"

Stack* stack = NULL;
GtkWidget *entry;
GtkWidget *stack_box;

void on_push_clicked(GtkWidget *widget, gpointer data);
void on_pop_clicked(GtkWidget *widget, gpointer data);


gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        on_push_clicked(NULL, NULL);  // Simula clique no botão Push
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Delete) {
        on_pop_clicked(NULL, NULL);  // Simula clique no botão Pop
        return TRUE;
    }
    return FALSE;
}

void update_display() {
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(stack_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (int i = stack->top; i >= 0; i--) {
        char text[32];
        sprintf(text, "%d", stack->array[i]);

        GtkWidget *block = gtk_frame_new(NULL);
        GtkWidget *label = gtk_label_new(text);
        gtk_container_add(GTK_CONTAINER(block), label);

        gtk_widget_set_size_request(block, 100, 40);
        gtk_frame_set_shadow_type(GTK_FRAME(block), GTK_SHADOW_ETCHED_OUT);
        gtk_widget_set_name(block, "stack-block");

        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_yalign(GTK_LABEL(label), 0.5);
        gtk_widget_set_name(label, "stack-label");

        gtk_box_pack_start(GTK_BOX(stack_box), block, FALSE, FALSE, 5);
    }

    gtk_widget_show_all(stack_box);
}

void on_push_clicked(GtkWidget *widget, gpointer data) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    if (strlen(text) == 0) return;
    int value = atoi(text);
    push(stack, value);
    update_display();
    gtk_entry_set_text(GTK_ENTRY(entry), "");
}

void on_pop_clicked(GtkWidget *widget, gpointer data) {
    int popped = pop(stack);
    update_display();
    if (popped == INT_MIN) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "A pilha está vazia!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void on_peek_clicked(GtkWidget *widget, gpointer data) {
    int top = peek(stack);
    char msg[50];
    if (top == INT_MIN)
        sprintf(msg, "A pilha está vazia!");
    else
        sprintf(msg, "Topo da pilha: %d", top);

    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void on_clear_clicked(GtkWidget *widget, gpointer data) {
    while (!isEmpty(stack))
        pop(stack);
    update_display();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    stack = createStack(10);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "#stack-block {"
        "  border: 2px solid #38e46cff;"
        "  border-radius: 10px;"
        "  background-color: #2c61beff;"
        "  padding: 5px;"
        "}"
        "#stack-label {"
        "  font-weight: bold;"
        "  font-size: 16px;"
        "}"
        "entry {"
        "  padding: 6px;"
        "  font-size: 14px;"
        "}"
        "button {"
        "  font-weight: bold;"
        "  padding: 8px;"
        "  border-radius: 5px;"
        "}", -1, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Visualizador de Pilha");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    GdkScreen *screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);


    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 20);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled, TRUE, TRUE, 0);

    stack_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(stack_box, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(scrolled), stack_box);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Digite um número para empilhar");
    gtk_box_pack_start(GTK_BOX(main_box), entry, FALSE, FALSE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);

    GtkWidget *btn_push = gtk_button_new_with_label("Push");
    g_signal_connect(btn_push, "clicked", G_CALLBACK(on_push_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), btn_push, TRUE, TRUE, 0);

    GtkWidget *btn_pop = gtk_button_new_with_label("Pop");
    g_signal_connect(btn_pop, "clicked", G_CALLBACK(on_pop_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), btn_pop, TRUE, TRUE, 0);

    GtkWidget *btn_peek = gtk_button_new_with_label("Peek");
    g_signal_connect(btn_peek, "clicked", G_CALLBACK(on_peek_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), btn_peek, TRUE, TRUE, 0);

    GtkWidget *btn_clear = gtk_button_new_with_label("Limpar");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), btn_clear, TRUE, TRUE, 0);

    update_display();
    gtk_widget_show_all(window);
    gtk_main();

    destroyStack(stack);
    return 0;
}
