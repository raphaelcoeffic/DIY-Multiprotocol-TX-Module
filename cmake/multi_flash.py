#!/usr/bin/env python3
"""Flash the STM32 firmware, as the Arduino IDE uploads it.

The upload tools of the board package (tools/<os>/auto_upload, maple_upload,
serial_upload, serial_upload_inc_bootloader), with stm32flash and dfu-util
from the PATH:

- serial: through the module's USB-serial adapter, with stm32flash: erases
  the flash, writes the boot loader at 0x8000000 and the firmware at
  0x8002000;
- serial-noboot: the same without the boot loader, the firmware at
  0x8000000 (the serial debug builds);
- usb: with the Maple (STM32duino) USB boot loader and dfu-util; a running
  firmware with USB support is first reset to the boot loader through its
  serial port;
- auto: usb if a Maple USB device (1eaf:xxxx) is connected, else serial.

The serial port is --port, or the MULTI_FLASH_PORT environment variable, or
the only USB serial port found.

Usage: multi_flash.py --bin FILE --method METHOD --bootloader FILE
                      --erase-len N [--port PORT] [--dry-run]
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

MAPLE_VID = 0x1eaf
MAPLE_DFU = '1eaf:0003'


def fail(msg):
    sys.exit('multi_flash: ' + msg)


def tool(name, path):
    found = path or shutil.which(name)
    if not found:
        fail('%s not found: install it (Homebrew, apt: %s) or set its path in CMake' % (name, name))
    return found


def serial_ports():
    if sys.platform == 'darwin':
        patterns = ['/dev/cu.usbserial*', '/dev/cu.wchusbserial*', '/dev/cu.usbmodem*',
                    '/dev/cu.SLAB_USBtoUART*']
    elif sys.platform.startswith('linux'):
        patterns = ['/dev/ttyUSB*', '/dev/ttyACM*']
    elif sys.platform == 'win32':
        return windows_serial_ports()
    else:
        patterns = []
    return sorted(set(p for pattern in patterns for p in glob.glob(pattern)))


def windows_serial_ports():
    """The COM ports of USB devices (not the motherboard's \\Device\\Serial<n>)."""
    import winreg
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DEVICEMAP\SERIALCOMM')
    except OSError:
        return []
    ports = []
    i = 0
    while True:
        try:
            device, port, _ = winreg.EnumValue(key, i)
        except OSError:
            break
        if not device.startswith('\\Device\\Serial'):
            ports.append(port)
        i += 1
    return sorted(ports)


def find_port(port, required=True):
    port = os.environ.get('MULTI_FLASH_PORT') or port
    if port:
        return port
    ports = serial_ports()
    if len(ports) == 1:
        return ports[0]
    if not required:
        return None
    if not ports:
        fail('no serial port found: connect the module or set MULTI_FLASH_PORT')
    fail('several serial ports, set MULTI_FLASH_PORT to one of: ' + ', '.join(ports))


def maple_connected(dfu_util):
    """A Maple USB device, in the boot loader (DFU) or running (serial)."""
    if sys.platform.startswith('linux'):
        for path in glob.glob('/sys/bus/usb/devices/*/idVendor'):
            with open(path) as f:
                if int(f.read().strip(), 16) == MAPLE_VID:
                    return True
        return False
    if sys.platform == 'darwin':
        out = subprocess.run(['ioreg', '-p', 'IOUSB', '-l', '-w0'], stdout=subprocess.PIPE,
                             universal_newlines=True).stdout
        return '"idVendor" = %d' % MAPLE_VID in out
    if dfu_util:
        out = subprocess.run([dfu_util, '-l'], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, universal_newlines=True).stdout
        return MAPLE_DFU in out
    return False


def run(cmd, dry_run):
    print(' '.join(cmd), flush=True)
    if not dry_run:
        subprocess.run(cmd, check=True)


def maple_reset(port, dry_run):
    """upload-reset: DTR/RTS toggles and '1EAF', the running firmware reboots
    to the boot loader."""
    print('reset %s to the boot loader' % port, flush=True)
    if dry_run:
        return
    import fcntl
    import termios
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attr = termios.tcgetattr(fd)
        old = list(attr)
        attr[2] |= termios.CRTSCTS | termios.CLOCAL
        attr[1] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attr)

        def lines(rts, dtr):
            status = int.from_bytes(fcntl.ioctl(fd, termios.TIOCMGET, b'\0\0\0\0'), sys.byteorder)
            status = (status | termios.TIOCM_RTS) if rts else (status & ~termios.TIOCM_RTS)
            status = (status | termios.TIOCM_DTR) if dtr else (status & ~termios.TIOCM_DTR)
            fcntl.ioctl(fd, termios.TIOCMSET, status.to_bytes(4, sys.byteorder))

        lines(False, False)
        lines(False, True)
        time.sleep(0.05)
        lines(False, False)
        lines(True, False)
        lines(True, True)
        time.sleep(0.05)
        lines(True, False)
        time.sleep(0.05)
        os.write(fd, b'1EAF')
        termios.tcsetattr(fd, termios.TCSANOW, old)
    finally:
        os.close(fd)
    time.sleep(0.75 + 0.5)


def flash_usb(args, dfu_util):
    port = find_port(args.port, required=False)
    if port and os.path.exists(port):
        maple_reset(port, args.dry_run)
    run([dfu_util, '-d', MAPLE_DFU, '-a', '2', '-D', args.bin, '-R'], args.dry_run)


def flash_serial(args, stm32flash, bootloader):
    port = find_port(args.port)
    run([stm32flash, '-o', '-S', '0x8000000:%d' % args.erase_len, '-b', '115200', port],
        args.dry_run)
    if bootloader:
        run([stm32flash, '-v', '-e', '0', '-g', '0x8000000', '-b', '115200',
             '-w', args.bootloader, port], args.dry_run)
        run([stm32flash, '-v', '-e', '0', '-s', '8', '-g', '0x8002000', '-b', '115200',
             '-w', args.bin, port], args.dry_run)
    else:
        run([stm32flash, '-v', '-e', '0', '-g', '0x8000000', '-b', '115200',
             '-w', args.bin, port], args.dry_run)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True)
    ap.add_argument('--method', required=True, choices=['auto', 'usb', 'serial', 'serial-noboot'])
    ap.add_argument('--bootloader', required=True)
    ap.add_argument('--erase-len', type=int, required=True)
    ap.add_argument('--port', default='')
    ap.add_argument('--stm32flash', default='')
    ap.add_argument('--dfu-util', default='')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    method = args.method
    if method == 'auto':
        method = 'usb' if maple_connected(args.dfu_util or shutil.which('dfu-util')) else 'serial'
        print('%s upload' % ('Maple device found: USB' if method == 'usb' else
                             'No Maple device: serial'), flush=True)
    try:
        if method == 'usb':
            flash_usb(args, tool('dfu-util', args.dfu_util))
        else:
            flash_serial(args, tool('stm32flash', args.stm32flash), method == 'serial')
    except subprocess.CalledProcessError as e:
        fail('%s failed (%d)' % (e.cmd[0], e.returncode))


if __name__ == '__main__':
    main()
