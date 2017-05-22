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
