#!/usr/bin/env python3
"""Runs the link of a firmware (RULE_LAUNCH_LINK): when the firmware does not
fit, the linker's error is followed by what to do.

Usage: multi_link.py LINKER ARGS...
"""

import re
import subprocess
import sys

from multi_firmware import FEWER

MEMORIES = {'rom': 'flash', 'ram': 'RAM'}


def main():
    p = subprocess.run(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode(errors='replace')
    sys.stdout.write(out)
    sys.stdout.flush()
    for region, n in re.findall(r"region [`']?(\w+)'? overflowed by (\d+) bytes", out):
        print('error: firmware too big for the %s, at least %s bytes over: %s'
              % (MEMORIES.get(region, region), n, FEWER), file=sys.stderr)
    sys.exit(p.returncode)


if __name__ == '__main__':
    main()
