#!/usr/bin/env python3
"""
Drive a SiMPLE OS QEMU instance through the monitor socket:
send keystrokes (sendkey) and take screendumps.

Usage:
  qemu_drive.py start            — boot QEMU in the background
  qemu_drive.py type 'text'      — type text (maps chars to sendkey names)
  qemu_drive.py key  <keyname>   — send one raw qemu key (ret, esc, ...)
  qemu_drive.py shot <out.png>   — screendump + convert to png
  qemu_drive.py stop             — kill QEMU
"""
import os, socket, subprocess, sys, time

IMG    = os.path.join(os.path.dirname(__file__), '..', 'simple.img')
MONSOCK = '/tmp/simple_qmon.sock'
PIDFILE = '/tmp/simple_qemu.pid'
SERIAL  = '/tmp/simple_serial.log'

KEYMAP = {
    ' ': 'spc', '.': 'dot', ',': 'comma', '/': 'slash', '-': 'minus',
    '=': 'equal', ';': 'semicolon', "'": 'apostrophe', '[': 'bracket_left',
    ']': 'bracket_right', '\\': 'backslash', '`': 'grave_accent',
    '\n': 'ret', '\t': 'tab',
}
SHIFTMAP = {
    ':': 'semicolon', '!': '1', '@': '2', '#': '3', '$': '4', '%': '5',
    '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': 'minus',
    '+': 'equal', '{': 'bracket_left', '}': 'bracket_right', '|': 'backslash',
    '"': 'apostrophe', '<': 'comma', '>': 'dot', '?': 'slash', '~': 'grave_accent',
}

def mon(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(MONSOCK)
    s.settimeout(2)
    try:
        s.recv(4096)             # banner
    except socket.timeout:
        pass
    s.sendall((cmd + '\n').encode())
    time.sleep(0.15)
    out = b''
    try:
        while True:
            b_ = s.recv(4096)
            if not b_:
                break
            out += b_
    except socket.timeout:
        pass
    s.close()
    return out.decode(errors='replace')

def start():
    if os.path.exists(MONSOCK):
        os.unlink(MONSOCK)
    p = subprocess.Popen([
        'qemu-system-x86_64',
        '-drive', f'format=raw,file={IMG}',
        '-serial', f'file:{SERIAL}',
        '-monitor', f'unix:{MONSOCK},server,nowait',
        '-display', 'none',
        '-no-reboot', '-no-shutdown',
    ])
    open(PIDFILE, 'w').write(str(p.pid))
    # wait for the monitor socket
    for _ in range(100):
        if os.path.exists(MONSOCK):
            break
        time.sleep(0.1)
    print(f'qemu pid {p.pid}')

def send_char(c):
    if c.isupper():
        mon(f'sendkey shift-{c.lower()}')
    elif c in SHIFTMAP:
        mon(f'sendkey shift-{SHIFTMAP[c]}')
    elif c in KEYMAP:
        mon(f'sendkey {KEYMAP[c]}')
    else:
        mon(f'sendkey {c}')
    time.sleep(0.06)

def type_text(text):
    for c in text:
        send_char(c)

def shot(out_png):
    ppm = '/tmp/simple_shot.ppm'
    mon(f'screendump {ppm}')
    time.sleep(0.4)
    subprocess.run(['convert', ppm, out_png], check=True)
    print(out_png)

def stop():
    try:
        pid = int(open(PIDFILE).read())
        os.kill(pid, 15)
    except Exception as e:
        print(e)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'start':
        start()
    elif cmd == 'type':
        type_text(sys.argv[2])
    elif cmd == 'key':
        mon(f'sendkey {sys.argv[2]}')
    elif cmd == 'shot':
        shot(sys.argv[2])
    elif cmd == 'stop':
        stop()
