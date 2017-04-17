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
#include<cstdio>

SshSession::SshSession() : session(ssh_new()) {
    int never = 0; // SSH v1 shall never be supported.
    ssh_options_set(session, SSH_OPTIONS_SSH1, &never);
}

SshSession::~SshSession() {
    ssh_free(session);
}

SshChannel SshSession::open_shell() {
    SshChannel channel(session, ssh_channel_new(session));
    if(channel == nullptr) {
        printf("Could not open channel: %s\n", ssh_get_error(session));
        return channel;
    }
    auto rc = ssh_channel_open_session(channel);
    if(rc != SSH_OK) {
        printf("Could not open session: %s\n", ssh_get_error(session));
        return channel;
    }
    rc = ssh_channel_request_pty(channel);
    if(rc != SSH_OK) {
        printf("Could not get pty: %s\n", ssh_get_error(session));
        return channel;
    }
    rc = ssh_channel_change_pty_size(channel, 80, 25);
    if(rc != SSH_OK) {
        printf("Could not get change pty size: %s\n", ssh_get_error(session));
        return channel;
    }
    rc = ssh_channel_request_shell(channel);
    if(rc != SSH_OK) {
        printf("Could not get create shell: %s\n", ssh_get_error(session));
        return channel;
    }
    return channel;
}

SftpSession SshSession::open_sftp_session() {
    SftpSession s(session, sftp_new(session));
    if(s == nullptr) {
        printf("Could not open sftp connection: %s\n", ssh_get_error(session));
        return s;
    }
    auto rc = sftp_init(s);
    if(rc != SSH_OK) {
        printf("Could not init sftp connection: %s\n", ssh_get_error(session));
        return s;
    }
    return s;
}

SftpDir SftpSession::open_directory(const char *dirpath) {
    SftpDir fdir = sftp_opendir(ftp_session, dirpath);
    if(fdir == nullptr) {
        printf("Could not open sftp directory: %s\n", ssh_get_error(session));
    }
    return fdir;
}

SshChannel::SshChannel(ssh_session session, ssh_channel channel) : session(session), channel(channel) {
}

SshChannel::~SshChannel() {
    disconnect();
}

void SshChannel::disconnect() {
    if(channel) {
        ssh_channel_close(channel);
        ssh_channel_send_eof(channel);
        ssh_channel_free(channel);
    }
}

int SshChannel::read(char *buf, int bufsize) {
    return ssh_channel_read_nonblocking(channel, buf, bufsize, 0);
}


int SshChannel::write(char *buf, int bufsize) {
    return ssh_channel_write(channel, buf, bufsize);
}

SftpSession::SftpSession(ssh_session session, sftp_session ftp_session) : session(session), ftp_session(ftp_session) {
}

SftpSession::~SftpSession() {
    disconnect();
}

void SftpSession::disconnect() {
    if(ftp_session) {
        sftp_free(ftp_session);
        ftp_session = nullptr;
    }
}
