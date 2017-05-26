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
#include<vector>
#include<algorithm>

struct DirEntry {
    std::string name;
    bool is_dir;
    uint64_t size;

    // Sort by directoryness, then by name.
    bool operator<(const DirEntry &other) const {
        if(&other == this) {
            return false;
        }
        if(is_dir && !other.is_dir) {
            return true;
        }
        if(other.is_dir && !is_dir) {
            return false;
        }
        if(name == ".") { // Each directory has only one of these.
            return true;
        }
        if(other.name == ".") {
            return false;
        }
        if(name == "..") {
            return true;
        }
        if(other.name == "..") {
            return false;
        }
        return name < other.name;
    }
};

enum SftpViewColumns {
    IS_DIR_COLUMN,
    NAME_COLUMN,
    SIZE_COLUMN,
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
    sftp_win.downloaded_bytes += bytes_read;
    gtk_progress_bar_set_fraction(sftp_win.progress, ((double)(sftp_win.downloaded_bytes)) / sftp_win.download_size);
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
    gtk_widget_set_sensitive(GTK_WIDGET(sftp_win.sftp_window), TRUE);
    gtk_progress_bar_set_fraction(sftp_win.progress, 0);
}

void load_sftp_dir_data(SftpWindow &s, const char *newdir) {
    s.dirname = newdir;
    gtk_list_store_clear(s.file_list);
    SftpDir dir = s.sftp.open_directory(newdir);
    SftpAttributes attribute;
    GtkTreeIter iter;
    std::vector<DirEntry> entries;
    while((attribute = sftp_readdir(s.sftp, dir))) {
        DirEntry e = {attribute->name, attribute->type == SSH_FILEXFER_TYPE_DIRECTORY, attribute->size};
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end());
    for(const auto &e : entries) {
        gtk_list_store_append(s.file_list, &iter);
        gtk_list_store_set(s.file_list, &iter,
                           IS_DIR_COLUMN, (gboolean) e.is_dir,
                           NAME_COLUMN, e.name.c_str(),
                           SIZE_COLUMN, e.size,
                          -1);
    }
}

std::string get_output_file_name(GtkWindow *parent_window, const std::string fname) {
    std::string result;
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Save File",
                                         parent_window,
                                         action,
                                         "_Cancel",
                                          GTK_RESPONSE_CANCEL,
                                         "_Save",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    chooser = GTK_FILE_CHOOSER (dialog);
    gtk_file_chooser_set_current_name(chooser, fname.c_str());
    gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);
    res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        result = filename;
        g_free (filename);
    }

    gtk_widget_destroy (dialog);

    return result;
}

void download_clicked(GtkButton *, gpointer data) {
    SftpWindow *sftp_win = reinterpret_cast<SftpWindow*>(data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(sftp_win->file_view));
    GtkTreeIter iter;
    if(!gtk_tree_selection_get_selected(sel, nullptr, &iter)) {
        // No entry is selected, bail out.
        return;
    }
    GValue val = G_VALUE_INIT;
    gtk_tree_model_get_value(GTK_TREE_MODEL(sftp_win->file_list), &iter, NAME_COLUMN, &val);
    g_assert(G_VALUE_HOLDS_STRING(&val));
    std::string fname(g_value_get_string(&val));
    g_value_unset(&val);
    gtk_tree_model_get_value(GTK_TREE_MODEL(sftp_win->file_list), &iter, IS_DIR_COLUMN, &val);
    bool is_dir = g_value_get_boolean(&val) == TRUE;
    g_value_unset(&val);
    gtk_tree_model_get_value(GTK_TREE_MODEL(sftp_win->file_list), &iter, SIZE_COLUMN, &val);
    sftp_win->download_size = g_value_get_uint64(&val);
    g_value_unset(&val);
    if(is_dir) {
        // FIXME download full directories.
        return;
    }
    std::string full_remote_path = fname; // FIXME, path
    std::string full_local_path = get_output_file_name(sftp_win->sftp_window, fname);
    if(full_local_path.empty()) {
        return;
    }
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
    sftp_win->downloaded_bytes = 0;
    gtk_progress_bar_set_fraction(sftp_win->progress, 0);
    gtk_widget_set_sensitive(GTK_WIDGET(sftp_win->sftp_window), FALSE);
}

void upload_clicked(GtkButton *, gpointer data) {
    SftpWindow *sftp_win = reinterpret_cast<SftpWindow*>(data);
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
    load_sftp_dir_data(sftp_win, ".");
}

void build_sftp_win(SftpWindow &sftp_win) {
    sftp_win.dirname = ".";
    sftp_win.builder = gtk_builder_new_from_file(data_file_name("sftpwindow.glade").c_str());
    sftp_win.sftp_window = GTK_WINDOW(gtk_builder_get_object(sftp_win.builder, "sftp_window"));
    sftp_win.file_view = GTK_TREE_VIEW(gtk_builder_get_object(sftp_win.builder, "fileview"));
    sftp_win.file_list = gtk_list_store_new(N_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT64);
    sftp_win.download_button = GTK_BUTTON(gtk_builder_get_object(sftp_win.builder, "download_button"));
    sftp_win.upload_button = GTK_BUTTON(gtk_builder_get_object(sftp_win.builder, "upload_button"));
    sftp_win.progress = GTK_PROGRESS_BAR(gtk_builder_get_object(sftp_win.builder, "transfer_progress"));

    gtk_tree_view_set_model(sftp_win.file_view, GTK_TREE_MODEL(sftp_win.file_list));
    gtk_tree_view_append_column(sftp_win.file_view,
                gtk_tree_view_column_new_with_attributes("Filename",
                gtk_cell_renderer_text_new(), "text", NAME_COLUMN, nullptr));
    gtk_tree_view_append_column(sftp_win.file_view,
                gtk_tree_view_column_new_with_attributes("Size",
                gtk_cell_renderer_text_new(), "text", SIZE_COLUMN, nullptr));
    g_signal_connect(GTK_WIDGET(sftp_win.download_button), "clicked", G_CALLBACK(download_clicked), &sftp_win);
    g_signal_connect(GTK_WIDGET(sftp_win.upload_button), "clicked", G_CALLBACK(upload_clicked), &sftp_win);
    g_signal_connect(GTK_WIDGET(sftp_win.file_view), "row-activated", G_CALLBACK(sftp_row_activated), &sftp_win);
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
