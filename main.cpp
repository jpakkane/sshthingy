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
#include<glib/gstdio.h>
#include<fcntl.h>

static const int SFTP_BUF_SIZE = 4*1024;

enum SftpViewColumns {
    NAME_COLUMN,
    N_COLUMNS,
};


struct SftpWindow {
    GtkBuilder *builder;
    GtkWindow *sftp_window;
    GtkTreeView *file_view;
    GtkListStore *file_list;
    GtkProgressBar *progress;
    SftpFile remote_file;
    int async_request;
    std::string dirname;
    int local_file;
    char buf[SFTP_BUF_SIZE];
    bool downloading;
};

enum PortForwardingColumns {
    HOST_COLUMN,
    LOCAL_PORT_COLUMN,
    REMOTE_PORT_COLUMN,
    PF_N_COLUMNS,
};

struct PortForwardings {
    GtkBuilder *forwardingBuilder;
    GtkBuilder *newBuilder;

    GtkWindow *forwardWindow;
    GtkWindow *createWindow;

    GtkTreeView *forwardings;
    GtkListStore *forward_list;

    GtkButton *create_button;
    GtkButton *delete_button;

    GtkSpinButton *local_spin;
    GtkSpinButton *remote_spin;
    GtkEntry *host_entry;
    GtkButton *ok_button;
    GtkButton *cancel_button;

};

struct App {
    GtkWidget *mainWindow;
    VteTerminal *terminal;
    SshSession session;
    SshChannel pty;
    SftpSession sftp;
    PortForwardings ports;

    GIOChannel *session_channel;
    GtkBuilder *connectionBuilder;
    SftpWindow sftp_win;
};

void feed_terminal(App &a) {
    const int bufsize = 1024;
    char buf[bufsize];
    auto num_read = a.pty.read(buf, bufsize);
    vte_terminal_feed(a.terminal, buf, num_read);
}

void feed_sftp(App &a) {
    ssize_t bytes_written, bytes_read;
    if(a.sftp_win.remote_file == nullptr) {
        return;
    }
    // FIXME, can only read from remote.
    bytes_read = sftp_async_read(a.sftp_win.remote_file, a.sftp_win.buf, SFTP_BUF_SIZE, a.sftp_win.async_request);
    if(bytes_read == SSH_AGAIN) {
        return;
    }
    if(bytes_read == 0) {
        goto cleanup;
    }
    if(bytes_read < 0) {
        printf("Error reading file: %s\n", ssh_get_error(a.session));
        goto cleanup;
    }
    bytes_written = write(a.sftp_win.local_file, a.sftp_win.buf, bytes_read);
    if(bytes_written != bytes_read) {
        printf("Could not write to file.");
        goto cleanup;
    }
    a.sftp_win.async_request = sftp_async_read_begin(a.sftp_win.remote_file, SFTP_BUF_SIZE);
    return; // There is more data to transfer.

cleanup:
    close(a.sftp_win.local_file);
    a.sftp_win.remote_file = SftpFile();
}


gboolean session_has_data(GIOChannel *channel, GIOCondition cond, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    feed_terminal(a);
    if(a.sftp_win.downloading) {
        feed_sftp(a);
    }
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
    app.pty = s.open_shell();
    //app.sftp = s.open_sftp_session();
    app.session_channel = g_io_channel_unix_new(ssh_get_fd(s));
    g_io_add_watch(app.session_channel, G_IO_IN, session_has_data, &app);
}

gboolean async_uploader(gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    auto bytes_read = read(a.sftp_win.local_file, a.sftp_win.buf, SFTP_BUF_SIZE);
    ssize_t bytes_written = 0;
    feed_terminal(a); // Otherwise libssh drains the socket and glib does not see the events.
    if(bytes_read < 0) {
        printf("Could not read from file.\n");
        goto cleanup;
    }
    if(bytes_read == 0) {
        // All of input file has been read. Time to stop.
        goto cleanup;
    }
    while(bytes_written < bytes_read) {
        auto current_written = sftp_write(a.sftp_win.remote_file, a.sftp_win.buf + bytes_written, bytes_read-bytes_written);
        if(current_written < 0) {
            printf("Writing failed: %s", ssh_get_error(a.session));
            goto cleanup;
        }
        bytes_written += current_written;
    }
    return G_SOURCE_CONTINUE;

cleanup:
    close(a.sftp_win.local_file);
    a.sftp_win.remote_file = SftpFile();
    return G_SOURCE_REMOVE;
}

