#!/usr/bin/env python3
# MHFZ Plus for Linux. MHFZ Plus itself is Windows software (the launcher, the frame-rate mod with DXVK, the settings
# app); on Linux it runs inside the Wine prefix the game already runs in: Steam (Proton), Lutris, Bottles, Heroic or
# plain Wine. This script puts it there, starts it, puts the original game files back and removes it again. It needs
# only Python 3's standard library, and installing or removing never starts Wine.
#   sh install.sh                       setup: a window (zenity) on a desktop, otherwise questions in the terminal
#   mhfz-plus app [meze|rain]           the MHFZ Plus app: play, settings, help (what the menu entry runs)
#   mhfz-plus play meze|rain            start the game through MHFZ Plus directly (Steam's Play entries)
#   mhfz-plus folder                    the MHFZ Plus folder (settings and logs) in the file manager
#   mhfz-plus restore                   original game files back, frame-rate mod out
#   mhfz-plus uninstall [--yes]         remove MHFZ Plus from every prefix it was installed in
# (mhfz-plus = ~/.local/share/mhfz-plus/mhfz-plus once installed; sh uninstall.sh in the package does the same)
import argparse
import hashlib
import json
import os
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
import time
import urllib.parse
import zlib

HOME = os.path.expanduser('~')
XDG_DATA = os.environ.get('XDG_DATA_HOME') or os.path.join(HOME, '.local', 'share')
XDG_CONFIG = os.environ.get('XDG_CONFIG_HOME') or os.path.join(HOME, '.config')
DATA = os.path.join(XDG_DATA, 'mhfz-plus')        # this script, the list of installs, the icon
STATE = os.path.join(DATA, 'installs.json')
WRAPPER = os.path.join(DATA, 'mhfz-plus')
ICON = os.path.join(DATA, 'mhfz-plus.png')
APPS = os.path.join(XDG_DATA, 'applications')
HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(os.path.dirname(HERE))       # the extracted package (payload/ next to setup/), when run from it
SERVERS = ('meze', 'rain')
NAMES = {'meze': 'MHF-Z MezeLounge (Plus)', 'rain': 'MHF-Z Rain (Plus)'}
LONG = {'meze': 'MezeLounge', 'rain': 'Rain'}
GAME_EXE = {'meze': 'mhf.exe', 'rain': 'client.exe'}   # the programs that load d3d9.dll (Wine override per program)
TITLE = 'MHFZ Plus setup'
RISK = ('Private servers may treat mods as cheating: use at your own risk. Your game files are only changed while you '
        'play through MHFZ Plus, and "MHFZ Plus: put the original game files back" in your menu undoes everything.')


