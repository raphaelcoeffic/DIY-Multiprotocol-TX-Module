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
  serial port (the Maple one, 1eaf:xxxx, or the one given);
- auto: usb if a Maple USB device (1eaf:xxxx) is connected, else serial;
- start: nothing written, a module waiting in its USB boot loader starts its
  firmware. The boot loader stays there at power up when it sees a USB host
  (D-, PA11, low): plugged into a computer, a module only flashes. Its DFU
  code (STM32duino-bootloader dfu.c) sets the "just uploaded" boot flag at
  the first DFU_DNLOAD with data and copies that data to flash only at the
  next DFU_GETSTATUS; a USB reset in between resets the chip, which then
  starts the firmware at once. Needs libusb-1.0 (dfu-util's).

The serial port is --port, or the MULTI_FLASH_PORT environment variable, or
the only USB serial port found. The serial ports are pyserial's
(lib/pyserial, vendored): Python alone runs this script.

Usage: multi_flash.py --bin FILE --method METHOD --bootloader FILE
                      --erase-len N [--port PORT] [--dry-run]
       multi_flash.py --method start
"""

import argparse
import ctypes
import ctypes.util
import os
import shutil
import subprocess
import sys
import time
from typing import NoReturn

# pyserial, vendored (lib/pyserial)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'lib', 'pyserial'))
import serial  # noqa: E402
from serial.tools import list_ports  # noqa: E402

MAPLE_VID = 0x1eaf
MAPLE_DFU = '1eaf:0003'
MAPLE_DFU_PID = 0x0003
DFU_DNLOAD = 1


def fail(msg) -> NoReturn:
    sys.exit('multi_flash: ' + msg)


def tool(name, path):
    found = path or shutil.which(name)
    if not found:
        fail('%s not found: install it (Homebrew, apt: %s) or set its path in CMake' % (name, name))
    return found


def serial_ports():
    """The USB serial ports (not the motherboard's)."""
    return sorted(p.device for p in list_ports.comports() if p.vid is not None)


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


def maple_ports():
    """The serial ports of a running firmware with USB (Maple)."""
    return sorted(p.device for p in list_ports.comports() if p.vid == MAPLE_VID)


def maple_connected(dfu_util):
    """A Maple USB device: running (serial) or in the boot loader (DFU)."""
    if maple_ports():
        return True
    if dfu_util:
        out = subprocess.run([dfu_util, '-l'], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, universal_newlines=True).stdout
        return MAPLE_DFU in out
    return False


def run(cmd, dry_run):
    print(' '.join(cmd), flush=True)
    if not dry_run:
        subprocess.run(cmd, check=True)


def run_dfu(cmd, dry_run):
    """dfu-util, its output shown as it comes. dfu-util 0.11 (Homebrew, macOS)
    exits with 74 after the final USB reset (-R) although the download is
    done and the firmware starts: the download decides."""
    print(' '.join(cmd), flush=True)
    if dry_run:
        return
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert p.stdout
    out = b''
    while True:
        chunk = os.read(p.stdout.fileno(), 256)
        if not chunk:
            break
        out += chunk
        sys.stdout.buffer.write(chunk)
        sys.stdout.flush()
    if p.wait() != 0 and b'Download done.' not in out:
        raise subprocess.CalledProcessError(p.returncode, cmd)


def maple_reset(port, dry_run):
    """upload-reset: DTR/RTS toggles and '1EAF', the running firmware reboots
    to the boot loader."""
    print('reset %s to the boot loader' % port, flush=True)
    if dry_run:
        return
    s = serial.Serial()
    s.port = port
    s.rts = s.dtr = False  # from the start, not raised by open()
    try:
        s.open()
    except serial.SerialException as e:
        fail('cannot open %s: %s' % (port, e))
    with s:
        def lines(rts, dtr):
            s.rts = rts
            s.dtr = dtr

        lines(False, False)
        lines(False, True)
        time.sleep(0.05)
        lines(False, False)
        lines(True, False)
        lines(True, True)
        time.sleep(0.05)
        lines(True, False)
        time.sleep(0.05)
        s.write(b'1EAF')
    time.sleep(0.75 + 0.5)


def flash_usb(args, dfu_util):
    # A running firmware first goes to the boot loader
    ports = maple_ports()
    port = os.environ.get('MULTI_FLASH_PORT') or args.port or (ports[0] if ports else None)
    if port:
        maple_reset(port, args.dry_run)
    run_dfu([dfu_util, '-d', MAPLE_DFU, '-a', '2', '-D', args.bin, '-R'], args.dry_run)


def libusb(dfu_util):
    """libusb-1.0: the system's, Homebrew's, or the one next to dfu-util
    (Windows builds of dfu-util come with libusb-1.0.dll)."""
    names = [ctypes.util.find_library('usb-1.0'), 'libusb-1.0.so.0',
             '/opt/homebrew/lib/libusb-1.0.dylib', '/usr/local/lib/libusb-1.0.dylib']
    if dfu_util:
        names.append(os.path.join(os.path.dirname(dfu_util), 'libusb-1.0.dll'))
    lib = None
    for name in filter(None, names):
        try:
            lib = ctypes.CDLL(name)
            break
        except OSError:
            pass
    if lib is None:
        fail('libusb-1.0 not found: install it (Homebrew, apt: libusb-1.0-0), dfu-util uses it too')
    vp, i = ctypes.c_void_p, ctypes.c_int
    lib.libusb_init.argtypes = [ctypes.POINTER(vp)]
    lib.libusb_exit.argtypes = [vp]
    lib.libusb_open_device_with_vid_pid.argtypes = [vp, ctypes.c_uint16, ctypes.c_uint16]
    lib.libusb_open_device_with_vid_pid.restype = vp
    lib.libusb_claim_interface.argtypes = [vp, i]
    lib.libusb_set_interface_alt_setting.argtypes = [vp, i, i]
    lib.libusb_control_transfer.argtypes = [vp, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint16,
                                            ctypes.c_uint16, ctypes.c_char_p, ctypes.c_uint16,
                                            ctypes.c_uint]
    lib.libusb_reset_device.argtypes = [vp]
    lib.libusb_close.argtypes = [vp]
    lib.libusb_error_name.argtypes = [i]
    lib.libusb_error_name.restype = ctypes.c_char_p
    return lib


def maple_start(dfu_util, dry_run):
    """The firmware of a module in its USB boot loader starts, nothing
    written: DFU_DNLOAD of one byte on alt 2 (the boot flag is set, the byte
    stays in RAM), then a USB reset (the chip resets). The reset goes out
    even if the boot loader refuses the DNLOAD (not idle)."""
    print('start the firmware of the module in its boot loader (%s)' % MAPLE_DFU, flush=True)
    if dry_run:
        return
    lib = libusb(dfu_util)
    ctx = ctypes.c_void_p()
    if lib.libusb_init(ctypes.byref(ctx)) < 0:
        fail('libusb_init failed')
    try:
        h = lib.libusb_open_device_with_vid_pid(ctx, MAPLE_VID, MAPLE_DFU_PID)
        if not h:
            fail('no module in its USB boot loader (%s): connect it, or it runs already' % MAPLE_DFU)
        r = lib.libusb_claim_interface(h, 0)
        if r >= 0:
            r = lib.libusb_set_interface_alt_setting(h, 0, 2)
        if r >= 0:
            lib.libusb_control_transfer(h, 0x21, DFU_DNLOAD, 0, 0, b'\xff', 1, 1000)
        if r < 0:
            lib.libusb_close(h)
            fail('cannot open the boot loader: %s' % lib.libusb_error_name(r).decode())
        lib.libusb_reset_device(h)  # the device goes away: an error is expected
        lib.libusb_close(h)
        # A boot loader that stayed comes back within a second
        time.sleep(2.5)
        h = lib.libusb_open_device_with_vid_pid(ctx, MAPLE_VID, MAPLE_DFU_PID)
        if h:
            lib.libusb_close(h)
            fail('the module is still in its boot loader')
        print('firmware started', flush=True)
    finally:
        lib.libusb_exit(ctx)


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
    ap.add_argument('--bin')
    ap.add_argument('--method', required=True,
                    choices=['auto', 'usb', 'serial', 'serial-noboot', 'start'])
    ap.add_argument('--bootloader')
    ap.add_argument('--erase-len', type=int)
    ap.add_argument('--port', default='')
    ap.add_argument('--stm32flash', default='')
    ap.add_argument('--dfu-util', default='')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    method = args.method
    if method == 'start':
        maple_start(args.dfu_util or shutil.which('dfu-util'), args.dry_run)
        return
    if not (args.bin and args.bootloader and args.erase_len):
        ap.error('--bin, --bootloader and --erase-len are needed to flash')
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
