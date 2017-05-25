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

#include<forwards.hpp>
#include<ssh_util.hpp>
#include<util.hpp>

enum PortForwardingColumns {
    HOST_COLUMN,
    LOCAL_PORT_COLUMN,
    REMOTE_PORT_COLUMN,
    PF_N_COLUMNS,
};

SshChannel open_forward_channel(PortForwardings &pf, int local_port, int remote_port, const char *source_host, const char *remote_host) {
    SshChannel forward_channel(pf.session, ssh_channel_new(pf.session));
    int rc;
    if(forward_channel == nullptr) {
        printf("Could not open ssh forwarding: %s", ssh_get_error(pf.session));
        return forward_channel;
    }

    rc = ssh_channel_open_forward(forward_channel, remote_host, remote_port, source_host, local_port);
    if(rc != SSH_OK) {
        printf("Could not open forward channel: %s", ssh_get_error(pf.session));
        return SshChannel();
    }
    return forward_channel;
}

void network_socket_has_data(GObject */*source_object*/, GAsyncResult *res, gpointer user_data) {
    ForwardState *fs = reinterpret_cast<ForwardState*>(user_data);
    int read_bytes = g_input_stream_read_finish(fs->istream, res, nullptr);
    if(read_bytes >= 0) {
        auto written_bytes = ssh_channel_write(fs->channel, fs->from_network, read_bytes);
        if(written_bytes < 0) {
            printf("Error writing: %s\n", ssh_get_error(ssh_channel_get_session(fs->channel)));
            // FIXME error out?
        }
        g_input_stream_read_async(fs->istream,
                                  fs->from_network,
                                  FORW_BLOCK_SIZE,
                                  G_PRIORITY_DEFAULT,
                                  nullptr,
                                  network_socket_has_data,
                                  fs);
    } else {
        // FIXME, close connection?
    }
}

gboolean incoming_connection(GSocketService *service, GSocketConnection *connection, GObject */*source_object*/, gpointer user_data) {
    PortForwardings &pf = *reinterpret_cast<PortForwardings*>(user_data);
    GValue val = G_VALUE_INIT;
    GtkTreeIter iter;
    GInetSocketAddress *local_address = G_INET_SOCKET_ADDRESS(g_socket_connection_get_local_address(connection, nullptr));
    int local_port = g_inet_socket_address_get_port(local_address);
    int remote_port;
    g_object_unref(G_OBJECT(local_address));

    const char *remote_host;
    bool found = false;
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pf.forward_list), &iter);
    do {
        gtk_tree_model_get_value(GTK_TREE_MODEL(pf.forward_list), &iter, LOCAL_PORT_COLUMN, &val);
        g_assert(G_VALUE_HOLDS_INT(&val));
        int current_port = g_value_get_int(&val);
        if(local_port == current_port) {
            found = true;
            break;
        }
    } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(pf.forward_list), &iter));
    g_value_unset(&val);
    if(!found) {
        return TRUE;
    }
    gtk_tree_model_get_value(GTK_TREE_MODEL(pf.forward_list), &iter, REMOTE_PORT_COLUMN, &val);
    g_assert(G_VALUE_HOLDS_INT(&val));
    remote_port = g_value_get_int(&val);
    g_value_unset(&val);
    gtk_tree_model_get_value(GTK_TREE_MODEL(pf.forward_list), &iter, HOST_COLUMN, &val);
    g_assert(G_VALUE_HOLDS_STRING(&val));
    remote_host = g_value_get_string(&val);

    GInetSocketAddress *source_address = G_INET_SOCKET_ADDRESS(g_socket_connection_get_remote_address(connection, nullptr));
    const char *source_address_string = g_inet_address_to_string(g_inet_socket_address_get_address(source_address));

    auto channel = open_forward_channel(pf, local_port, remote_port, source_address_string, remote_host);
    // Can't be released earlier as they hold the actual data used by the above function call.
    g_object_unref(source_address);
    g_value_unset(&val);
    if(channel == nullptr) {
        return TRUE;
    }
    g_object_ref(G_OBJECT(connection));
    pf.ongoing.emplace_back(std::make_unique<ForwardState>());
    ForwardState *fs = pf.ongoing.back().get();
    fs->istream = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    fs->ostream = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    fs->channel = std::move(channel);
    fs->socket_connection = connection;
    fs->port = local_port;
    g_input_stream_read_async(fs->istream,
                              fs->from_network,
                              FORW_BLOCK_SIZE,
                              G_PRIORITY_DEFAULT,
                              nullptr,
                              network_socket_has_data,
                              fs);
    return TRUE;
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
    if(g_socket_listener_add_inet_port(G_SOCKET_LISTENER(pf.listener), local_port, nullptr, nullptr)) {
        gtk_list_store_append(pf.forward_list, &iter);
        gtk_list_store_set(pf.forward_list, &iter,
                           HOST_COLUMN, host,
                           LOCAL_PORT_COLUMN, local_port,
                           REMOTE_PORT_COLUMN, remote_port,
                          -1);
    } else {
        // FIXME add errors here.
    }
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

    pf.listener = g_socket_service_new();
    g_signal_connect(G_OBJECT(pf.listener), "incoming", G_CALLBACK(incoming_connection), &pf);
}

bool feed_forwards(PortForwardings &pf) {
    bool read_data = false;
    for(const auto &f : pf.ongoing) {
        auto num_read = ssh_channel_read_nonblocking(f->channel, f->from_channel, FORW_BLOCK_SIZE, 0);
        if(num_read == SSH_AGAIN) {
            continue;
        }
        if(num_read < 0) {
            printf("Error reading reply: %s\n", ssh_get_error(pf.session));
            // FIXME, close connection.
        }
        if(num_read > 0) {
            read_data = true;
            gsize bytes_written;
            g_output_stream_write_all(f->ostream, f->from_channel, num_read, &bytes_written, nullptr, nullptr);
            if(bytes_written != (gsize)num_read) {
                // FIXME, close connection
            }
        }
    }
    return read_data;
}
