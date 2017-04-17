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
#include<libssh/sftp.h>

class SshChannel;
class SftpSession;
class SftpDir;

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

    SshChannel open_shell();
    operator ssh_session() { return session; }

    SftpSession open_sftp_session();
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

class SftpSession final {
private:
    ssh_session session;
    sftp_session ftp_session;

    void disconnect();

public:

    SftpSession() : session(nullptr), ftp_session(nullptr) {}
    SftpSession(ssh_session session, sftp_session ftp_session);
    ~SftpSession();

    SftpSession(const SftpSession &other) = delete;
    SftpSession& operator=(const SftpSession &other) = delete;

    SftpSession(SftpSession &&other) = default;
    SftpSession& operator=(SftpSession &&other) {
        disconnect();
        session = other.session;
        ftp_session = other.ftp_session;
        other.session = nullptr;
        other.ftp_session = nullptr;
        return *this;
    }

    SftpDir open_directory(const char *dirpath);

    operator sftp_session() { return ftp_session; }
};

class SftpDir final {
private:
    sftp_dir dir;

    void disconnect() { if(dir) { sftp_closedir(dir); dir=nullptr; } }

public:

    SftpDir() : dir(nullptr) {}
    SftpDir(sftp_dir dir) : dir(dir) {}
    ~SftpDir() { disconnect(); }

    SftpDir(const SftpDir &other) = delete;
    SftpDir& operator=(const SftpDir &other) = delete;

    SftpDir(SftpDir &&other) = default;
    SftpDir& operator=(SftpDir &&other) {
        disconnect();
        dir = other.dir;
        other.dir = nullptr;
        return *this;
    }

    operator sftp_dir() { return dir; }
};


class SftpFile final {
private:
    sftp_file file;

    void disconnect() { if(file) { sftp_close(file); file=nullptr; } }

public:

    SftpFile() : file(nullptr) {}
    SftpFile(sftp_file file) : file(file) {}
    ~SftpFile() { disconnect(); }

    SftpFile(const SftpFile &other) = delete;
    SftpFile& operator=(const SftpFile &other) = delete;

    SftpFile(SftpFile &&other) = default;
    SftpFile& operator=(SftpFile &&other) {
        disconnect();
        file = other.file;
        other.file = nullptr;
        return *this;
    }

    operator sftp_file() { return file; }
};


class SftpAttributes final {
private:
    sftp_attributes attributes;

public:

    SftpAttributes() : attributes(nullptr) {}
    SftpAttributes(sftp_attributes attributes) : attributes(attributes) {}
    ~SftpAttributes() {}

    SftpAttributes(const SftpAttributes &other) = delete;
    SftpAttributes& operator=(const SftpAttributes &other) = delete;

    SftpAttributes(SftpAttributes &&other) = default;
    SftpAttributes& operator=(SftpAttributes &&other) {
        attributes = other.attributes;
        other.attributes = nullptr;
        return *this;
    }

    operator sftp_attributes() { return attributes; }
    sftp_attributes operator ->() { return attributes; }
};
