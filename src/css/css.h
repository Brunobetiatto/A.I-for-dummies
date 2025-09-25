#include <gtk/gtk.h>

#ifndef CSS_H
#define CSS_H

static char *parse_CSS_file(const char *Path){
    char fullPath[512];
    const char *sep = strrchr(__FILE__, '/');
    if (!sep) sep = strrchr(__FILE__, '\\');
    if (sep) {
        size_t len = sep - __FILE__ + 1;
        strncpy(fullPath, __FILE__, len);
        fullPath[len] = '\0';
        sprintf(fullPath + len, "%s", Path);
    } else {
        strcpy(fullPath, Path);
    }
    Path = fullPath;
    
    FILE *fp;
    fopen_s(&fp, Path, "r");
    if(fp == NULL) {
        return NULL;
    }

    char *str;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    str = (char *)malloc(size + 1);
    if (str == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t i = 0;
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        str[i++] = ch;
    }
    str[i] = '\0';
    fclose(fp);

    return str;
}

static void apply_CSS(const char *CSS) {
    GtkCssProvider *prov = gtk_css_provider_new();
    
    gtk_css_provider_load_from_data(prov, CSS, -1, NULL);
    GdkScreen *scr = gdk_screen_get_default();

    gtk_style_context_add_provider_for_screen(scr,
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(prov);
}

static GtkWidget* wrap_CSS(const char *CSS, const char *class_name,
                           GtkWidget *child, const char *name_opt) {
    if (CSS && *CSS) apply_CSS(CSS);   // garante provider carregado
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (name_opt && *name_opt) gtk_widget_set_name(box, name_opt);
    GtkStyleContext *sc = gtk_widget_get_style_context(box);
    gtk_style_context_add_class(sc, class_name);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0);
    return box;
}

#endif