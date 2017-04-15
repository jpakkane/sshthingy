/*
 * Copyright (C) 2017 Jussi Pakkanen.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of version 3, or (at your option) any later version,
 * of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include<libssh/libssh.h>
#include<vte/vte.h>
#include<gtk/gtk.h>

struct App {
    GtkWidget *mainWindow;
    GtkWidget *terminal;
};

int main(int argc, char **argv) {
    struct App app;
    GtkWidget *box;
    GtkWidget *scrollBar;
    GMainLoop *ml = g_main_loop_new(nullptr, FALSE);

    gtk_init(&argc, &argv);
    app.mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.mainWindow), "Unnamed SSH client");

    app.terminal = vte_terminal_new();
    vte_terminal_set_size(VTE_TERMINAL(app.terminal), 80, 25);

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    scrollBar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app.terminal)));
    gtk_box_pack_start(GTK_BOX(box), app.terminal, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), scrollBar, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app.mainWindow), box);
    gtk_widget_show_all(app.mainWindow);
    g_main_loop_run(ml);
    g_main_loop_unref(ml);
    gtk_widget_destroy(app.mainWindow);
    return 0;
}