void upload_file(App &a, const char *fname) {
    int local_file = g_open(fname, O_RDONLY, 0);
    if(local_file < 0) {
        printf("Could not open file: %s\n", fname);
        return;
    }

    std::string remote_name = a.sftp_win.dirname + "/" + split_filename(fname);
    auto remote_file = sftp_open(a.sftp, remote_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if(remote_file == nullptr) {
        printf("Could not open remote file %s.\n", ssh_get_error(a.session));
        return;
    }
    a.sftp_win.remote_file = SftpFile(remote_file);
    a.sftp_win.local_file = local_file;
    g_idle_add(async_uploader, &a);
}

void load_sftp_dir_data(App &a, const char *newdir) {
    SftpWindow &s = a.sftp_win;
    s.dirname = newdir;
    gtk_list_store_clear(s.file_list);
    SftpDir dir = a.sftp.open_directory(newdir);
    SftpAttributes attribute;
    GtkTreeIter iter;
    while((attribute = sftp_readdir(a.sftp, dir))) {
        gtk_list_store_append(s.file_list, &iter);
        gtk_list_store_set(s.file_list, &iter,
                            NAME_COLUMN, attribute->name,
                           -1);
    }
}

void sftp_row_activated(GtkTreeView       *tree_view,
                        GtkTreePath       *path,
                        GtkTreeViewColumn *column,
                        gpointer          data) {
    App &a = *reinterpret_cast<App*>(data);
    // FIXME assumes that the clicked thing is a file name.
    GtkTreeIter iter;
    gtk_tree_model_get_iter(GTK_TREE_MODEL(a.sftp_win.file_list), &iter, path);
    GValue val = G_VALUE_INIT;
    gtk_tree_model_get_value(GTK_TREE_MODEL(a.sftp_win.file_list), &iter, NAME_COLUMN, &val);
    g_assert(G_VALUE_HOLDS_STRING(&val));
    const char *fname = g_value_get_string(&val);
    std::string full_remote_path = fname; // FIXME, path
    std::string full_local_path = fname; // FIXME, use file dialog.
    g_value_unset(&val);
    auto remote_file = sftp_open(a.sftp, full_remote_path.c_str(), O_RDONLY, 0);
    if(remote_file == nullptr) {
        printf("Could not open file: %s\n", ssh_get_error(a.session));
        return;
    }
    sftp_file_set_nonblocking(remote_file);
    a.sftp_win.local_file = g_open(full_local_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if(a.sftp_win.local_file < 0) {
        printf("Could not open local file.");
        return;
    }
    int async_request = sftp_async_read_begin(remote_file, SFTP_BUF_SIZE);
    a.sftp_win.remote_file = std::move(remote_file);
    a.sftp_win.async_request = async_request;
    a.sftp_win.downloading = true;
}

void open_sftp(App &a) {
    a.sftp_win.builder = gtk_builder_new_from_file(data_file_name("sftpwindow.glade").c_str());
    a.sftp_win.sftp_window = GTK_WINDOW(gtk_builder_get_object(a.sftp_win.builder, "sftp_window"));
    a.sftp_win.file_view = GTK_TREE_VIEW(gtk_builder_get_object(a.sftp_win.builder, "fileview"));
    a.sftp_win.file_list = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING);
    a.sftp_win.progress = GTK_PROGRESS_BAR(gtk_builder_get_object(a.sftp_win.builder, "transfer_progress"));

    gtk_tree_view_set_model(a.sftp_win.file_view, GTK_TREE_MODEL(a.sftp_win.file_list));
    gtk_tree_view_append_column(a.sftp_win.file_view,
                gtk_tree_view_column_new_with_attributes("Filename",
                        gtk_cell_renderer_text_new(), "text", NAME_COLUMN, nullptr));
    g_signal_connect(GTK_WIDGET(a.sftp_win.file_view), "row-activated", G_CALLBACK(sftp_row_activated),& a);
    //gtk_widget_show_all(GTK_WIDGET(a.sftp_win.sftp_window));
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
    // All hacks here.
    open_sftp(a);
    a.sftp_win.dirname = ".";
    feed_terminal(a);
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

void open_sftp_window(GtkMenuItem *, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
}

void open_forwardings_window(GtkMenuItem *, gpointer data) {
    App &a = *reinterpret_cast<App*>(data);
    gtk_widget_show_all(GTK_WIDGET(a.ports.forwardWindow));
}

void open_new_forwarding(GtkMenuItem*, gpointer data) {
    PortForwardings &pf = *reinterpret_cast<PortForwardings*>(data);
    gtk_widget_show_all(GTK_WIDGET(pf.createWindow));
}

void delete_forwarding(GtkMenuItem*, gpointer data) {
    PortForwardings &pf = *reinterpret_cast<PortForwardings*>(data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(pf.forwardings);
    GtkTreeIter iter;
    GtkTreeModel *m = nullptr;
    if(gtk_tree_selection_get_selected(sel, &m, &iter)) {
        auto path = gtk_tree_model_get_path(m, &iter);
        int row = gtk_tree_path_get_indices(path)[0];
        // FIXME delete ongoing connections for this rule.
        gtk_tree_path_free(path);
        gtk_list_store_remove(pf.forward_list, &iter);
    }

}

void create_new_forwarding(GtkMenuItem*, gpointer data) {
    PortForwardings &pf = *reinterpret_cast<PortForwardings*>(data);
    GtkTreeIter iter;
    gtk_widget_hide(GTK_WIDGET(pf.createWindow));
    int local_port = gtk_spin_button_get_value_as_int(pf.local_spin);
    int remote_port = gtk_spin_button_get_value_as_int(pf.remote_spin);
    const gchar *host = gtk_entry_get_text(pf.host_entry);
    if(host == nullptr || host[0] == '\0') {
        return;
    }
    // FIXME, check that there is not already a forwarding for the
    // given port.
    gtk_list_store_append(pf.forward_list, &iter);
    gtk_list_store_set(pf.forward_list, &iter,
                       HOST_COLUMN, host,
                       LOCAL_PORT_COLUMN, local_port,
                       REMOTE_PORT_COLUMN, remote_port,
                      -1);
}

void close_new_fw_window(GtkMenuItem*, gpointer data) {
    PortForwardings &pf = *reinterpret_cast<PortForwardings*>(data);
    gtk_widget_hide(GTK_WIDGET(pf.createWindow));
}

void build_port_gui(PortForwardings &pf) {
    GtkBuilder *portBuilder = gtk_builder_new_from_file(data_file_name("forwardings.glade").c_str());
    GtkBuilder *newPortBuilder = gtk_builder_new_from_file(data_file_name("createforwarding.glade").c_str());

    pf.forwardingBuilder = portBuilder;
    pf.newBuilder = newPortBuilder;

    pf.forwardWindow = GTK_WINDOW(gtk_builder_get_object(portBuilder, "forwarding_window"));
    pf.createWindow = GTK_WINDOW(gtk_builder_get_object(newPortBuilder, "create_forwarding_window"));
    pf.forwardings = GTK_TREE_VIEW(gtk_builder_get_object(portBuilder, "forwards_view"));
    pf.forward_list = gtk_list_store_new(PF_N_COLUMNS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);

    pf.create_button = GTK_BUTTON(gtk_builder_get_object(portBuilder, "create_button"));
    pf.delete_button = GTK_BUTTON(gtk_builder_get_object(portBuilder, "delete_button"));

    pf.host_entry = GTK_ENTRY(gtk_builder_get_object(newPortBuilder, "host_entry"));
    pf.remote_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(newPortBuilder, "remote_spin"));
    pf.local_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(newPortBuilder, "local_spin"));
    pf.ok_button = GTK_BUTTON(gtk_builder_get_object(newPortBuilder, "ok_button"));
    pf.cancel_button = GTK_BUTTON(gtk_builder_get_object(newPortBuilder, "cancel_button"));

    g_signal_connect(G_OBJECT(pf.create_button), "clicked", G_CALLBACK(open_new_forwarding), &pf);
    g_signal_connect(G_OBJECT(pf.delete_button), "clicked", G_CALLBACK(delete_forwarding), &pf);
    g_signal_connect(G_OBJECT(pf.ok_button), "clicked", G_CALLBACK(create_new_forwarding), &pf);
    g_signal_connect(G_OBJECT(pf.cancel_button), "clicked", G_CALLBACK(close_new_fw_window), &pf);

    // Connect view to model.
    gtk_tree_view_set_headers_visible(pf.forwardings, TRUE);
    gtk_tree_view_set_model(pf.forwardings, GTK_TREE_MODEL(pf.forward_list));
    gtk_tree_view_append_column(pf.forwardings,
                gtk_tree_view_column_new_with_attributes("Hostname",
                gtk_cell_renderer_text_new(), "text", HOST_COLUMN, nullptr));
    gtk_tree_view_append_column(pf.forwardings,
                gtk_tree_view_column_new_with_attributes("Local port",
                gtk_cell_renderer_text_new(), "text", LOCAL_PORT_COLUMN, nullptr));
    gtk_tree_view_append_column(pf.forwardings,
                gtk_tree_view_column_new_with_attributes("Remote port",
                gtk_cell_renderer_text_new(), "text", REMOTE_PORT_COLUMN, nullptr));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(pf.forwardings), GTK_SELECTION_SINGLE);
}

void build_gui(App &app) {
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *scrollBar;
    GtkWidget *menubar;
    GtkWidget *filemenu;
    GtkWidget *actionmenu;

    app.mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    build_port_gui(app.ports);
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

    actionmenu = gtk_menu_new();
    GtkWidget *amenu = gtk_menu_item_new_with_label("Actions");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(amenu), actionmenu);
    auto opensftp = gtk_menu_item_new_with_label("Open file transfer");
    auto openforward = gtk_menu_item_new_with_label("Open port forwardings");
    gtk_menu_shell_append(GTK_MENU_SHELL(actionmenu), opensftp);
    gtk_menu_shell_append(GTK_MENU_SHELL(actionmenu), openforward);
    g_signal_connect(opensftp, "activate", G_CALLBACK(open_sftp_window), &app);
    g_signal_connect(openforward, "activate", G_CALLBACK(open_forwardings_window), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), amenu);

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
    struct App *app = new App();

    gtk_init(&argc, &argv);
    build_gui(*app);

    gtk_widget_show_all(app->mainWindow);
    gtk_main();
    delete app;
    return 0;
}
