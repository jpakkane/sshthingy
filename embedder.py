#!/usr/bin/env python3

# Copyright (C) 2017 Jussi Pakkanen.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of version 3, or (at your option) any later version,
# of the GNU General Public License as published
# by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

'''This file converts a binary file into a const array source
file that you can embed in your programs.
'''

import sys
import os

def process_(cfile, hfile, inputs):
    hfile.write('#pragma once\n')
    cfile.write('#include<%s>\n' % os.path.split(hfile.name)[1])
    for input in inputs:
        contents = open(input, 'rb').read()
        inputbase = os.path.splitext(os.path.split(input)[1])[0]
        hfile.write('extern const unsigned char %s[%d];\n' % (inputbase, len(contents)))
        cfile.write('const unsigned char %s[%d] = {\n' % (inputbase, len(contents)))
        for i, c in enumerate(contents):
            if i % 16 == 0:
                cfile.write('\n    ');
            cfile.write("{:>3}, ".format(c))
        cfile.write('};\n')

def process(cfilename, hfilename, inputs):
    with open(cfilename, 'w') as cfile:
        with open(hfilename, 'w') as hfile:
            process_(cfile, hfile, inputs)

if __name__ == '__main__':
    cfile = sys.argv[1]
    hfile = sys.argv[2]
    inputs = sys.argv[3:]

    process(cfile, hfile, inputs)