# ------------------------------------------------------------------------------------------------ questions and messages
class UI:
    """zenity windows on a desktop, plain questions in a terminal, or nothing at all (unattended: the defaults)."""

    def __init__(self, mode):
        self.mode = mode

    @staticmethod
    def make(unattended=False, terminal=False):
        if unattended:
            return UI('none')
        if not terminal and (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')) and shutil.which('zenity'):
            return UI('zenity')
        return UI('tty' if sys.stdin.isatty() else 'none')

    def _z(self, *args):
        try:
            p = subprocess.run(['zenity'] + list(args), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               universal_newlines=True)
        except OSError:
            return 1, ''
        return p.returncode, p.stdout.strip()

    def info(self, text, title=TITLE):
        if self.mode == 'zenity':
            self._z('--info', '--no-markup', '--title', title, '--width', '560', '--text', text)
            return
        print(text)
        if self.mode == 'none' and not sys.stdout.isatty() and shutil.which('notify-send') and \
                (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
            subprocess.call(['notify-send', title, text], stderr=subprocess.DEVNULL)

    def error(self, text, title=TITLE):
        if self.mode == 'zenity':
            self._z('--error', '--no-markup', '--title', title, '--width', '560', '--text', text)
            return
        print(text, file=sys.stderr)
        # started from a menu without zenity: a desktop notification at least
        if self.mode == 'none' and not sys.stderr.isatty() and shutil.which('notify-send') and \
                (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
            subprocess.call(['notify-send', title, text], stderr=subprocess.DEVNULL)

    def ask(self, text, default=True, title=TITLE):
        if self.mode == 'zenity':
            return self._z('--question', '--no-markup', '--title', title, '--width', '560', '--text', text)[0] == 0
        if self.mode == 'tty':
            a = input('%s [%s] ' % (text, 'Y/n' if default else 'y/N')).strip().lower()
            return default if not a else a.startswith('y')
        return default

    def checklist(self, text, items, title=TITLE):
        """items: (key, label, ticked). Returns the chosen keys, or None when cancelled."""
        if self.mode == 'zenity':
            args = ['--list', '--checklist', '--title', title, '--text', text, '--width', '860',
                    '--height', str(min(560, 230 + 34 * len(items))), '--column', '', '--column', 'key', '--column', '',
                    '--hide-column', '2', '--print-column', '2', '--separator', '|', '--hide-header']
            for k, label, on in items:
                args += ['TRUE' if on else 'FALSE', k, label]
            rc, out = self._z(*args)
            return None if rc != 0 else [x for x in out.split('|') if x]
        if self.mode == 'tty':
            print('\n' + text)
            for i, (k, label, on) in enumerate(items, 1):
                print('  %d. [%s] %s' % (i, 'x' if on else ' ', label))
            a = input('Numbers to use, separated by spaces (Enter = the ticked ones, q = cancel): ').strip().lower()
            if a == 'q':
                return None
            if not a:
                return [k for k, _, on in items if on]
            pick = set(int(x) for x in re.findall(r'\d+', a))
            return [k for i, (k, _, _) in enumerate(items, 1) if i in pick]
        return [k for k, _, on in items if on]

    def choose(self, text, items, title=TITLE):
        """items: (key, label). One of them, or None."""
        if len(items) == 1:
            return items[0][0]
        if self.mode == 'zenity':
            args = ['--list', '--radiolist', '--title', title, '--text', text, '--width', '760',
                    '--height', str(min(520, 200 + 34 * len(items))), '--column', '', '--column', 'key', '--column', '',
                    '--hide-column', '2', '--print-column', '2', '--hide-header']
            for i, (k, label) in enumerate(items):
                args += ['TRUE' if i == 0 else 'FALSE', k, label]
            rc, out = self._z(*args)
            return (out or None) if rc == 0 else None
        if self.mode == 'tty':
            print('\n' + text)
            for i, (k, label) in enumerate(items, 1):
                print('  %d. %s' % (i, label))
            a = input('Number (Enter = 1): ').strip()
            n = int(a) if a.isdigit() else 1
            return items[n - 1][0] if 1 <= n <= len(items) else None
        return items[0][0] if items else None

    def folder(self, text, title=TITLE):
        if self.mode == 'zenity':
            rc, out = self._z('--file-selection', '--directory', '--title', text, '--filename', HOME + '/')
            return out if rc == 0 and out else None
        if self.mode == 'tty':
            a = input('%s\nFolder (empty = cancel): ' % text).strip()
            return os.path.expanduser(a) if a else None
        return None


# ------------------------------------------------------------------------------------------------ files and paths
def listdir(d):
    try:
        return sorted(os.listdir(d))
    except OSError:
        return []


def child(d, name):
    """d/name, the name matched without case as Wine does (Linux file names are case-sensitive)."""
    if not d:
        return None
    p = os.path.join(d, name)
    if os.path.lexists(p):
        return p
    low = name.lower()
    for e in listdir(d):
        if e.lower() == low:
            return os.path.join(d, e)
    return None


def sub(base, rel):
    """base + a relative path with \\ or / separators, matched without case; None when a part is missing."""
    p = base
    for part in re.split(r'[\\/]+', rel):
        if part:
            p = child(p, part)
            if p is None:
                return None
    return p


def new_path(base, rel):
    """Where base + rel is, or would be created: existing parts keep their case, the rest as written (as Wine does)."""
    p = base
    parts = [x for x in re.split(r'[\\/]+', rel) if x]
    for i, part in enumerate(parts):
        q = child(p, part)
        if q is None:
            return os.path.join(p, *parts[i:])
        p = q
    return p


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.mhfz-tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def file_is(path, size, digest):
    return bool(path) and os.path.isfile(path) and os.path.getsize(path) == size and sha256_file(path) == digest


def short(path):
    return '~' + path[len(HOME):] if path == HOME or path.startswith(HOME + '/') else path


def is_meze_dir(d):
    return bool(d) and os.path.isdir(d) and child(d, 'mhf.exe') is not None and os.path.isdir(child(d, 'dat') or '')


def is_rain_dir(d):
    return bool(d) and os.path.isdir(d) and os.path.isdir(sub(d, 'Client/dat') or '') and \
        os.path.isdir(child(d, 'Launcher') or '')


def is_prefix(p):
    return bool(p) and os.path.isdir(os.path.join(p, 'drive_c')) and os.path.isfile(os.path.join(p, 'system.reg'))


def prefix_above(path):
    p = os.path.realpath(path)
    while True:
        if is_prefix(p):
            return p
        q = os.path.dirname(p)
        if q == p:
            return None
        p = q


def drives(prefix):
    """The prefix's drive letters and the Linux folders they stand for (C: is drive_c, Z: usually /)."""
    out = {}
    dd = os.path.join(prefix, 'dosdevices')
    for e in listdir(dd):
        if re.match(r'^[a-zA-Z]:$', e):
            out[e[0].upper()] = os.path.realpath(os.path.join(dd, e))
    out.setdefault('C', os.path.realpath(os.path.join(prefix, 'drive_c')))
    return out


def to_windows(prefix, path):
    """The Windows path a program in the prefix sees for a Linux path."""
    real = os.path.realpath(path)
    best = None
    for letter, root in sorted(drives(prefix).items()):
        inside = real == root or real.startswith(root.rstrip('/') + '/')
        if inside and (best is None or len(root) > len(best[1])):
            best = (letter, root)
    if best is None:
        best = ('Z', '/')
    rel = '' if real == best[1] else os.path.relpath(real, best[1])
    return best[0] + ':\\' + rel.replace('/', '\\')


def to_unix(prefix, win):
    m = re.match(r'^([A-Za-z]):[\\/]*(.*)$', (win or '').strip())
    if not m:
        return None
    root = drives(prefix).get(m.group(1).upper())
    return None if not root else new_path(root, m.group(2)) if m.group(2) else root


def wine_user(prefix):
    users = os.path.join(prefix, 'drive_c', 'users')
    names = [e for e in listdir(users) if e.lower() != 'public' and os.path.isdir(os.path.join(users, e))]
    for want in ('steamuser', os.environ.get('USER', ''), os.environ.get('LOGNAME', '')):
        if want and want in names:
            return want
    for n in names:
        if os.path.isdir(os.path.join(users, n, 'AppData')):
            return n
    return names[0] if names else (os.environ.get('USER') or 'user')


def target_of(prefix):
    """Where MHFZ Plus goes in a prefix: the same place as on Windows (%LOCALAPPDATA%\\MHFZ-Plus)."""
    return new_path(os.path.join(prefix, 'drive_c'), 'users/%s/AppData/Local/MHFZ-Plus' % wine_user(prefix))


def ci_get(d, key):
    if not isinstance(d, dict):
        return None
    if key in d:
        return d[key]
    low = key.lower()
    for k, v in d.items():
        if k.lower() == low:
            return v
    return None


# ------------------------------------------------------------------------------------------------ Steam's files
def vdf_load(text):
    """Text VDF (config.vdf, libraryfolders.vdf) into nested dicts."""
    root = {}
    stack = [root]
    key = None
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"|([{}])|//[^\n]*', text):
        s, brace = m.group(1), m.group(2)
        if brace == '{':
            d = {}
            stack[-1][key if key is not None else ''] = d
            stack.append(d)
            key = None
        elif brace == '}':
            if len(stack) > 1:
                stack.pop()
            key = None
        elif s is not None:
            s = re.sub(r'\\(.)', lambda x: {'n': '\n', 't': '\t'}.get(x.group(1), x.group(1)), s)
            if key is None:
                key = s
            else:
                stack[-1][key] = s
                key = None
    return root


def vdf_dump(d, depth=0):
    esc = lambda s: str(s).replace('\\', '\\\\').replace('"', '\\"')
    tab = '\t' * depth
    out = []
    for k, v in d.items():
        if isinstance(v, dict):
            out += ['%s"%s"' % (tab, esc(k)), tab + '{'] + vdf_dump(v, depth + 1) + [tab + '}']
        else:
            out.append('%s"%s"\t\t"%s"' % (tab, esc(k), esc(v)))
    return out


def bvdf_load(b):
    """Binary VDF (shortcuts.vdf): 0x00 map, 0x01 string, 0x02 int32, 0x08 end of map."""
    pos = [0]

    def rstr():
        end = b.index(b'\0', pos[0])
        s = b[pos[0]:end].decode('utf-8', 'replace')
        pos[0] = end + 1
        return s

    def rmap():
        m = {}
        while True:
            t = b[pos[0]]
            pos[0] += 1
            if t == 8:
                return m
            k = rstr()
            if t == 0:
                m[k] = rmap()
            elif t == 1:
                m[k] = rstr()
            elif t == 2:
                m[k] = struct.unpack_from('<i', b, pos[0])[0]
                pos[0] += 4
            else:
                raise ValueError('unsupported VDF type %d at %d' % (t, pos[0]))
    return rmap()


def bvdf_dump(m):
    out = bytearray()

    def w(m):
        for k, v in m.items():
            kb = k.encode('utf-8') + b'\0'
            if isinstance(v, dict):
                out.extend(b'\0' + kb)
                w(v)
            elif isinstance(v, int):
                out.extend(b'\2' + kb + struct.pack('<i', v))
            else:
                out.extend(b'\1' + kb + str(v).encode('utf-8') + b'\0')
        out.extend(b'\x08')
    w(m)
    return bytes(out)


def shortcut_appid(exe, name):
    """Id for a new non-Steam shortcut: crc32(exe + name) with the top bit set (as Steam ROM Manager does)."""
    u = zlib.crc32((exe + name).encode('utf-8')) | 0x80000000
    return struct.unpack('<i', struct.pack('<I', u))[0]


def unsigned(i):
    return int(i) & 0xFFFFFFFF


def steam_roots():
    out = []
    for c in ('~/.steam/steam', '~/.steam/root', '~/.local/share/Steam', '~/.var/app/com.valvesoftware.Steam/.local/share/Steam',
              '~/.var/app/com.valvesoftware.Steam/data/Steam'):
        p = os.path.realpath(os.path.expanduser(c))
        if (os.path.isdir(os.path.join(p, 'steamapps')) or os.path.isdir(os.path.join(p, 'userdata'))) and p not in out:
            out.append(p)
    return out


def steam_libraries(root):
    libs = [root]
    try:
        with open(os.path.join(root, 'steamapps', 'libraryfolders.vdf'), encoding='utf-8', errors='replace') as f:
            v = vdf_load(f.read())
        for e in (ci_get(v, 'libraryfolders') or {}).values():
            p = e.get('path') if isinstance(e, dict) else e if isinstance(e, str) and e.startswith('/') else None
            if p and os.path.isdir(p) and os.path.realpath(p) not in libs:
                libs.append(os.path.realpath(p))
    except OSError:
        pass
    return libs


def steam_user_configs(root):
    ud = os.path.join(root, 'userdata')
    return [os.path.join(ud, e, 'config') for e in listdir(ud)
            if e != '0' and e.isdigit() and os.path.isdir(os.path.join(ud, e, 'config'))]


def read_shortcuts(cfg):
    try:
        with open(os.path.join(cfg, 'shortcuts.vdf'), 'rb') as f:
            return bvdf_load(f.read())
    except (OSError, ValueError, IndexError, struct.error):
        return None


class SteamFileUnknown(Exception):
    pass


def read_shortcuts_checked(cfg):
    """For changing Steam's file: None if there is none yet; its contents only if reading it and writing it back gives
    exactly its bytes. A layout this script doesn't know (a new kind of field, text that isn't UTF-8) is left alone -
    with every other game in it - rather than rewritten with something lost (or, unreadable, replaced by ours alone)."""
    path = os.path.join(cfg, 'shortcuts.vdf')
    if not os.path.exists(path):
        return None
    try:
        with open(path, 'rb') as f:
            raw = f.read()
        root = bvdf_load(raw)
        same = bvdf_dump(root) == raw
    except (OSError, ValueError, IndexError, struct.error, UnicodeError):
        same = False
    if not same:
        raise SteamFileUnknown("Steam's shortcuts.vdf is laid out in a way MHFZ Plus doesn't know, so it was left as it is.")
    return root


def steam_shortcut_names(root):
    names = {}
    for cfg in steam_user_configs(root):
        for e in (ci_get(read_shortcuts(cfg) or {}, 'shortcuts') or {}).values():
            if isinstance(e, dict) and ci_get(e, 'appid') is not None:
                names[str(unsigned(ci_get(e, 'appid')))] = str(ci_get(e, 'AppName') or '')
    return names


def proton_of(compatdata):
    """The Proton a Steam prefix was last run with (its config_info lists Proton's own files)."""
    try:
        with open(os.path.join(compatdata, 'config_info'), encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
    except OSError:
        return None
    for line in lines[1:]:
        for mark in ('/files/', '/dist/'):
            i = line.find(mark)
            if i > 0 and os.path.isfile(os.path.join(line[:i], 'proton')):
                return line[:i]
    return None


def steam_config_path(root):
    return os.path.join(root, 'config', 'config.vdf')


def compat_map(cfg, create=False):
    """InstallConfigStore > Software > Valve > Steam > CompatToolMapping in a loaded config.vdf."""
    d = cfg
    for k in ('InstallConfigStore', 'Software', 'Valve', 'Steam', 'CompatToolMapping'):
        n = ci_get(d, k)
        if not isinstance(n, dict):
            if not create:
                return None
            n = {}
            d[k] = n
        d = n
    return d


def compat_tool_of(root, appid):
    try:
        with open(steam_config_path(root), encoding='utf-8', errors='replace') as f:
            m = compat_map(vdf_load(f.read()))
    except OSError:
        return None
    e = ci_get(m or {}, str(appid))
    return dict(e) if isinstance(e, dict) and e.get('name') else None


def steam_command(root):
    if '/.var/app/com.valvesoftware.Steam/' in root + '/' and shutil.which('flatpak'):
        return ['flatpak', 'run', 'com.valvesoftware.Steam']
    s = shutil.which('steam')
    return [s] if s else None


def processes():
    for pid in listdir('/proc'):
        if pid.isdigit() and pid != str(os.getpid()):
            yield pid


def steam_running():
    for pid in processes():
        try:
            with open('/proc/%s/comm' % pid) as f:
                if f.read().strip() in ('steam', 'steamwebhelper'):
                    return True
        except OSError:
            pass
    return False


def steam_close(ui, root, why):
    """(closed or was closed, start it again afterwards)"""
    if not steam_running():
        return True, False
    if not ui.ask('Steam has to close for a moment to %s. Close Steam now?\n\n(It starts again afterwards.)' % why):
        return False, False
    cmd = steam_command(root)
    if cmd:
        subprocess.Popen(cmd + ['-shutdown'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    for _ in range(60):
        if not steam_running():
            break
        time.sleep(1)
    if steam_running():
        return False, False
    time.sleep(2)
    return True, True


def steam_start(root):
    cmd = steam_command(root)
    if cmd:
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)


# ------------------------------------------------------------------------------------------------ where the game runs
def yaml_lite(text):
    """The top two levels of a Lutris / Bottles YAML file: 'key: value', and sections of indented 'key: value'."""
    out, sec, indent = {}, None, None
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        m = re.match(r'^(\S[^:]*):\s*(.*)$', line)
        if m:
            v = unquote(m.group(2))
            out[m.group(1).strip()] = v if v else {}
            sec, indent = (m.group(1).strip(), None) if not v else (None, None)
            continue
        m = re.match(r'^(\s+)([^:#\s][^:#]*):\s*(.*)$', line)
        if m and sec and isinstance(out.get(sec), dict):
            if indent is None:
                indent = len(m.group(1))
            if len(m.group(1)) == indent:
                out[sec][m.group(2).strip()] = unquote(m.group(3))
    return out


def unquote(v):
    v = v.strip()
    if len(v) >= 2 and v[0] == v[-1] and v[0] in '\'"':
        v = v[1:-1]
    return v


def read_text(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def find_setups():
    """Every Wine prefix a game could run in, with how to run programs in it."""
    found = {}

    def add(kind, label, prefix, run, hints=()):
        if not prefix or not is_prefix(prefix):
            return
        p = os.path.realpath(prefix)
        if p in found:
            found[p]['hints'] += [h for h in hints if h]
            return
        found[p] = {'kind': kind, 'label': label, 'prefix': p, 'run': run, 'hints': [h for h in hints if h]}

    # Steam: Proton prefixes (compatdata/<id>/pfx), named after the shortcut or game they belong to
    for root in steam_roots():
        names = steam_shortcut_names(root)
        for lib in steam_libraries(root):
            cd = os.path.join(lib, 'steamapps', 'compatdata')
            for e in listdir(cd):
                comp = os.path.join(cd, e)
                add('steam', 'Steam: %s' % (names.get(e) or 'game %s' % e), os.path.join(comp, 'pfx'),
                    {'proton': proton_of(comp), 'compatdata': comp, 'appid': e, 'steam_root': root})

    # Lutris: a game file per game (game: prefix / exe, wine: version)
    for cfgdir, datadir in ((os.path.join(XDG_CONFIG, 'lutris'), os.path.join(XDG_DATA, 'lutris')),
                            (os.path.expanduser('~/.var/app/net.lutris.Lutris/config/lutris'),
                             os.path.expanduser('~/.var/app/net.lutris.Lutris/data/lutris'))):
        titles = {}
        try:
            con = sqlite3.connect('file:%s?mode=ro' % os.path.join(datadir, 'pga.db'), uri=True)
            for name, cp in con.execute('SELECT name, configpath FROM games'):
                titles[cp] = name
            con.close()
        except sqlite3.Error:
            pass
        default = (yaml_lite(read_text(os.path.join(cfgdir, 'runners', 'wine.yml'))).get('wine') or {})
        for gdir in (os.path.join(cfgdir, 'games'), os.path.join(datadir, 'games')):
            for f in listdir(gdir):
                if not f.endswith('.yml'):
                    continue
                y = yaml_lite(read_text(os.path.join(gdir, f)))
                game = y.get('game') if isinstance(y.get('game'), dict) else {}
                wine = y.get('wine') if isinstance(y.get('wine'), dict) else {}
                prefix = os.path.expanduser(game.get('prefix', ''))
                version = wine.get('version') or (default.get('version') if isinstance(default, dict) else None)
                binary = os.path.join(datadir, 'runners', 'wine', version, 'bin', 'wine') if version else ''
                add('lutris', 'Lutris: %s' % titles.get(f[:-4], f[:-4]), prefix,
                    {'wine': binary if os.path.isfile(binary) else None}, [os.path.expanduser(game.get('exe', ''))])

    # Bottles: a folder per bottle (bottle.yml: Name, Runner); started with bottles-cli
    for root in (os.path.join(XDG_DATA, 'bottles'), os.path.expanduser('~/.var/app/com.usebottles.bottles/data/bottles')):
        flatpak = '/.var/app/' in root
        cli = ['flatpak', 'run', '--command=bottles-cli', 'com.usebottles.bottles'] if flatpak and shutil.which('flatpak') \
            else [shutil.which('bottles-cli')] if not flatpak and shutil.which('bottles-cli') else None
        for b in listdir(os.path.join(root, 'bottles')):
            path = os.path.join(root, 'bottles', b)
            y = yaml_lite(read_text(os.path.join(path, 'bottle.yml')))
            name = y.get('Name') if isinstance(y.get('Name'), str) and y.get('Name') else b
            runner = y.get('Runner') if isinstance(y.get('Runner'), str) else ''
            binary = None
            for rel in ('bin/wine', 'files/bin/wine'):
                p = os.path.join(root, 'runners', runner, rel) if runner else ''
                if p and os.path.isfile(p):
                    binary = p
                    break
            add('bottles', 'Bottles: %s' % name, path, {'bottle': name, 'cli': cli, 'wine': binary})

    # Heroic: GamesConfig/<app>.json (winePrefix, wineVersion: bin / type)
    for cfgdir in (os.path.join(XDG_CONFIG, 'heroic'), os.path.expanduser('~/.var/app/com.heroicgameslauncher.hgl/config/heroic')):
        titles, exes = {}, {}
        try:
            with open(os.path.join(cfgdir, 'sideload_apps', 'library.json'), encoding='utf-8') as f:
                for g in json.load(f).get('games', []):
                    titles[g.get('app_name')] = g.get('title')
                    exes[g.get('app_name')] = (g.get('install') or {}).get('executable')
        except (OSError, ValueError, AttributeError):
            pass
        for f in listdir(os.path.join(cfgdir, 'GamesConfig')):
            try:
                with open(os.path.join(cfgdir, 'GamesConfig', f), encoding='utf-8') as fh:
                    conf = json.load(fh)
            except (OSError, ValueError):
                continue
            for app, c in conf.items() if isinstance(conf, dict) else []:
                if not isinstance(c, dict) or not c.get('winePrefix'):
                    continue
                wv = c.get('wineVersion') or {}
                wp = os.path.expanduser(c['winePrefix'])
                label = 'Heroic: %s' % (titles.get(app) or app)
                if wv.get('type') == 'proton':
                    pfx = os.path.join(wp, 'pfx') if is_prefix(os.path.join(wp, 'pfx')) else wp
                    roots = steam_roots()
                    add('heroic', label, pfx, {'proton': os.path.dirname(wv.get('bin') or '') or None, 'compatdata': wp,
                                               'steam_root': roots[0] if roots else None}, [exes.get(app)])
                else:
                    add('heroic', label, wp, {'wine': wv.get('bin')}, [exes.get(app)])

    # plain Wine: $WINEPREFIX, ~/.wine, and prefixes in ~/Games
    for p in [os.environ.get('WINEPREFIX'), os.path.join(HOME, '.wine')] + \
             [os.path.join(HOME, 'Games', e) for e in listdir(os.path.join(HOME, 'Games'))]:
        if p:
            add('wine', 'Wine: %s' % short(os.path.realpath(p)), p, {'wine': None})
    return list(found.values())


SKIP = {'windows', 'programdata', 'appdata', 'application data', 'local settings', 'temp', '$recycle.bin'}


def find_games(setup):
    """MezeLounge (mhf.exe + dat) and Rain (Launcher + Client\\dat) folders in a prefix: from the launcher's own idea
    of the game first, then a look through drive_c."""
    found = {'meze': [], 'rain': []}

    def check(d):
        if is_rain_dir(d):
            s = 'rain'
        elif is_meze_dir(d):
            s = 'meze'
        else:
            return False
        r = os.path.realpath(d)
        if r not in found[s]:
            found[s].append(r)
        return True

    for h in setup['hints']:
        if h and os.path.isabs(h):
            d = h if os.path.isdir(h) else os.path.dirname(h)
            for _ in range(3):
                if check(d):
                    break
                d = os.path.dirname(d)
    queue = [(os.path.join(setup['prefix'], 'drive_c'), 0)]
    while queue:
        d, depth = queue.pop(0)
        if check(d) or depth >= 5:
            continue
        for e in listdir(d):
            p = os.path.join(d, e)
            if e.lower() not in SKIP and not os.path.islink(p) and os.path.isdir(p):
                queue.append((p, depth + 1))
    return found


def prefix_in_use(prefix):
    """Is anything running in the prefix? (a process with that WINEPREFIX, or its wineserver)"""
    want = os.path.realpath(prefix)
    try:
        st = os.stat(want)
        tag = '/server-%x-%x' % (st.st_dev, st.st_ino)   # wineserver's working folder: /tmp/.wine-<uid>/server-<dev>-<inode>
    except OSError:
        tag = None
    for pid in processes():
        try:
            with open('/proc/%s/environ' % pid, 'rb') as f:
                env = f.read()
        except OSError:
            env = b''
        for kv in env.split(b'\0'):
            if kv.startswith(b'WINEPREFIX='):
                v = kv[11:].decode('utf-8', 'replace').rstrip('/')
                if v and os.path.realpath(v) == want:
                    return True
        if tag:
            try:
                if os.readlink('/proc/%s/cwd' % pid).endswith(tag):
                    return True
            except OSError:
                pass
    return False


# ------------------------------------------------------------------------------------------------ running Windows programs
def runtime_for(proton):
    """Steam Linux Runtime entry point Proton asks for (toolmanifest.vdf: require_tool_appid), if installed."""
    m = vdf_load(read_text(os.path.join(proton, 'toolmanifest.vdf')))
    need = ci_get(ci_get(m, 'manifest') or {}, 'require_tool_appid')
    if not need:
        return None
    names = {'1628350': 'SteamLinuxRuntime_sniper', '1391110': 'SteamLinuxRuntime_soldier'}
    for root in steam_roots():
        for lib in steam_libraries(root):
            p = os.path.join(lib, 'steamapps', 'common', names.get(str(need), ''), '_v2-entry-point')
            if names.get(str(need)) and os.path.isfile(p):
                return p
    return None


def windows_command(inst, exe, args):
    """The command and environment that run a Windows program (a Linux path) in the install's prefix."""
    run = inst['run']
    env = dict(os.environ)
    env.setdefault('WINEDEBUG', '-all')
    for k in ('SteamGameId', 'SteamAppId'):   # (started from a Steam shortcut of ours: not Steam's own game)
        env.pop(k, None)
    if run.get('proton') and os.path.isfile(os.path.join(run['proton'], 'proton')):
        roots = steam_roots()
        env['STEAM_COMPAT_DATA_PATH'] = run.get('compatdata') or os.path.dirname(inst['prefix'])
        env['STEAM_COMPAT_CLIENT_INSTALL_PATH'] = run.get('steam_root') or (roots[0] if roots else '')
        proton = os.path.join(run['proton'], 'proton')
        rt = runtime_for(run['proton'])
        if rt:
            return [rt, '--verb=waitforexitandrun', '--', proton, 'waitforexitandrun', exe] + args, env
        return [proton, 'waitforexitandrun', exe] + args, env
    if run.get('cli') and run.get('bottle'):
        return run['cli'] + ['run', '-b', run['bottle'], '-e', exe] + (['-a', ' '.join(args)] if args else []), env
    if inst['kind'] == 'steam':   # (a Proton prefix with another Wine would be upgraded and may break)
        return None, env
    wine = run.get('wine') if run.get('wine') and os.path.isfile(run['wine']) else shutil.which('wine')
    if not wine:
        return None, env
    env['WINEPREFIX'] = inst['prefix']
    return [wine, exe] + args, env


def run_windows(inst, exe, args, wait=True):
    cmd, env = windows_command(inst, exe, args)
    if not cmd:
        return None
    if wait:
        return subprocess.call(cmd, env=env, cwd=os.path.dirname(exe))
    subprocess.Popen(cmd, env=env, cwd=os.path.dirname(exe), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)
    return 0


# ------------------------------------------------------------------------------------------------ Wine registry
def reg_key(exe):
    return 'Software\\\\Wine\\\\AppDefaults\\\\%s\\\\DllOverrides' % exe    # as written in user.reg (\\ between keys)


def reg_override(prefix, exe, value):
    """Sets (value) or removes (None) the d3d9 DLL override of one program in the prefix's user.reg, so Wine loads
    the d3d9.dll in the game folder before its own. Returns what was there before (None: nothing)."""
    path = os.path.join(prefix, 'user.reg')
    with open(path, encoding='latin-1', newline='') as f:
        lines = f.read().split('\n')
    head = '[' + reg_key(exe).lower() + ']'
    start = next((i for i, l in enumerate(lines) if l.lower().startswith(head)), None)
    prev = None
    if start is not None:
        end = start + 1
        while end < len(lines) and not lines[end].startswith('['):
            end += 1
        vi = None
        for j in range(start + 1, end):
            m = re.match(r'^"d3d9"="(.*)"\s*$', lines[j], re.I)
            if m:
                vi, prev = j, m.group(1)
                break
        if value is None:
            if vi is not None:
                del lines[vi]
                end -= 1
            if not [l for l in lines[start + 1:end] if l.strip() and not l.startswith('#')]:
                del lines[start:end]   # (nothing else under the key: the key goes too)
        elif vi is not None:
            lines[vi] = '"d3d9"="%s"' % value
        else:
            pos = start + 1
            while pos < end and lines[pos].startswith('#'):
                pos += 1
            lines.insert(pos, '"d3d9"="%s"' % value)
    elif value is not None:
        now = int(time.time())
        while lines and lines[-1] == '':
            lines.pop()
        lines += ['', '[%s] %d' % (reg_key(exe), now), '#time=%x' % (now * 10000000 + 116444736000000000),
                  '"d3d9"="%s"' % value, '']
    else:
        return None
    if not os.path.exists(path + '.mhfz-plus-backup'):
        shutil.copy2(path, path + '.mhfz-plus-backup')
    write_file(path, '\n'.join(lines).encode('latin-1'))
    return prev


# ------------------------------------------------------------------------------------------------ MHFZ Plus's own files
def ini_read(path):
    ini = {}
    try:
        with open(path, 'rb') as f:
            raw = f.read()
    except OSError:
        return ini
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8-sig', 'replace')
    sec = ''
    for line in text.splitlines():
        m = re.match(r'^\s*\[(.+)\]\s*$', line)
        if m:
            sec = m.group(1).lower()
            ini.setdefault(sec, {})
            continue
        m = re.match(r'^\s*([^;=]+?)\s*=\s*(.*)$', line)
        if sec and m:
            ini[sec][m.group(1).lower()] = m.group(2)
    return ini


def ini_write(path, ini):
    out = ['; MHFZ Plus settings (written by install.sh; your own extra keys are kept when it runs again)', '']
    for s, kv in ini.items():
        out.append('[%s]' % s)
        out += ['%s=%s' % (k, v) for k, v in kv.items()]
        out.append('')
    # UTF-16 with BOM, as the Windows setup writes it (read by GetPrivateProfileStringW)
    write_file(path, ('﻿' + '\r\n'.join(out) + '\r\n').encode('utf-16-le'))


def set_backend(path, value):
    """d3d9_backend in a mhfz-fps.ini: only an empty value or an MHFZ Plus DXVK path is replaced."""
    try:
        with open(path, 'rb') as f:
            lines = f.read().decode('utf-8-sig', 'replace').splitlines()
    except OSError:
        return
    i = next((k for k, l in enumerate(lines) if re.match(r'^\s*d3d9_backend\s*=', l)), -1)
    if i >= 0:
        cur = lines[i].split('=', 1)[1].strip()
        if cur and not re.search(r'\\fps\\dxvk\\d3d9\.dll$', cur, re.I):
            return   # (chosen by hand)
        lines[i] = 'd3d9_backend=' + value
    elif value:
        s = next((k for k, l in enumerate(lines) if l.strip().lower() == '[fps]'), -1)
        if s < 0:
            return
        e = s + 1
        while e < len(lines) and not lines[e].lstrip().startswith('['):
            e += 1
        while e > s + 1 and not lines[e - 1].strip():
            e -= 1
        lines.insert(e, 'd3d9_backend=' + value)
    else:
        return
    write_file(path, ('\r\n'.join(lines) + '\r\n').encode('utf-8'))


def manifest(target, server):
    """english/<server>/manifest.txt: rel|origSize|origSha256|newSize|newSha256|deltaFile"""
    out = []
    for line in read_text(os.path.join(target, 'english', server, 'manifest.txt')).splitlines():
        p = line.split('|')
        if line.startswith('#') or len(p) < 6:
            continue
        try:
            out.append({'rel': p[0], 'osize': int(p[1]), 'ohash': p[2].lower(), 'nsize': int(p[3]),
                        'nhash': p[4].lower(), 'delta': p[5].strip()})
        except ValueError:
            pass
    return out


def apply_delta(orig, delta):
    """'MHFD' u32 version(1) u32 origSize u32 newSize, then: 1 COPY u32 offset u32 length | 2 ADD u32 length + bytes | 0 END"""
    if len(delta) < 16 or delta[:4] != b'MHFD':
        return None
    ver, osz, nsz = struct.unpack_from('<III', delta, 4)
    if ver != 1 or osz != len(orig):
        return None
    out = bytearray()
    p = 16
    while p < len(delta):
        op = delta[p]
        p += 1
        if op == 0:
            break
        if op == 1 and p + 8 <= len(delta):
            a, n = struct.unpack_from('<II', delta, p)
            p += 8
            if a + n > len(orig):
                return None
            out += orig[a:a + n]
        elif op == 2 and p + 4 <= len(delta):
            (n,) = struct.unpack_from('<I', delta, p)
            p += 4
            if p + n > len(delta):
                return None
            out += delta[p:p + n]
            p += n
        else:
            return None
    return bytes(out) if len(out) == nsz else None


def prepare_english(target, server, game):
    """Builds cache/<server>/ from the deltas and the player's own originals, and backs every original up (what the
    launcher's --prepare does). Returns (ready, left out)."""
    ready = skipped = 0
    for pf in manifest(target, server):
        cache = new_path(target, 'cache/%s/%s' % (server, pf['rel']))
        bak = new_path(target, 'backup/%s/%s' % (server, pf['rel']))
        have_cache = file_is(cache, pf['nsize'], pf['nhash'])
        have_bak = file_is(bak, pf['osize'], pf['ohash'])
        if have_cache and have_bak:
            ready += 1
            continue
        orig = None
        for src in (bak, sub(game, pf['rel'])):
            if src and os.path.isfile(src) and os.path.getsize(src) == pf['osize']:
                with open(src, 'rb') as f:
                    data = f.read()
                if hashlib.sha256(data).hexdigest() == pf['ohash']:
                    orig = data
                    break
        if orig is None:
            skipped += 1   # (a different game version)
            continue
        if not have_bak:
            write_file(bak, orig)
        if not have_cache:
            try:
                with open(new_path(target, 'english/%s/%s' % (server, pf['delta'])), 'rb') as f:
                    delta = f.read()
            except OSError:
                delta = b''
            out = apply_delta(orig, delta)
            if out is None or hashlib.sha256(out).hexdigest() != pf['nhash']:
                skipped += 1
                continue
            write_file(cache, out)
        ready += 1
    return ready, skipped


def state_of(path, pf):
    if not path or not os.path.isfile(path):
        return 'missing'
    size = os.path.getsize(path)
    if size != pf['osize'] and size != pf['nsize']:
        return 'unknown'
    h = sha256_file(path)
    return 'original' if h == pf['ohash'] else 'english' if h == pf['nhash'] else 'unknown'


def our_dll(path):
    """Only a d3d9.dll of ours is ever removed (another one, ReShade or dgVoodoo, stays)."""
    try:
        with open(path, 'rb') as f:
            return b'mhfz-fps.ini' in f.read()
    except OSError:
        return False


def tidy_fps_files(target, server, game):
    """Swap mode (Rain): the mod's settings, logs and captures move out of the game folder (as the launcher does)."""
    st = new_path(target, 'fps-state/' + server)
    os.makedirs(st, exist_ok=True)
    if child(game, 'mhfz-fps.log') and os.path.exists(os.path.join(st, 'mhfz-fps.log')):
        os.replace(os.path.join(st, 'mhfz-fps.log'), os.path.join(st, 'mhfz-fps.prev.log'))
    for f in ('mhfz-fps.ini', 'mhfz-fps.log', 'mhfz-fps-dump.txt'):
        src = child(game, f)
        if src:
            shutil.move(src, os.path.join(st, f))
    prev = child(game, 'mhfz-fps.prev.log')
    if prev:
        os.remove(prev)
    for e in listdir(game):
        if e.lower().endswith(('_d3d9.log', '_dxgi.log')):
            shutil.move(os.path.join(game, e), os.path.join(st, e))
    for d in ('paircap', 'retro', 'captures', 'selfcheck', 'trace'):
        src = child(game, d)
        if src and os.path.isdir(src):
            dst, k = os.path.join(st, d), 2
            while os.path.exists(dst):
                dst, k = os.path.join(st, '%s-%d' % (d, k)), k + 1
            shutil.move(src, dst)


def restore_server(target, server, game, swap):
    """Original game files back and the frame-rate mod out (the launcher's 'restore'). Only files that are exactly
    our English or original version are touched, and only from a backup that is still exactly the original (a damaged
    one would break the game: the file stays then, a note says so). Returns (originals in place, problems, notes)."""
    ok, problems, notes = 0, [], []
    for pf in manifest(target, server):
        bak = sub(target, 'backup/%s/%s' % (server, pf['rel']))
        if not bak or not os.path.isfile(bak):
            continue
        g = sub(game, pf['rel']) or new_path(game, pf['rel'])
        st = state_of(g, pf)
        if st == 'original':
            ok += 1
        elif st == 'english':
            if os.path.getsize(bak) != pf['osize'] or sha256_file(bak) != pf['ohash']:
                notes.append("the backup of %s is damaged - the game file is left as it is (let the game's launcher "
                             "check its files)" % pf['rel'])
                continue
            try:
                shutil.copy2(bak, g)
                ok += 1
            except OSError as e:
                problems.append('could not put back %s (%s)' % (pf['rel'], e.strerror))
    dll = child(game, 'd3d9.dll')
    if dll and our_dll(dll):
        try:
            os.remove(dll)
        except OSError as e:
            problems.append('could not remove d3d9.dll (%s)' % e.strerror)
    if swap and not child(game, 'd3d9.dll'):
        tidy_fps_files(target, server, game)
    return ok, problems, notes


# ------------------------------------------------------------------------------------------------ menu entries
def exec_arg(a):
    a = a.replace('%', '%%')
    if not re.search(r'[\s"\'\\><~|&;$*?#()`]', a):
        return a
    return '"' + ''.join('\\\\' + c if c in '"`$\\' else c for c in a) + '"'


def desktop_entry(path, name, comment, args, categories='Game;'):
    text = '\n'.join(['[Desktop Entry]', 'Type=Application', 'Name=' + name, 'Comment=' + comment,
                      'Exec=' + ' '.join(exec_arg(a) for a in [WRAPPER] + args), 'Icon=' + ICON, 'Terminal=false',
                      'Categories=' + categories, 'StartupNotify=false', ''])
    write_file(path, text.encode('utf-8'))
    os.chmod(path, 0o755)
    return path


def desktop_dir():
    try:
        d = subprocess.run(['xdg-user-dir', 'DESKTOP'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           universal_newlines=True).stdout.strip()
    except OSError:
        d = ''
    if d and d != HOME and os.path.isdir(d):
        return d
    d = os.path.join(HOME, 'Desktop')
    return d if os.path.isdir(d) else None


def app_name(inst, installs):
    """'MHFZ Plus' - or, with MezeLounge and Rain in different prefixes, one app each: 'MHFZ Plus (Rain)'."""
    if len(installs) <= 1:
        return 'MHFZ Plus'
    return 'MHFZ Plus (%s)' % ' + '.join(LONG[s] for s in SERVERS if s in inst['servers'])


def make_entries(installs, desktop):
    """One menu entry: the MHFZ Plus app (play, settings, help) - one per prefix when the games are in two."""
    made = []
    for k, inst in enumerate(installs):
        name = 'mhfz-plus.desktop' if len(installs) == 1 else 'mhfz-plus-%d.desktop' % (k + 1)
        first = sorted(inst['servers'], key=SERVERS.index)[0]
        made.append(desktop_entry(os.path.join(APPS, name), app_name(inst, installs),
                                  'Monster Hunter Frontier Z with MHFZ Plus: play, settings, help and bug reports', ['app', first]))
    d = desktop_dir() if desktop else None
    for src in list(made) if d else []:
        p = os.path.join(d, os.path.basename(src))
        shutil.copy2(src, p)
        os.chmod(p, 0o755)
        if shutil.which('gio'):   # GNOME: start it without the "untrusted launcher" question
            subprocess.call(['gio', 'set', p, 'metadata::trusted', 'true'], stderr=subprocess.DEVNULL)
        made.append(p)
    if shutil.which('update-desktop-database'):
        subprocess.call(['update-desktop-database', '-q', APPS], stderr=subprocess.DEVNULL)
    return made


# ------------------------------------------------------------------------------------------------ Steam shortcuts
def our_shortcut(e):
    exe = str(ci_get(e, 'Exe') or '').strip().strip('"')
    return exe.lower().endswith(('mhfz-plus.exe', 'mhfz-plus-app.exe')) or exe.endswith('/mhfz-plus')


def set_shortcuts(cfg, entries):
    """Adds or updates our non-Steam shortcuts; entries of ours with the same name keep their id (artwork, play
    time). Returns {name: appid}."""
    file = os.path.join(cfg, 'shortcuts.vdf')
    root = read_shortcuts_checked(cfg) or {'shortcuts': {}}   # (raises SteamFileUnknown: nothing changed)
    if os.path.exists(file):
        shutil.copy2(file, file + '.mhfz-plus-backup')
    old = ci_get(root, 'shortcuts') or {}
    names = [n['name'] for n in entries]
    keep, prev = [], {}
    for e in old.values():
        if isinstance(e, dict) and our_shortcut(e) and ci_get(e, 'AppName') in names:
            prev[ci_get(e, 'AppName')] = e
        else:
            keep.append(e)
    ids = {}
    for n in entries:
        q = '"%s"' % n['exe']
        p = prev.get(n['name'], {})
        appid = ci_get(p, 'appid')
        e = {'appid': appid if appid is not None else shortcut_appid(q, n['name']), 'AppName': n['name'], 'Exe': q,
             'StartDir': '"%s"' % n['start'], 'icon': n['icon'], 'ShortcutPath': '', 'LaunchOptions': n['opts'],
             'IsHidden': 0, 'AllowDesktopConfig': 1, 'AllowOverlay': 1, 'OpenVR': 0, 'Devkit': 0, 'DevkitGameID': '',
             'DevkitOverrideAppID': 0, 'LastPlayTime': ci_get(p, 'LastPlayTime') or 0, 'FlatpakAppID': '', 'sortas': '',
             'tags': ci_get(p, 'tags') or {}}
        keep.append(e)
        ids[n['name']] = unsigned(e['appid'])
    root = {k: v for k, v in root.items() if k.lower() != 'shortcuts'}
    root['shortcuts'] = {str(i): e for i, e in enumerate(keep)}
    write_file(file, bvdf_dump(root))
    copy_artwork(cfg, root['shortcuts'])
    return ids


def remove_shortcuts(cfg):
    root = read_shortcuts_checked(cfg)   # (raises SteamFileUnknown: nothing changed)
    if not root:
        return False
    old = ci_get(root, 'shortcuts') or {}
    keep = [e for e in old.values() if not (isinstance(e, dict) and our_shortcut(e))]
    if len(keep) == len(old):
        return False
    shutil.copy2(os.path.join(cfg, 'shortcuts.vdf'), os.path.join(cfg, 'shortcuts.vdf.mhfz-plus-backup'))
    root = {k: v for k, v in root.items() if k.lower() != 'shortcuts'}
    root['shortcuts'] = {str(i): e for i, e in enumerate(keep)}
    write_file(os.path.join(cfg, 'shortcuts.vdf'), bvdf_dump(root))
    return True


def copy_artwork(cfg, shortcuts):
    """Artwork the player already set for another Monster Hunter Frontier shortcut is reused for ours."""
    grid = os.path.join(cfg, 'grid')
    if not os.path.isdir(grid):
        return
    files = listdir(grid)
    src, ours = None, []
    for e in shortcuts.values():
        if not isinstance(e, dict) or ci_get(e, 'appid') is None:
            continue
        i = str(unsigned(ci_get(e, 'appid')))
        if our_shortcut(e):
            ours.append(i)
        elif not src and (re.search(r'rain-server-launcher|mhf\.exe|mhfo', str(ci_get(e, 'Exe') or ''), re.I) or
                          'frontier' in str(ci_get(e, 'AppName') or '').lower()) and any(f.startswith(i) for f in files):
            src = i
    for i in ours if src else []:
        if any(f.startswith(i) for f in files):
            continue   # (has its own artwork)
        for f in files:
            if f.startswith(src):
                shutil.copy2(os.path.join(grid, f), os.path.join(grid, i + f[len(src):]))


def set_compat(root, appids, tool):
    """Our shortcuts run with the Proton the game's own shortcut uses (config.vdf, CompatToolMapping)."""
    path = steam_config_path(root)
    cfg = vdf_load(read_text(path))
    if not cfg:
        return False
    if not os.path.exists(path + '.mhfz-plus-backup'):
        shutil.copy2(path, path + '.mhfz-plus-backup')
    m = compat_map(cfg, create=True)
    for a in appids:
        m[str(a)] = dict(tool)
    write_file(path, ('\n'.join(vdf_dump(cfg)) + '\n').encode('utf-8'))
    return True


def remove_compat(root, appids):
    path = steam_config_path(root)
    cfg = vdf_load(read_text(path))
    m = compat_map(cfg) if cfg else None
    if not m:
        return
    gone = [k for k in list(m) if k in [str(a) for a in appids]]
    for k in gone:
        del m[k]
    if gone:
        write_file(path, ('\n'.join(vdf_dump(cfg)) + '\n').encode('utf-8'))


def add_to_steam(ui, installs, report):
    roots = [r for r in steam_roots() if steam_user_configs(r)]
    if not roots:
        report.append('Steam: no Steam user found - not added')
        return
    ok, restart = steam_close(ui, roots[0], 'add the games')
    if not ok:
        report.append('Steam: not added (Steam was open) - run install.sh again to add them')
        return
    added, problem = [], None
    for root in roots:
        entries = []
        for inst in installs:
            # the MHFZ Plus app, then a Play entry per server (one press to play: Steam Deck, Big Picture)
            tool = compat_tool_of(root, inst['run']['appid']) \
                if inst['kind'] == 'steam' and inst['run'].get('steam_root') == root else None
            first = sorted(inst['servers'], key=SERVERS.index)[0]
            if tool:
                entries.append({'name': app_name(inst, installs), 'exe': os.path.join(inst['target'], 'MHFZ-Plus-App.exe'),
                                'start': inst['target'], 'icon': ICON, 'tool': tool, 'inst': inst, 'server': 'app',
                                'opts': 'STEAM_COMPAT_DATA_PATH="%s" %%command%%' % inst['run']['compatdata']})
            else:
                entries.append({'name': app_name(inst, installs), 'exe': WRAPPER, 'start': DATA, 'icon': ICON, 'tool': None,
                                'inst': inst, 'server': 'app', 'opts': 'app %s' % first})
            for s in inst['servers']:
                tool = compat_tool_of(root, inst['run']['appid']) \
                    if inst['kind'] == 'steam' and inst['run'].get('steam_root') == root else None
                if tool:   # Steam runs MHFZ-Plus.exe with the game's own Proton, in the game's own prefix
                    entries.append({'name': NAMES[s], 'exe': os.path.join(inst['target'], 'MHFZ-Plus.exe'),
                                    'start': inst['target'], 'icon': ICON, 'tool': tool, 'inst': inst, 'server': s,
                                    'opts': 'STEAM_COMPAT_DATA_PATH="%s" %%command%% %s' % (inst['run']['compatdata'], s)})
                else:      # Steam runs our script, which starts it in the prefix
                    entries.append({'name': NAMES[s], 'exe': WRAPPER, 'start': DATA, 'icon': ICON, 'tool': None,
                                    'inst': inst, 'server': s, 'opts': 'play %s' % s})
        for cfg in steam_user_configs(root):
            try:
                ids = set_shortcuts(cfg, entries)
            except SteamFileUnknown as e:   # (Steam's file left as it was: everything else is installed)
                problem = str(e)
                continue
            for n in entries:
                appid = ids[n['name']]
                n['inst'].setdefault('steam', {}).setdefault(root, {})[n['server']] = appid
                if n['tool']:
                    set_compat(root, [appid], n['tool'])
                    n['inst'].setdefault('steam_direct', {})[n['server']] = appid
            added += [n['name'] for n in entries]
    if restart:
        steam_start(roots[0])
    if added:
        report.append('Steam: added %s' % ' and '.join(sorted(set(added))))
    if problem:
        report.append('Steam: not added - %s Add MHFZ Plus in Steam yourself (Games > Add a Non-Steam Game: %s).' % (problem, WRAPPER))


# ------------------------------------------------------------------------------------------------ the list of installs
def load_state():
    try:
        with open(STATE, encoding='utf-8') as f:
            st = json.load(f)
        if isinstance(st, dict) and isinstance(st.get('installs'), list):
            return st
    except (OSError, ValueError):
        pass
    return {'installs': [], 'entries': []}


def save_state(st):
    write_file(STATE, (json.dumps(st, indent=2) + '\n').encode('utf-8'))


def version_of(pkg_or_target):
    for p in (os.path.join(pkg_or_target, 'payload', 'version.txt'), os.path.join(pkg_or_target, 'version.txt')):
        v = read_text(p).strip().splitlines()
        if v:
            return v[0].strip()
    return '1.x'


# ------------------------------------------------------------------------------------------------ install
def install_prefix(setup, games, opts, old, report):
    """MHFZ Plus into one prefix for the games found there ({server: Linux folder})."""
    prefix = setup['prefix']
    target = old['target'] if old and os.path.isdir(old['target']) else target_of(prefix)
    payload = os.path.join(PKG, 'payload')
    os.makedirs(target, exist_ok=True)
    shutil.copytree(payload, target, dirs_exist_ok=True)
    old_settings = child(target, 'MHFZ-Plus-Settings.exe')   # (1.3 and older: now the MHFZ Plus app)
    if old_settings:
        os.remove(old_settings)

    ini_path = os.path.join(target, 'MHFZ-Plus.ini')
    ini = ini_read(ini_path)
    ini.setdefault('general', {})['server'] = sorted(games, key=SERVERS.index)[0]
    # (the app, running in Wine, hands bug reports and uninstalling to this script: where it is, as Wine sees it)
    ini.setdefault('linux', {})['tool'] = to_windows(prefix, WRAPPER)
    for s, game in games.items():
        sec = ini.setdefault(s, {})
        sec['game'] = to_windows(prefix, game if s == 'meze' else sub(game, 'Client'))
        sec['english'] = str(int(opts['english']))
        sec['fps'] = str(int(opts['fps']))
        sec.setdefault('fps_mode', 'stay' if s == 'meze' else 'swap')
    ini_write(ini_path, ini)

    # graphics backend: the DXVK that comes with MHFZ Plus, in the default settings and each game's own
    dxvk = os.path.join(target, 'fps', 'dxvk', 'd3d9.dll')
    if opts['fps'] and os.path.isfile(dxvk):
        files = [os.path.join(target, 'fps', 'mhfz-fps.ini'), new_path(target, 'fps-state/rain/mhfz-fps.ini')]
        if 'meze' in games:
            files.append(child(games['meze'], 'mhfz-fps.ini') or '')
        for f in files:
            if f and os.path.isfile(f):
                set_backend(f, to_windows(prefix, dxvk) if opts['dxvk'] else '')

    # the English files from the player's own originals (this also checks the game version)
    for s, game in games.items():
        if not opts['english']:
            report.append('%s: English off' % LONG[s])
            continue
        ready, left = prepare_english(target, s, game if s == 'meze' else sub(game, 'Client'))
        if not ready and not left:
            report.append('%s: the English patch could not be prepared' % LONG[s])
        elif left:
            report.append("%s: English ready (%d files; %d don't match your game version and are left out)" % (LONG[s], ready, left))
        else:
            report.append('%s: English ready (%d files)' % (LONG[s], ready))

    # Wine: the game loads the d3d9.dll in its own folder (the mod) instead of Wine's
    reg = dict(old.get('reg', {})) if old else {}
    for s in games:
        exe = GAME_EXE[s]
        prev = reg_override(prefix, exe, 'native,builtin')
        if exe not in reg:
            reg[exe] = prev
    return {'prefix': prefix, 'kind': setup['kind'], 'label': setup['label'], 'run': setup['run'], 'target': target,
            'servers': dict(games), 'reg': reg}


def cmd_install(a):
    ui = UI.make(a.unattended, a.terminal)
    if ui.mode == 'none' and not a.unattended:   # (started without a terminal, and no zenity for a window)
        ui.error('Run install.sh in a terminal (or install zenity for the setup window).')
        return 1
    if not os.path.isfile(os.path.join(PKG, 'payload', 'MHFZ-Plus.exe')):
        ui.error('The setup files are incomplete.\n\nExtract the whole zip file to a folder first, then run install.sh from there.')
        return 1
    version = version_of(PKG)
    st = load_state()
    has_english = os.path.isfile(os.path.join(PKG, 'payload', 'english', 'meze', 'manifest.txt'))
    has_dxvk = os.path.isfile(os.path.join(PKG, 'payload', 'fps', 'dxvk', 'd3d9.dll'))
    if ui.mode == 'tty':
        print('MHFZ Plus %s setup: looking for MezeLounge and Rain in your Wine prefixes...' % version)
    setups = find_setups()
    for p in a.prefix:   # (a prefix given by hand)
        if is_prefix(p) and os.path.realpath(p) not in [s['prefix'] for s in setups]:
            setups.append({'kind': 'wine', 'label': 'Wine: %s' % short(os.path.realpath(p)), 'prefix': os.path.realpath(p),
                           'run': {'wine': None}, 'hints': []})
    if a.only_prefix:
        setups = [s for s in setups if s['prefix'] == os.path.realpath(a.only_prefix)]

    # every game found: (setup, server, folder)
    cands = []
    for setup in setups:
        for s, dirs in find_games(setup).items():
            cands += [(setup, s, d) for d in dirs]
    for g in a.game:   # games given by hand (folders inside a prefix)
        d = os.path.realpath(g)
        s = 'rain' if is_rain_dir(d) else 'meze' if is_meze_dir(d) else None
        p = prefix_above(d)
        if not s or not p:
            ui.error('Not a MezeLounge or Rain folder inside a Wine prefix: %s' % g)
            return 1
        setup = next((x for x in setups if x['prefix'] == p), None) or \
            {'kind': 'wine', 'label': 'Wine: %s' % short(p), 'prefix': p, 'run': {'wine': None}, 'hints': []}
        if setup not in setups:
            setups.append(setup)
        if not any(c[2] == d for c in cands):
            cands.append((setup, s, d))
    if a.game and a.unattended:   # (unattended with folders given: only those)
        cands = [c for c in cands if c[2] in [os.path.realpath(x) for x in a.game]]

    before = {(i['prefix'], s, os.path.realpath(d)) for i in st['installs'] for s, d in i['servers'].items()}
    taken = set()
    items = []
    for k, (setup, s, d) in enumerate(cands):
        on = (setup['prefix'], s, d) in before if before else s not in taken
        taken.add(s)
        items.append((str(k), '%s  -  %s  -  %s' % (LONG[s], setup['label'], short(d)), on))
    if not a.unattended:
        items.append(('other', 'Another folder (choose it next)', not cands))
    upd = ''
    if st['installs']:
        upd = 'MHFZ Plus %s is installed: this updates it to %s. Your settings and clips are kept.\n\n' % (
            st.get('version', 'an earlier version'), version)
    text = upd + ('MezeLounge and Rain installs found in your Wine prefixes. Tick the ones to use with MHFZ Plus.' if cands
                  else 'No MezeLounge or Rain install was found. Choose the game folder (the folder with mhf.exe, or '
                       'the one with Launcher and Client) inside the Wine prefix the game runs in.')
    picked = ui.checklist(text, items, 'MHFZ Plus %s setup' % version)
    if picked is None:
        return 1
    chosen = [cands[int(k)] for k in picked if k.isdigit()]
    if 'other' in picked:
        d = ui.folder('The MezeLounge folder (with mhf.exe) or the Rain folder (with Launcher and Client)')
        if not d:
            return 1
        d = os.path.realpath(d)
        s = 'rain' if is_rain_dir(d) else 'meze' if is_meze_dir(d) else None
        p = prefix_above(d)
        if not s:
            ui.error('This folder has no mhf.exe and dat folder, and no Launcher and Client\\dat folders:\n%s' % d)
            return 1
        if not p:
            ui.error('This folder is not inside a Wine prefix:\n%s\n\nMHFZ Plus runs in the prefix the game runs in: '
                     'choose the game folder inside it (drive_c/...).' % d)
            return 1
        setup = next((x for x in setups if x['prefix'] == p), None) or \
            {'kind': 'wine', 'label': 'Wine: %s' % short(p), 'prefix': p, 'run': {'wine': None}, 'hints': []}
        chosen.append((setup, s, d))
    for s in SERVERS:
        if len([c for c in chosen if c[1] == s]) > 1:
            ui.error('Choose one %s install (MHFZ Plus starts one of each).' % LONG[s])
            return 1
    if not chosen:
        ui.error('Choose MezeLounge, Rain, or both.')
        return 1

    roots = [r for r in steam_roots() if steam_user_configs(r)]
    opt_items = [('fps', "High frame rate (up to your monitor's refresh rate; game speed stays the same)", not a.no_fps)]
    if has_english:
        opt_items.append(('english', 'English translation', not a.no_english))
    if has_dxvk:
        opt_items.append(('dxvk', 'Use the DXVK that comes with MHFZ Plus (faster; needs Vulkan)', not a.no_dxvk))
    opt_items.append(('desktop', 'Desktop shortcuts', not a.no_desktop))
    if roots:
        opt_items.append(('steam', 'Add to Steam (as non-Steam games; Steam closes for a moment)', not a.no_steam))
    note = '' if has_english else '\n\n(The English translation is not included in this download.)'
    picked = ui.checklist('Use' + note + '\n\n' + RISK, opt_items, 'MHFZ Plus %s setup' % version)
    if picked is None:
        return 1
    opts = {k: k in picked for k in ('fps', 'english', 'dxvk', 'desktop', 'steam')}
    if not opts['fps'] and not opts['english']:
        ui.error('Choose the frame-rate mod, the English translation, or both.')
        return 1

    # one install per prefix; nothing may run in it while its files and registry change
    by_prefix = {}
    for setup, s, d in chosen:
        by_prefix.setdefault(setup['prefix'], (setup, {}))[1][s] = d
    for p, (setup, games) in by_prefix.items():
        if prefix_in_use(p):
            ui.error('Something is still running in %s (the game, its launcher or MHFZ Plus).\n\nClose it, then run '
                     'install.sh again.' % setup['label'])
            return 1
    report = []
    installs = []
    for p, (setup, games) in by_prefix.items():
        old = next((i for i in st['installs'] if i['prefix'] == p), None)
        try:
            installs.append(install_prefix(setup, games, opts, old, report))
        except OSError as e:
            ui.error('Setup failed in %s: %s' % (setup['label'], e))
            return 1
    # installs in other prefixes for servers not chosen now stay; the same server moved elsewhere replaces its old one
    for i in st['installs']:
        if i['prefix'] not in by_prefix:
            i['servers'] = {s: d for s, d in i['servers'].items() if s not in [c[1] for c in chosen]}
            if i['servers']:
                installs.append(i)

    # this script, its icon and a launcher for the menu entries
    os.makedirs(DATA, exist_ok=True)
    shutil.copy2(os.path.abspath(__file__), os.path.join(DATA, 'mhfzplus.py'))
    if os.path.isfile(os.path.join(HERE, 'mhfz-plus.png')):
        shutil.copy2(os.path.join(HERE, 'mhfz-plus.png'), ICON)
    write_file(WRAPPER, b'#!/bin/sh\n# MHFZ Plus for Linux (see mhfzplus.py)\nexec python3 "$(dirname "$(readlink -f "$0")")/mhfzplus.py" "$@"\n')
    os.chmod(WRAPPER, 0o755)
    for old_entry in st.get('entries', []):
        if os.path.basename(old_entry).startswith('mhfz-plus') and os.path.exists(old_entry):
            os.remove(old_entry)
    entries = make_entries(installs, opts['desktop'])
    if opts['steam']:
        add_to_steam(ui, installs, report)
    save_state({'version': version, 'installs': installs, 'entries': entries})
    for i in installs:
        write_file(os.path.join(i['target'], 'install-report.txt'), ('\n'.join(report) + '\n').encode('utf-8'))
    ui.info('MHFZ Plus is installed.\n\n%s\n\nFrom now on, start the game from "MHFZ Plus" in your app menu (or from '
            'Steam) instead of the usual launcher. It also has the settings, and Help for bug reports, the original '
            'files and uninstalling.' % '\n'.join(report))
    return 0


# ------------------------------------------------------------------------------------------------ play, settings, folder
def pick_install(ui, st, what):
    items = [(str(k), '%s  -  %s' % (i['label'], ', '.join(LONG[s] for s in i['servers']))) for k, i in enumerate(st['installs'])]
    k = ui.choose(what, items, 'MHFZ Plus')
    return st['installs'][int(k)] if k is not None else None


def cmd_play(a):
    ui = UI.make(a.unattended)
    st = load_state()
    inst = next((i for i in st['installs'] if a.server in i['servers']), None)
    if not inst:
        ui.error('MHFZ Plus is not set up for %s. Run install.sh first.' % LONG.get(a.server, a.server), 'MHFZ Plus')
        return 1
    # a Steam game with our own Steam entry: start that, so Steam runs it in its Proton and runtime
    gid = (inst.get('steam_direct') or {}).get(a.server)
    if gid and not os.environ.get('SteamGameId') and shutil.which('xdg-open'):
        return subprocess.call(['xdg-open', 'steam://rungameid/%d' % ((int(gid) << 32) | 0x02000000)])
    exe = os.path.join(inst['target'], 'MHFZ-Plus.exe')
    rc = run_windows(inst, exe, [a.server])
    if rc is None:
        ui.error('Wine was not found, so MHFZ Plus cannot start %s.\n\nInstall Wine, or start the game the way you '
                 'usually do (Steam, Lutris, Bottles) once to check it still works.' % LONG[a.server], 'MHFZ Plus')
        return 1
    return rc


def cmd_app(a):
    """The MHFZ Plus app (play, settings, help), in the prefix of the install with that server (or the only one)."""
    ui = UI.make(a.unattended)
    st = load_state()
    if not st['installs']:
        ui.error('MHFZ Plus is not installed.', 'MHFZ Plus')
        return 1
    server = getattr(a, 'server', None)
    inst = next((i for i in st['installs'] if server in i['servers']), None) if server else None
    inst = inst or (st['installs'][0] if len(st['installs']) == 1 else pick_install(ui, st, 'Which MHFZ Plus?'))
    if not inst:
        return 1
    exe = child(inst['target'], 'MHFZ-Plus-App.exe') or os.path.join(inst['target'], 'MHFZ-Plus-Settings.exe')
    if run_windows(inst, exe, [], wait=False) is None:
        ui.error('Wine was not found, so the MHFZ Plus app cannot start. The settings are in mhfz-fps.ini in the MHFZ '
                 'Plus folder (a text file).', 'MHFZ Plus')
        return 1
    return 0


def cmd_folder(a):
    ui = UI.make(a.unattended)
    st = load_state()
    inst = pick_install(ui, st, 'Open the folder of which install?') if st['installs'] else None
    if not inst:
        return 1
    if shutil.which('xdg-open'):
        subprocess.Popen(['xdg-open', inst['target']], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        print(inst['target'])
    return 0


# ------------------------------------------------------------------------------------------------ bug report
ISSUE_URL = 'https://github.com/Sushii-dev/mhfz-plus/issues/new?template=bug_report.yml'
OURS = ('mhfz-plus*.log', 'mhfz-fps.log', 'mhfz-fps.prev.log', 'mhfz-fps.ini', 'mhfz-fps-dump.txt', '*_d3d9.log',
        '*_dxgi.log', 'install-report.txt', 'mhfz-plus.ini')


def run_text(cmd, timeout=10):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True,
                              timeout=timeout).stdout
    except (OSError, subprocess.SubprocessError):
        return ''


def linux_specs():
    import platform
    lines = []
    osr = dict(re.findall(r'^(\w+)="?([^"\n]*)"?$', read_text('/etc/os-release'), re.M))
    product = read_text('/sys/class/dmi/id/product_name').strip()
    deck = ' (Steam Deck)' if product in ('Jupiter', 'Galileo') else ''
    lines.append('Linux: %s, kernel %s%s' % (osr.get('PRETTY_NAME', '?'), platform.release(), deck))
    cpu = re.search(r'^model name\s*:\s*(.+)$', read_text('/proc/cpuinfo'), re.M)
    mem = re.search(r'^MemTotal:\s*(\d+)', read_text('/proc/meminfo'), re.M)
    lines.append('CPU: %s (%s threads), RAM %s GB' % (cpu.group(1).strip() if cpu else '?', os.cpu_count(),
                                                      round(int(mem.group(1)) / 1048576) if mem else '?'))
    for l in run_text(['lspci']).splitlines() if shutil.which('lspci') else []:
        if re.search(r'VGA|3D controller|Display controller', l):
            lines.append('GPU: ' + l.split(': ', 1)[-1])
    vk = run_text(['vulkaninfo', '--summary']) if shutil.which('vulkaninfo') else ''
    for l in vk.splitlines():
        if re.search(r'deviceName|driverInfo|apiVersion', l):
            lines.append('Vulkan: ' + l.strip())
    if not vk:
        lines.append('Vulkan: (vulkaninfo not installed)')
    lines.append('Session: %s, %s' % (os.environ.get('XDG_SESSION_TYPE', '?'), os.environ.get('XDG_CURRENT_DESKTOP', '?')))
    for l in run_text(['xrandr', '--current']).splitlines() if shutil.which('xrandr') else []:
        if '*' in l:
            lines.append('Display mode: ' + ' '.join(l.split()))
    return lines


def runner_desc(inst):
    run = inst.get('run') or {}
    if run.get('proton'):
        ver = read_text(os.path.join(run.get('compatdata') or '', 'version')).strip()
        return 'Proton %s (%s)' % (os.path.basename(run['proton'].rstrip('/')), ver or '?')
    if run.get('cli'):
        return 'Bottles (%s)' % run.get('bottle')
    wine = run.get('wine') if run.get('wine') and os.path.isfile(run['wine']) else shutil.which('wine')
    return 'Wine %s (%s)' % (run_text([wine, '--version']).strip() if wine else 'not found', wine or '-')


def redact(text):
    """User and computer names out: home folder paths, Windows profile paths inside the prefix, $USER, host name."""
    text = text.replace(HOME, '~')
    text = re.sub(r'(?i)([\\/]users[\\/])[^\\/\r\n]+', r'\1<user>', text)
    for word, repl in ((os.environ.get('USER', ''), '<user>'), (os.environ.get('LOGNAME', ''), '<user>'),
                       (os.uname().nodename, '<pc>')):
        if word and len(word) >= 3:
            text = re.sub(re.escape(word), repl, text, flags=re.I)
    return text


def write_parts(folder, base, out, name):
    """Zips of at most MHFZPLUS_CLIP_PART_MB MiB (22: GitHub takes files up to 25 MB): <name>.zip, or <name>-1of3.zip..."""
    import zipfile
    limit = float(os.environ.get('MHFZPLUS_CLIP_PART_MB') or 22) * 1048576
    made, z = [], None
    for root, dirs, names in os.walk(folder):
        dirs.sort()
        for n in sorted(names):
            p = os.path.join(root, n)
            size = os.path.getsize(p)
            guess = size if re.search(r'\.(jpe?g|png|zip|mp4|webm)$', n, re.I) else size // 3   # (pictures don't shrink in a zip)
            if z and z.fp.tell() + guess > limit:
                z.close()
                z = None
            if not z:
                made.append(os.path.join(out, '%s-part%d.zip' % (name, len(made) + 1)))
                z = zipfile.ZipFile(made[-1], 'w', zipfile.ZIP_DEFLATED)
            z.write(p, os.path.relpath(p, base))
    if z:
        z.close()
    final = []
    for i, p in enumerate(made):
        to = os.path.join(out, '%s.zip' % name if len(made) == 1 else '%s-%dof%d.zip' % (name, i + 1, len(made)))
        os.replace(p, to)
        final.append(to)
    return final


def cmd_report(a):
    import fnmatch
    import zipfile
    ui = UI.make(a.unattended)
    st = load_state()
    if not st['installs']:
        ui.error('MHFZ Plus is not installed.', 'MHFZ Plus bug report')
        return 1
    out = a.out or desktop_dir() or HOME
    os.makedirs(out, exist_ok=True)
    stamp = time.strftime('%Y-%m-%d-%H%M')
    lines = ['MHFZ Plus bug report, %s' % time.strftime('%Y-%m-%d %H:%M'), 'MHFZ Plus version: %s' % st.get('version', '?'), '']
    specs = linux_specs()
    lines += specs + ['']
    # the short version for the form's "Your setup" (filled in through the form's link)
    setup = ['MHFZ Plus %s on %s' % (st.get('version', '?'), specs[0])]
    setup += [re.sub(r'^Vulkan: deviceName\s*=\s*', 'Vulkan device: ', l) for l in specs[1:] if re.match(r'CPU|GPU|Vulkan: deviceName|Display mode', l)]
    files, clips = [], []

    def collect(folder, into):
        for e in listdir(folder):
            p = os.path.join(folder, e)
            if os.path.isfile(p) and any(fnmatch.fnmatch(e.lower(), pat) for pat in OURS):
                files.append((p, '%s/%s' % (into, e)))
            elif os.path.isdir(p) and e.lower().startswith('retro'):
                clips.append(p)

    for k, inst in enumerate(st['installs']):
        tag = 'mhfz-plus-%d-%s' % (k + 1, inst['kind']) if len(st['installs']) > 1 else 'mhfz-plus'
        lines.append('%s - %s' % (', '.join(LONG[s] for s in inst['servers']), inst['label']))
        lines.append('  prefix %s, %s' % (inst['prefix'], runner_desc(inst)))
        collect(inst['target'], 'logs/' + tag)
        ini = ini_read(os.path.join(inst['target'], 'MHFZ-Plus.ini'))
        reg = read_text(os.path.join(inst['prefix'], 'user.reg')).lower()
        for s, game in inst['servers'].items():
            folder = game if s == 'meze' else sub(game, 'Client')
            state = new_path(inst['target'], 'fps-state/' + s)
            lines.append('  %s: game folder %s (%s)  english=%s fps=%s fps_mode=%s' % (
                LONG[s], folder, 'found' if folder and os.path.isdir(folder) else 'NOT FOUND',
                ini.get(s, {}).get('english'), ini.get(s, {}).get('fps'), ini.get(s, {}).get('fps_mode')))
            collect(folder, 'logs/%s-game' % s)
            collect(state, 'logs/%s-state' % s)
            dlls = [e + (' (MHFZ Plus)' if our_dll(os.path.join(folder, e)) else ' (NOT MHFZ Plus: another mod?)')
                    for e in listdir(folder) if re.match(r'^(d3d9|dxgi|dinput8|d3d8|ddraw|winmm|version|dsound)\.dll$', e, re.I)]
            lines.append('    proxy DLLs in the game folder: %s' % (', '.join(dlls) or 'none'))
            lines.append('    Wine d3d9 override for %s: %s' % (GAME_EXE[s], 'set' if reg_key(GAME_EXE[s]).lower() in reg else 'MISSING'))
            eng = {'0': 'off', 'safe': 'tested parts only'}.get(str(ini.get(s, {}).get('english', '1')).lower(), 'on')
            setup.append('%s: English %s, frame-rate mod %s (%s, %s)' % (
                LONG[s], eng, 'off' if str(ini.get(s, {}).get('fps', '1')) == '0' else 'on', inst['label'], runner_desc(inst)))
    # the newest clip (one press of Delete: a numbered folder in retro*), if it is from the last week. Its frames are
    # full-size pictures (~100 MB a clip), so it comes in parts GitHub takes (25 MB a file); the names say when it was saved.
    newest = []
    for c in clips:
        subs = [os.path.join(c, e) for e in listdir(c) if e.isdigit() and os.path.isdir(os.path.join(c, e))]
        newest += subs or [c]
    clip = max(newest, key=os.path.getmtime) if newest else None
    clip_zips = []
    if clip and os.path.getmtime(clip) > time.time() - 7 * 86400:
        saved = time.localtime(os.path.getmtime(clip))
        clip_zips = write_parts(clip, os.path.dirname(clip), out, 'MHFZ-Plus-clip-from-' + time.strftime('%Y-%m-%d-%H%M', saved))
        lines.append('\nClip: %s/%s, saved %s -> %d zip(s), %.1f MB' % (
            os.path.basename(os.path.dirname(clip)), os.path.basename(clip), time.strftime('%Y-%m-%d %H:%M', saved),
            len(clip_zips), sum(os.path.getsize(p) for p in clip_zips) / 1048576))
    else:
        lines.append('\nClip: none from the last week (press Delete right after you see a glitch to save one)')
    lines.append('Files collected: %d' % len(files))

    zip_path = os.path.join(out, 'MHFZ-Plus-report-%s.zip' % stamp)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for path, name in files:
            with open(path, 'rb') as f:
                raw = f.read()
            text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8-sig', 'replace')
            z.writestr(name, redact(text).encode('utf-8'))
        z.writestr('report.txt', redact('\n'.join(lines) + '\n').encode('utf-8'))
    msg = "Saved in %s:\n  %s   (logs, settings and your PC's specs)" % (short(out), os.path.basename(zip_path))
    if clip_zips:
        msg += ''.join('\n  ' + os.path.basename(p) for p in clip_zips)
        msg += '\n  (the newest clip, saved %s%s. Add it only if it shows this problem.)' % (
            time.strftime('%Y-%m-%d %H:%M', saved), ', in %d parts: drag them in together' % len(clip_zips) if len(clip_zips) > 1 else '')
    msg += ('\n\nYour user name in file paths was replaced with <user>.\n\nOpen the bug report form on GitHub now? '
            'Your setup is filled in: write what happened and drag the zip file(s) into it (their folder opens too).')
    form = ISSUE_URL + '&setup=' + urllib.parse.quote(redact('\n'.join(setup)), safe='')
    if ui.ask(msg, ui.mode != 'none', 'MHFZ Plus bug report') and shutil.which('xdg-open'):
        for target in (out, form):   # (the zip's folder, to drag it from, and the form)
            subprocess.Popen(['xdg-open', target], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if ui.mode == 'none':
        ui.info(msg.split('\n\nOpen the')[0] + '\n\nThe bug report form, with your setup filled in:\n' + form, 'MHFZ Plus bug report')
    return 0


# ------------------------------------------------------------------------------------------------ restore, uninstall
def restore_all(st):
    """(lines, problems) - refuses a prefix that is in use."""
    lines, problems = [], []
    for inst in st['installs']:
        if prefix_in_use(inst['prefix']):
            problems.append('%s: the game or MHFZ Plus is still running - skipped' % inst['label'])
            continue
        ini = ini_read(os.path.join(inst['target'], 'MHFZ-Plus.ini'))
        for s, game in inst['servers'].items():
            folder = game if s == 'meze' else sub(game, 'Client')
            if not folder or not os.path.isdir(folder):
                continue
            swap = (ini.get(s, {}).get('fps_mode') or ('stay' if s == 'meze' else 'swap')).lower() == 'swap'
            n, p, notes = restore_server(inst['target'], s, folder, swap)
            problems += ['%s: %s' % (LONG[s], x) for x in p]
            lines.append('%s: %d original files in place, frame-rate mod removed' % (LONG[s], n))
            lines += ['%s: %s' % (LONG[s], x) for x in notes]
    return lines, problems


def cmd_restore(a):
    ui = UI.make(a.unattended)
    st = load_state()
    lines, problems = restore_all(st)
    if problems:
        ui.error('Not everything could be put back:\n\n%s' % '\n'.join(problems + lines), 'MHFZ Plus')
        return 1
    ui.info('The original game files are back and the frame-rate mod is out.\n\n%s\n\n(Starting the game through '
            'MHFZ Plus puts them in again.)' % '\n'.join(lines), 'MHFZ Plus')
    return 0


def cmd_uninstall(a):
    ui = UI.make(a.unattended or a.yes)
    title = 'Uninstall MHFZ Plus'
    if ui.mode == 'none' and not (a.yes or a.unattended):   # (no window and no terminal to ask in)
        ui.error('Run "%s uninstall" in a terminal (or install zenity).' % WRAPPER, title)
        return 1
    st = load_state()
    if not a.yes and not ui.ask('Remove MHFZ Plus?\n\nYour original game files are put back first.', True, title):
        return 0
    if a.wait:   # (started from the MHFZ Plus app, which closes itself: give it and its Wine time to go)
        for _ in range(40):
            if not any(prefix_in_use(i['prefix']) for i in st['installs']):
                break
            time.sleep(1)
    for inst in st['installs']:
        if prefix_in_use(inst['prefix']):
            ui.error('Something is still running in %s (the game, its launcher or MHFZ Plus). Close it first, then try '
                     'again. Nothing was removed.' % inst['label'], title)
            return 1
    # 1. originals back, mod out
    lines, problems = restore_all(st)
    if problems:
        ui.error('Could not put all original files back. Nothing was removed.\n\n%s' % '\n'.join(problems), title)
        return 1
    # 2. per prefix: the mod's settings left in game folders, the Wine override, the MHFZ Plus folder
    for inst in st['installs']:
        for s, game in inst['servers'].items():
            folder = game if s == 'meze' else sub(game, 'Client')
            if folder and os.path.isdir(folder) and not child(folder, 'd3d9.dll'):
                for f in ('mhfz-fps.ini', 'mhfz-fps.log'):
                    p = child(folder, f)
                    if p:
                        os.remove(p)
        try:
            for exe, prev in (inst.get('reg') or {}).items():
                reg_override(inst['prefix'], exe, prev)
            os.remove(os.path.join(inst['prefix'], 'user.reg.mhfz-plus-backup'))   # (only needed while installed)
        except OSError:
            pass
        shutil.rmtree(inst['target'], ignore_errors=True)
    # 3. Steam entries (every one of ours, also from earlier installs; Steam closed for it), menu entries, this script
    note = ''
    ours = {}
    for root in steam_roots():
        for cfg in steam_user_configs(root):
            for e in (ci_get(read_shortcuts(cfg) or {}, 'shortcuts') or {}).values():
                if isinstance(e, dict) and our_shortcut(e) and ci_get(e, 'appid') is not None:
                    ours.setdefault(root, []).append(unsigned(ci_get(e, 'appid')))
    if ours:
        ok, restart = steam_close(ui, list(ours)[0], 'remove the MHFZ Plus games from it')
        if ok:
            for root, ids in ours.items():
                for cfg in steam_user_configs(root):
                    try:
                        remove_shortcuts(cfg)
                    except SteamFileUnknown as e:
                        note = '\n\n%s Remove the MHFZ Plus entries in Steam yourself: right-click them > Manage > Remove non-Steam game.' % e
                remove_compat(root, ids)   # (only our shortcuts' ids: the Proton set for them)
            if restart:
                steam_start(list(ours)[0])
        else:
            note = '\n\nThe Steam entries are still there: right-click them in Steam > Manage > Remove non-Steam game.'
    for p in st.get('entries', []):
        if os.path.basename(p).startswith('mhfz-plus') and os.path.exists(p):
            os.remove(p)
    if shutil.which('update-desktop-database'):
        subprocess.call(['update-desktop-database', '-q', APPS], stderr=subprocess.DEVNULL)
    shutil.rmtree(DATA, ignore_errors=True)
    damaged = [x for x in lines if 'is damaged' in x]
    ui.info('MHFZ Plus has been removed. Your game folders are back to the original files' +
            (', except:\n\n%s' % '\n'.join(damaged) if damaged else '.') + note, title)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog='mhfz-plus', description='MHFZ Plus for Linux')
    sp = ap.add_subparsers(dest='cmd')
    i = sp.add_parser('install', help='install or update (from the extracted package)')
    i.add_argument('--unattended', action='store_true', help='no questions: every game found, the default options')
    i.add_argument('--terminal', action='store_true', help='questions in the terminal even on a desktop')
    i.add_argument('--game', action='append', default=[], help='a game folder inside a prefix (several allowed)')
    i.add_argument('--prefix', action='append', default=[], help='also look in this Wine prefix')
    i.add_argument('--only-prefix', help='only this Wine prefix')
    for o in ('fps', 'english', 'dxvk', 'desktop', 'steam'):
        i.add_argument('--no-' + o, action='store_true')
    p = sp.add_parser('play', help='start the game through MHFZ Plus')
    p.add_argument('server', choices=SERVERS)
    for name in ('app', 'settings'):   # (settings: the name before 1.4)
        q = sp.add_parser(name, help='the MHFZ Plus app: play, settings, help' if name == 'app' else None)
        q.add_argument('server', nargs='?', choices=SERVERS)
    for name in ('folder', 'restore'):
        sp.add_parser(name)
    r = sp.add_parser('report', help='logs, settings and PC specs in a zip, for a bug report')
    r.add_argument('--out', help='where the zips go (default: the desktop)')
    r.add_argument('--unattended', action='store_true', help='no questions')
    u = sp.add_parser('uninstall')
    u.add_argument('--yes', action='store_true', help='no questions')
    u.add_argument('--wait', action='store_true', help='wait for the game prefixes to be idle first (from the app)')
    a = ap.parse_args(argv)
    a.unattended = getattr(a, 'unattended', False)
    if not a.cmd:
        ap.print_help()
        return 2
    return {'install': cmd_install, 'play': cmd_play, 'app': cmd_app, 'settings': cmd_app, 'folder': cmd_folder,
            'restore': cmd_restore, 'report': cmd_report, 'uninstall': cmd_uninstall}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main())
