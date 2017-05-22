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

#include<gtk/gtk.h>

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

void build_port_gui(PortForwardings &pf);

