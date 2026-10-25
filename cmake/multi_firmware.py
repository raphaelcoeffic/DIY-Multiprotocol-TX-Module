#!/usr/bin/env python3
"""After the link: the size check and the binary signature.

The size is computed and checked as by the Arduino IDE (recipe.size.regex of
the board package), the signature is appended as by the board package's
do_version script: 'multi-x<flags>-<version>', which the radios and the
flashing tools read to identify the firmware. A copy is made, by default
multi-<type>-<version>.bin next to the binary (--copy: the file name, with
{type} and {version}).

Usage: multi_firmware.py --elf FILE --size-tool SIZE --max-size N --max-data N
                         --bin FILE --preproc FILE --header Multiprotocol.h
                         --board MULTI_STM32_WITH_BOOT=131 [--copy FILE]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

# do_version: the board (build.board) gives the type and the boot loader support
BOARDS = {
    'MULTI_NO_BOOT': ('avr', '00', '0'),
    'MULTI_FLASH_FROM_TX': ('avr', '00', '1'),
    'MULTI_STM32_NO_BOOT': ('stm', '01', '0'),
    'MULTI_STM32_WITH_BOOT': ('stm', '01', '1'),
    'MULTI_ORANGERX': ('orx', '10', '0'),
}

CHANNEL_ORDERS = ['AETR', 'AERT', 'ARET', 'ARTE', 'ATRE', 'ATER', 'EATR', 'EART',
                  'ERAT', 'ERTA', 'ETRA', 'ETAR', 'TEAR', 'TERA', 'TREA', 'TRAE',
                  'TARE', 'TAER', 'RETA', 'REAT', 'RAET', 'RATE', 'RTAE', 'RTEA']


def size_check(elf, size_tool, max_size, max_data):
    out = subprocess.run([size_tool, '-A', elf], check=True,
                         stdout=subprocess.PIPE, universal_newlines=True).stdout
    size = sum(int(m.group(1)) for m in re.finditer(
        r'^(?:\.text|\.data|\.rodata|\.text.align|\.ARM.exidx)\s+([0-9]+).*', out, re.M))
    data = sum(int(m.group(1)) for m in re.finditer(
        r'^(?:\.data|\.bss|\.noinit)\s+([0-9]+).*', out, re.M))
    print('Firmware uses %d bytes (%d%%) of program storage space. Maximum is %d bytes.'
          % (size, size * 100 // max_size, max_size))
    print('Global variables use %d bytes (%d%%) of dynamic memory, leaving %d bytes for '
          'local variables. Maximum is %d bytes.'
          % (data, data * 100 // max_data, max_data - data, max_data))
    ok = True
    if size > max_size:
        print('error: firmware too big, %d bytes over' % (size - max_size), file=sys.stderr)
        ok = False
    if data > max_data:
        print('error: not enough memory, %d bytes over' % (data - max_data), file=sys.stderr)
        ok = False
    return ok


def version(header):
    text = open(header, encoding='utf-8', errors='replace').read()

    def field(name):
        m = re.search(r'^\s*#define\s+%s\s+(\d+)' % name, text, re.M)
        if not m:
            sys.exit('%s: no %s' % (header, name))
        return int(m.group(1))
    return [field('VERSION_' + n) for n in ('MAJOR', 'MINOR', 'REVISION', 'PATCH_LEVEL')]


def signature(preproc, board, ver):
    """The signature of do_version, from the preprocessed sketch."""
    text = open(preproc, encoding='utf-8', errors='replace').read()
    multi_type, sig_type, boot_support = BOARDS[board.split('=')[0]]

    def flag(name):
        return '1' if 'firmwareFlag_' + name in text else '0'

    order = '11111'
    m = re.search(r' firmwareFlag_ChannelOrder_([AETR]{4}) ', text)
    if m and m.group(1) in CHANNEL_ORDERS:
        order = format(CHANNEL_ORDERS.index(m.group(1)), '05b')

    sub_type = '000'
    if sig_type == '01':
        if flag('MCU_STM32F103C8') == '1':
            sub_type = '001'
        elif flag('MCU_STM32F103CB') == '1' and flag('MULTI_5IN1_INTERNAL') == '1':
            sub_type = '010'

    bits = (sub_type + flag('DEBUG_SERIAL') + flag('MULTI_TELEMETRY') + flag('MULTI_STATUS')
            + flag('INVERT_TELEMETRY') + flag('CHECK_FOR_BOOTLOADER') + boot_support
            + order + sig_type)
    return multi_type, 'multi-x%08x-%02d%02d%02d%02d' % ((int(bits, 2),) + tuple(ver))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--size-tool', required=True)
    ap.add_argument('--max-size', type=int, required=True)
    ap.add_argument('--max-data', type=int, required=True)
    ap.add_argument('--bin', required=True)
    ap.add_argument('--preproc', required=True)
    ap.add_argument('--header', required=True)
    ap.add_argument('--board', required=True)
    ap.add_argument('--copy')
    args = ap.parse_args()

    if not size_check(args.elf, args.size_tool, args.max_size, args.max_data):
        os.remove(args.bin)
        sys.exit(1)

    ver = version(args.header)
    multi_type, sig = signature(args.preproc, args.board, ver)
    with open(args.bin, 'rb') as f:
        data = f.read()
    if sig.encode() not in data:
        with open(args.bin, 'ab') as f:
            f.write(sig.encode())
    print('Signature: %s' % sig)
    copy = args.copy or os.path.join(os.path.dirname(args.bin), 'multi-{type}-{version}.bin')
    copy = copy.format(type=multi_type, version='.'.join(str(v) for v in ver))
    os.makedirs(os.path.dirname(os.path.abspath(copy)), exist_ok=True)
    shutil.copyfile(args.bin, copy)
    print('Firmware: %s' % copy)


if __name__ == '__main__':
    main()
