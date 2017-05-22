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

#include<sftp.hpp>
#include<fcntl.h>
#include<util.hpp>
#include<glib/gstdio.h>

enum SftpViewColumns {
    NAME_COLUMN,
    N_COLUMNS,
};

void feed_sftp(SftpWindow &sftp_win) {
    ssize_t bytes_written, bytes_read;
    if(sftp_win.remote_file == nullptr) {
        return;
    }
    // FIXME, can only read from remote.
    bytes_read = sftp_async_read(sftp_win.remote_file, sftp_win.buf, SFTP_BUF_SIZE, sftp_win.async_request);
    if(bytes_read == SSH_AGAIN) {
        return;
    }
    if(bytes_read == 0) {
        goto cleanup;
    }
    if(bytes_read < 0) {
        printf("Error reading file: %s\n", ssh_get_error(sftp_win.session));
        goto cleanup;
    }
    bytes_written = write(sftp_win.local_file, sftp_win.buf, bytes_read);
    if(bytes_written != bytes_read) {
        printf("Could not write to file.");
        goto cleanup;
    }
    sftp_win.async_request = sftp_async_read_begin(sftp_win.remote_file, SFTP_BUF_SIZE);
    return; // There is more data to transfer.

cleanup:
    close(sftp_win.local_file);
    sftp_win.remote_file = SftpFile();
}

void load_sftp_dir_data(SftpWindow &s, const char *newdir) {
    s.dirname = newdir;
    gtk_list_store_clear(s.file_list);
    SftpDir dir = s.sftp.open_directory(newdir);
    SftpAttributes attribute;
    GtkTreeIter iter;
    while((attribute = sftp_readdir(s.sftp, dir))) {
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
    SftpWindow *sftp_win = reinterpret_cast<SftpWindow*>(data);
    // FIXME assumes that the clicked thing is a file name.
    GtkTreeIter iter;
    gtk_tree_model_get_iter(GTK_TREE_MODEL(sftp_win->file_list), &iter, path);
    GValue val = G_VALUE_INIT;
    gtk_tree_model_get_value(GTK_TREE_MODEL(sftp_win->file_list), &iter, NAME_COLUMN, &val);
    g_assert(G_VALUE_HOLDS_STRING(&val));
    const char *fname = g_value_get_string(&val);
    std::string full_remote_path = fname; // FIXME, path
    std::string full_local_path = fname; // FIXME, use file dialog.
    g_value_unset(&val);
    auto remote_file = sftp_open(sftp_win->sftp, full_remote_path.c_str(), O_RDONLY, 0);
    if(remote_file == nullptr) {
        printf("Could not open file: %s\n", ssh_get_error(sftp_win->session));
        return;
    }
    sftp_file_set_nonblocking(remote_file);
    sftp_win->local_file = g_open(full_local_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if(sftp_win->local_file < 0) {
        printf("Could not open local file.");
        return;
    }
    int async_request = sftp_async_read_begin(remote_file, SFTP_BUF_SIZE);
    sftp_win->remote_file = std::move(remote_file);
    sftp_win->async_request = async_request;
    sftp_win->downloading = true;
}

void open_sftp(SftpWindow &sftp_win) {
    sftp_win.dirname = ".";
    sftp_win.builder = gtk_builder_new_from_file(data_file_name("sftpwindow.glade").c_str());
    sftp_win.sftp_window = GTK_WINDOW(gtk_builder_get_object(sftp_win.builder, "sftp_window"));
    sftp_win.file_view = GTK_TREE_VIEW(gtk_builder_get_object(sftp_win.builder, "fileview"));
    sftp_win.file_list = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING);
    sftp_win.progress = GTK_PROGRESS_BAR(gtk_builder_get_object(sftp_win.builder, "transfer_progress"));

    gtk_tree_view_set_model(sftp_win.file_view, GTK_TREE_MODEL(sftp_win.file_list));
    gtk_tree_view_append_column(sftp_win.file_view,
                gtk_tree_view_column_new_with_attributes("Filename",
                gtk_cell_renderer_text_new(), "text", NAME_COLUMN, nullptr));
    g_signal_connect(GTK_WIDGET(sftp_win.file_view), "row-activated", G_CALLBACK(sftp_row_activated), &sftp_win);
    //gtk_widget_show_all(GTK_WIDGET(a.sftp_win.sftp_window));
}

gboolean async_uploader(gpointer data) {
    SftpWindow &sftp_win = *reinterpret_cast<SftpWindow*>(data);
    auto bytes_read = read(sftp_win.local_file, sftp_win.buf, SFTP_BUF_SIZE);
    ssize_t bytes_written = 0;
    //feed_terminal(a); // Otherwise libssh drains the socket and glib does not see the events.
    if(bytes_read < 0) {
        printf("Could not read from file.\n");
        goto cleanup;
    }
    if(bytes_read == 0) {
        // All of input file has been read. Time to stop.
        goto cleanup;
    }
    while(bytes_written < bytes_read) {
        auto current_written = sftp_write(sftp_win.remote_file, sftp_win.buf + bytes_written, bytes_read-bytes_written);
        if(current_written < 0) {
            printf("Writing failed: %s", ssh_get_error(sftp_win.session));
            goto cleanup;
        }
        bytes_written += current_written;
    }
    return G_SOURCE_CONTINUE;

cleanup:
    close(sftp_win.local_file);
    sftp_win.remote_file = SftpFile();
    return G_SOURCE_REMOVE;
}

void upload_file(SftpWindow &sftp_win, const char *fname) {
    int local_file = g_open(fname, O_RDONLY, 0);
    if(local_file < 0) {
        printf("Could not open file: %s\n", fname);
        return;
    }

    std::string remote_name = sftp_win.dirname + "/" + split_filename(fname);
    auto remote_file = sftp_open(sftp_win.sftp, remote_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if(remote_file == nullptr) {
        printf("Could not open remote file %s.\n", ssh_get_error(sftp_win.session));
        return;
    }
    sftp_win.remote_file = SftpFile(remote_file);
    sftp_win.local_file = local_file;
    g_idle_add(async_uploader, &sftp_win);
}
