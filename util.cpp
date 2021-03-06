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

#include<util.hpp>
#include<string>

#include<sys/select.h>

// FIXME, should look up PREFIX/share/whatever, envvar override
// and build dir. Currently hardcodes running from build dir.
std::string data_file_name(const char *basename) {
    return std::string("../sshthingy/") + basename;
}

std::string split_filename(const char *fname) {
    std::string f(fname);
    auto slash_loc = f.rfind('/');
    if(slash_loc == std::string::npos) {
        return f;
    }
    return f.substr(slash_loc+1, std::string::npos);
}



bool fd_has_data(int fd) {
    fd_set rfds;
    struct timeval tv;
    int retval;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    retval = select(1, &rfds, NULL, NULL, &tv);

    return retval > 0;
}
