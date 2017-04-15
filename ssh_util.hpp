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

#include<libssh/libssh.h>

class SshChannel;

class SshSession final {
private:
    ssh_session session;

public:

    SshSession();
    ~SshSession();

    SshSession(const SshSession &other) = delete;
    SshSession& operator=(const SshSession &other) = delete;

    SshSession(SshSession &&other) = default;
    SshSession& operator=(SshSession &&other) = default;

    SshChannel openShell();
    operator ssh_session() { return session; }
};

class SshChannel final {
private:
    ssh_session session;
    ssh_channel channel;

    void disconnect();

public:

    SshChannel() : session(nullptr), channel(nullptr) {}
    SshChannel(ssh_session session, ssh_channel channel);
    ~SshChannel();

    SshChannel(const SshChannel &other) = delete;
    SshChannel& operator=(const SshChannel &other) = delete;

    SshChannel(SshChannel &&other) = default;
    SshChannel& operator=(SshChannel &&other) {
        disconnect();
        session = other.session;
        channel = other.channel;
        other.session = nullptr;
        other.channel = nullptr;
        return *this;
    }

    operator ssh_channel() { return channel; }

    int read(char *buf, int bufsize);
    int write(char *buf, int bufsize);
};

