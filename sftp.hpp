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

#pragma once

static const constexpr int SFTP_BUF_SIZE = 4*1024;

#include<ssh_util.hpp>

#include<gtk/gtk.h>
#include<ssh_util.hpp>
#include<string>

struct SftpWindow {
    GtkBuilder *builder;
    GtkWindow *sftp_window;
    GtkTreeView *file_view;
    GtkListStore *file_list;
    GtkProgressBar *progress;
    ssh_session session; // A non-owning pointer.
    SftpSession sftp;
    SftpFile remote_file;
    int async_request;
    std::string dirname;
    int local_file;
    char buf[SFTP_BUF_SIZE];
    bool downloading;
};

void feed_sftp(SftpWindow &sftp_win);
void open_sftp(SftpWindow &sftp_win);
