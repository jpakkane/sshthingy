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

#include<ssh_util.hpp>
#include<util.hpp>
#include<vte/vte.h>
#include<gtk/gtk.h>

struct App {
    GtkWidget *mainWindow;
    VteTerminal *terminal;
    SshSession session;
    SshChannel pty;
    GIOChannel *session_channel;
    GtkBuilder *connectionBuilder;
};

gboolean session_has_data(GIOChannel *channel, GIOCondition cond, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    const int bufsize = 1024;
    char buf[bufsize];
    auto num_read = a.pty.read(buf, bufsize);
    vte_terminal_feed(a.terminal, buf, num_read);
    return TRUE;
}

gboolean key_pressed_cb(GtkWidget *widget, GdkEvent  *event, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    GdkEventKey *eventkey = reinterpret_cast<GdkEventKey*>(event);
    a.pty.write(eventkey->string, eventkey->length); // FIXME, this is wrong
    return TRUE;
}

void connect(App &app, const char *hostname, const char *username, const char *passphrase, int conn_type) {
    const unsigned int port=22;
    SshSession &s = app.session;
    ssh_options_set(s, SSH_OPTIONS_HOST, hostname);
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    auto rc = ssh_connect(s);
    if(rc != SSH_OK) {
        printf("Could not connect: %s\n", ssh_get_error(s));
        gtk_main_quit();
        return;
    }
    auto state = ssh_is_server_known(s);
    if(state != SSH_SERVER_KNOWN_OK) {
        printf("Server is not previously known.\n");
        gtk_main_quit();
        return;
    }
    if(conn_type == 0) {
        rc = ssh_userauth_password(s, username, passphrase);
    } else {
        rc = ssh_userauth_autopubkey(s, passphrase);
    }
    if(rc != SSH_OK) {
        printf("Could not connect: %s\n", ssh_get_error(s));
        gtk_main_quit();
        return;
    }
    app.pty = s.openShell();
    app.session_channel = g_io_channel_unix_new(ssh_get_fd(s));
    g_io_add_watch(app.session_channel, G_IO_IN, session_has_data, &app);
}

void open_connection(GtkMenuItem *, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    GtkWidget *host = GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "host_entry"));
    GtkWidget *password = GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "password_entry"));
    GtkWidget *username = GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "username_entry"));
    GtkWidget *authentication = GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "authentication_combo"));
    const char *host_str = gtk_entry_get_text(GTK_ENTRY(host));
    const char *username_str = gtk_entry_get_text(GTK_ENTRY(username));
    const char *password_str = gtk_entry_get_text(GTK_ENTRY(password));
    gint active_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(authentication));
    connect(a, host_str, username_str, password_str, active_mode);
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "connection_window")));
    g_object_unref(G_OBJECT(a.connectionBuilder));
    a.connectionBuilder = nullptr;
}

void connect_cancelled(GtkMenuItem *, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "connection_window")));
    g_object_unref(G_OBJECT(a.connectionBuilder));
    a.connectionBuilder = nullptr;
}

void launch_connection_dialog(GtkMenuItem *, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    a.connectionBuilder = gtk_builder_new_from_file(data_file_name("connectiondialog.glade").c_str());
    auto connectionWindow = GTK_WIDGET(gtk_builder_get_object(a.connectionBuilder, "connection_window"));
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(a.connectionBuilder, "username_entry")), g_get_user_name());
    g_signal_connect(gtk_builder_get_object(a.connectionBuilder, "connect_button"), "clicked", G_CALLBACK(open_connection), &a);
    g_signal_connect(gtk_builder_get_object(a.connectionBuilder, "cancel_button"), "clicked", G_CALLBACK(connect_cancelled), &a);
    gtk_widget_show_all(connectionWindow);
}


void build_gui(App &app) {
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *scrollBar;
    GtkWidget *menubar;
    GtkWidget *filemenu;

    app.mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.mainWindow), "Unnamed SSH client");
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    menubar = gtk_menu_bar_new();
    filemenu = gtk_menu_new();
    GtkWidget *fmenu = gtk_menu_item_new_with_label("File");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fmenu), filemenu);
    auto connect = gtk_menu_item_new_with_label("Connect");
    auto quit = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), connect);
    g_signal_connect(connect, "activate", G_CALLBACK(launch_connection_dialog), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
    g_signal_connect(quit, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), fmenu);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    app.terminal = VTE_TERMINAL(vte_terminal_new());
    vte_terminal_set_size(app.terminal, 80, 25);
    vte_terminal_set_cursor_blink_mode(app.terminal, VTE_CURSOR_BLINK_OFF);

    scrollBar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app.terminal)));
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(app.terminal), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), scrollBar, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app.mainWindow), vbox);
    g_signal_connect(app.mainWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(GTK_WIDGET(app.terminal), "key-press-event", G_CALLBACK(key_pressed_cb), &app);
}

int main(int argc, char **argv) {
    struct App app;

    gtk_init(&argc, &argv);
    build_gui(app);

    gtk_widget_show_all(app.mainWindow);
    gtk_main();
    return 0;
}
