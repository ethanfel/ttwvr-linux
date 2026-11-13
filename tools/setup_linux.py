#!/usr/bin/env python3
"""Patch a prepared TTWVR/Fluorine install. No game/mod downloads or bundled DLLs.

Usage: setup_linux.py {check,prepare,apply} setup.json
       setup_linux.py rollback /path/to/fixes/transaction.json [--force]
See docs/installation.md. Run as your desktop user, never with sudo.
"""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request

from experimental_author_dlss import build, BUILDS
from experimental_native_fsr import SOURCE_SHA256_114
from patch_openxr_bridge import patch_bytes
from proton_builds import identify_proton

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf0a0d90f8965147741ad17dcd0b263817572ab3'
PATCHES = ('dxvk-32bit-mapped-pool.patch', 'dxvk-ttwvr-opaque-fences.patch')
COMPILERS = {
    '32': ('d3dcompiler_47_32.dll', '2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851'),
    '64': ('d3dcompiler_47.dll', '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3'),
}
KNOWN_RENDERER = {
    'd3d9.dll': '2d7337a3ae9ab62399dd0daa92aee394db2c63c45f11b3039738167d4c4f21d1',
    'd3d11.dll': '09e23a2003e03d616858e0c2ded02456b4908b3e17fb66f760ff507ec38e753c',
    'dxgi.dll': 'fab463d85426681b9692f0a97337d19ef5a9f230f50025f6110868e0576dd9e7',
}
REQUIRED = ('instance', 'mod', 'manager', 'proton', 'prefix', 'steam_runtime',
            'config_home', 'data_home', 'fixes')


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def read_config(path):
    raw = json.loads(Path(path).read_text())
    for key in REQUIRED:
        if not raw.get(key):
            raise ValueError(f'missing setup.json path: {key}')
    c = dict(raw)
    for key in (*REQUIRED, 'xrizer', 'xr_runtime', 'dxvk_dir', 'compiler32', 'compiler64'):
        default = {'xrizer': '/opt/xrizer', 'xr_runtime': '/usr/share/openxr/1/openxr_wivrn.json'}.get(key)
        value = raw.get(key, default)
        if value is not None:
            p = Path(value).expanduser()
            if not p.is_absolute():
                raise ValueError(f'{key}: use an absolute path')
            if any(ch in str(p) for ch in '\n\r\0:"\\'):
                raise ValueError(f'{key}: unsupported path character (spaces are supported)')
            c[key] = p.resolve()
    c.setdefault('profile', 'TTW - VR')
    c.setdefault('executable', 'Tale of Two Wastelands')
    if Path(c['profile']).name != c['profile'] or c['profile'] in ('.', '..'):
        raise ValueError('profile must be a single directory name')
    return c


def ini(path):
    cp = configparser.RawConfigParser(strict=False)
    cp.optionxform = str
    cp.read_string(Path(path).read_text(encoding='utf-8-sig'))
    return cp


def qt_value(value):
    """Encode one QSettings INI string, including QProcess double quotes."""
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def unqt(value):
    if value.startswith('@ByteArray(') and value.endswith(')'):
        value = value[11:-1]
    if value.startswith('"') and value.endswith('"'):
        value = value[1:-1].replace('\\"', '"')
    return value.replace('\\\\', '\\')


def edit_ini(data, updates):
    """Edit only specified keys, retaining all unrelated Qt settings verbatim."""
    text = data.decode('utf-8-sig')
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    for section, values in updates.items():
        headers = [i for i, line in enumerate(lines) if line.strip() == f'[{section}]']
        if len(headers) > 1:
            raise ValueError(f'duplicate INI section: {section}')
        if not headers:
            lines += ['', f'[{section}]']
            headers = [len(lines)-1]
        start = headers[0] + 1
        end = next((i for i in range(start, len(lines)) if lines[i].startswith('[')), len(lines))
        for key, value in values.items():
            matches = [i for i in range(start, end) if lines[i].partition('=')[0].strip() == key]
            if len(matches) > 1:
                raise ValueError(f'duplicate INI key: {section}/{key}')
            replacement = key + '=' + str(value)
            if matches:
                lines[matches[0]] = replacement
            else:
                lines.insert(end, replacement)
                end += 1
    return (newline.join(lines) + newline).encode('utf-8')


def active_processes():
    found = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            args = (entry / 'cmdline').read_bytes().split(b'\0')
            exe = args[0].decode(errors='replace').replace('\\', '/').rsplit('/', 1)[-1].lower()
            if exe in {'falloutnv.exe', 'ttwvr_dlss_host.exe', 'modorganizer.exe',
                       'modorganizer-core', 'fluorine-manager'}:
                found.append(f'{exe} (PID {entry.name})')
        except (OSError, IndexError):
            pass
    return found


def require_closed():
    active = active_processes()
    if active:
        raise ValueError('close Fallout and Fluorine first: ' + ', '.join(active))


def check(c):
    errors, notes = [], []
    def verify(label, fn):
        try:
            fn()
        except (OSError, ValueError, KeyError, configparser.Error) as exc:
            errors.append(f'{label}: {exc}')
    def need(path):
        if not path.is_file():
            raise ValueError(f'required file missing: {path}')
    def hash_is(path, expected):
        if digest(path) != expected:
            raise ValueError(f'unsupported or modified file: {path}')
    for p in (c['manager'], c['instance']/'ModOrganizer.ini', c['proton']/'proton',
              c['prefix']/'system.reg', c['steam_runtime']/'_v2-entry-point', c['xr_runtime'],
              c['xrizer']/'bin/linux64/vrclient.so',
              c['prefix']/'drive_c/windows/syswow64/kernel32.dll',
              c['instance']/'profiles'/c['profile']/'modlist.txt'):
        verify('preparation', lambda p=p: need(p))
    for command in ('bwrap', 'fusermount3', 'unshare'):
        if not shutil.which(command):
            errors.append(f'install required command: {command}')
    if not os.access(c['manager'], os.X_OK):
        errors.append('manager launcher is not executable')
    if c['prefix'].name != 'pfx':
        errors.append('prefix must be the initialized private Proton .../pfx directory')
    for path in (c['instance'], c['mod'], c['prefix'], c['proton']):
        if c['fixes'] == path or c['fixes'].is_relative_to(path) or path.is_relative_to(c['fixes']):
            errors.append('fixes must be a separate directory outside instance, mod, prefix and Proton')
    if c['mod'].parent != c['instance']/'mods':
        errors.append('mod must be an installed mod directory immediately under instance/mods')
    plugin = c['mod']/'NVSE/Plugins/TTWVR.dll'
    def check_mod():
        h = digest(plugin)
        if h not in BUILDS:
            raise ValueError('install the original, unpatched TTWVR 1.1.3 or 1.1.4 archive first')
        c['version'] = '1.1.4' if h == SOURCE_SHA256_114 else '1.1.3'
        c['new_mod'] = c['instance']/'mods'/('TTWVR Linux ' + c['version'])
        if c['new_mod'].exists():
            raise ValueError(f'new mod destination already exists: {c["new_mod"]}')
        hashes = {
            'ttwvr_dxvk_d3d9.dll': 'c7f64a18da8fd5f455e9453ade4abcfaef9dec63cc1cb80a89215a12b6131cf2',
            'nvngx_dlss.dll': '3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983',
            'ttwvr_dlss_host.exe': ('194676411d6161b4420d753cfae20f9d1b308d0b99a8b5226ad9ef2e9f44ef38'
                if c['version']=='1.1.4' else 'c9122a1b6464f04ecdcd9760f1c7a8593ae69ea9e6814ad6e82a3f9af2b73b10'),
        }
        for name, expected in hashes.items():
            hash_is(c['mod']/'NVSE/Plugins/TTWVR'/name, expected)
        need(c['mod']/'NVSE/Plugins/TTWVR/TTWVR.ini')
        # Reproduce all exact-byte patches during preflight, without writing.
        build(plugin.read_bytes(), native_fsr=True)
    verify('TTWVR', check_mod)
    def check_proton():
        profile = identify_proton(c['proton'])
        notes.append('Proton: ' + profile['version'] +
                     ('; built-in OpenXR fix' if profile['openxr_builtin'] else '; private OpenXR workaround'))
    verify('Proton runtime and DXVK', check_proton)
    def check_instance():
        cp = ini(c['instance']/'ModOrganizer.ini')
        for key, path in (('prefix_path', c['prefix']), ('proton_path', c['proton'])):
            if Path(unqt(cp.get('fluorine', key))).resolve() != path:
                raise ValueError(f'instance {key} differs from setup.json')
        if cp.get('fluorine', 'vfs_backend', fallback='') != 'fuse':
            raise ValueError('select the FUSE backend in Fluorine')
        game = unqt(cp.get('General', 'gamePath')).replace('\\', '/')
        if not game.startswith('Z:/'):
            raise ValueError('prepared gamePath must use Wine Z: mapping to a Linux path')
        c['game'] = Path(game[2:]).resolve()
        need(c['game']/'FalloutNV.exe')
        for folder in (c['game']/'Data/NVSE/Plugins', c['mod']/'NVSE/Plugins'):
            if folder.is_dir():
                for file in folder.iterdir():
                    if file.name.lower() in {'ttwvrrenderingaddon.dll','ttwvrneuralmenu.dll'}:
                        raise ValueError('move the archived addon out of the base game/TTWVR mod: '+str(file))
        exe = cp['customExecutables']
        ids = [k.split('\\')[0] for k,v in exe.items() if k.endswith('\\title') and unqt(v)==c['executable']]
        if len(ids) != 1:
            raise ValueError('configure exactly one executable named ' + c['executable'])
        c['exe_id'] = ids[0]
        binary = unqt(exe.get(ids[0]+'\\binary', '')).replace('\\', '/')
        if binary.lower().rsplit('/',1)[-1] != 'falloutnv.exe':
            raise ValueError('prepared executable must launch the 4GB-patched FalloutNV.exe')
        if binary != 'Z:' + str(c['game']/'FalloutNV.exe'):
            raise ValueError('executable binary does not match the managed game path')
        if exe.get(ids[0]+'\\useProton', '').lower() != 'true':
            raise ValueError('enable Proton for the game executable')
        global_path = c['config_home']/'fluorine/config.json'
        if global_path.exists():
            global_config = json.loads(global_path.read_text())
            for key in ('prefix_path','proton_path'):
                if key in global_config and Path(global_config[key]).resolve() != Path(unqt(cp.get('fluorine', key))).resolve():
                    raise ValueError('Fluorine global config overrides '+key+' with another path')
    verify('Fluorine', check_instance)
    def check_modlist():
        lines = (c['instance']/'profiles'/c['profile']/'modlist.txt').read_text(encoding='utf-8-sig').splitlines()
        if '+' + c['mod'].name not in lines:
            raise ValueError('enable the prepared TTWVR mod in the selected VR profile first')
        for line in lines:
            if not line.startswith('+'):
                continue
            folder = c['instance']/'mods'/line[1:]
            if not folder.is_dir():
                raise ValueError('enabled mod folder missing: '+str(folder))
            for file in folder.rglob('*'):
                if file.is_symlink():
                    raise ValueError('Fluorine skips symlinked mod assets; use copies/reflinks: '+str(file))
        # An overwrite entry would beat the newly patched mod and its original menu.
        overwrite = c['instance']/'overwrite'
        if overwrite.exists():
            for file in overwrite.rglob('*'):
                if file.name.lower() in {'ttwvr.dll', 'ttwvr.ini', 'ttwvrrenderingmenu.dll', 'ttwvrneuralmenu.dll', 'ttwvrrenderingaddon.dll'}:
                    raise ValueError('move conflicting overwrite file into its own mod first: '+str(file))
    verify('profile assets', check_modlist)
    if (c['fixes']/'transaction.json').exists():
        errors.append('this fixes directory already has a transaction; use a new fixes directory or roll it back')
    for bits in COMPILERS:
        if c.get('compiler'+bits):
            verify('compiler'+bits, lambda b=bits: hash_is(c['compiler'+b], COMPILERS[b][1]))
    if c.get('dxvk_dir'):
        verify('prebuilt renderer', lambda: validate_renderer(c['dxvk_dir'], known_only=True))
    elif (c['fixes']/'dependencies/dxvk').exists():
        verify('cached renderer', lambda: validate_renderer(c['fixes']/'dependencies/dxvk'))
    else:
        for command in ('git','meson','ninja','glslangValidator','i686-w64-mingw32-gcc','i686-w64-mingw32-g++','i686-w64-mingw32-windres'):
            if not shutil.which(command):
                errors.append(f'install build prerequisite: {command}')
        notes.append('prepare/apply will build the pinned DXVK source with both patches')
    notes += [f'VR profile: {c["instance"]}/profiles/{c["profile"]}',
              f'Private prefix: {c["prefix"]}',
              f'Patch output/backups: {c["fixes"]}',
              'Original mod, saves and installed Proton are retained.',
              f'OpenXR manifest: {c["xr_runtime"]}; default runtime registration is unchanged.']
    return errors, notes


def validate_renderer(directory, known_only=False):
    hashes = json.loads((directory/'hashes.json').read_text())
    if set(hashes) != set(KNOWN_RENDERER):
        raise ValueError('renderer manifest must list d3d9.dll, d3d11.dll and dxgi.dll')
    if known_only and hashes != KNOWN_RENDERER:
        raise ValueError('unknown prebuilt renderer; omit dxvk_dir to build from pinned source')
    for name, expected in hashes.items():
        if digest(directory/name) != expected:
            raise ValueError('renderer checksum mismatch: '+name)
    if not known_only:
        provenance = json.loads((directory/'build.json').read_text())
        if provenance != {'revision': REVISION, 'patches': {p:digest(ROOT/'patches'/p) for p in PATCHES}}:
            raise ValueError('renderer build metadata differs; use a fresh fixes directory')


def run(command):
    print('+ ' + shlex.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def atomic_write(path, data, mode=0o644):
    path = Path(path)
    fd, name = tempfile.mkstemp(prefix='.'+path.name+'.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.chmod(name, mode)
        os.replace(name, path)  # replace a link itself, never its target
    finally:
        if os.path.lexists(name):
            os.unlink(name)


def write_json(path, obj):
    atomic_write(path, (json.dumps(obj, indent=2)+'\n').encode())


def prepare(c):
    deps = c['fixes']/'dependencies'
    deps.mkdir(parents=True, exist_ok=True)
    for bits, (remote, expected) in COMPILERS.items():
        dst = deps/('compiler'+bits+'.dll')
        if dst.exists():
            if digest(dst) != expected:
                raise ValueError('bad cached compiler: '+str(dst))
            continue
        if c.get('compiler'+bits):
            data = c['compiler'+bits].read_bytes()
        else:
            url = 'https://raw.githubusercontent.com/mozilla/fxc2/master/dll/'+remote
            print('Downloading checksum-pinned shader compiler '+bits, flush=True)
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(20*1024*1024)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('compiler checksum mismatch; no installation files changed')
        atomic_write(dst, data)
    renderer = deps/'dxvk'
    if renderer.exists():
        validate_renderer(renderer, known_only=bool(c.get('dxvk_dir')))
        return
    if c.get('dxvk_dir'):
        validate_renderer(c['dxvk_dir'], known_only=True)
        shutil.copytree(c['dxvk_dir'], renderer)
        return
    source = deps/'dxvk-src'
    if source.exists():
        raise ValueError('incomplete prior DXVK build; remove only '+str(source)+' and '+str(deps/'build32')+' to retry')
    run(['git','clone','--no-checkout','https://github.com/doitsujin/dxvk.git',source])
    run(['git','-C',source,'checkout','--detach',REVISION])
    run(['git','-C',source,'submodule','update','--init','--recursive'])
    for patch in PATCHES:
        run(['git','-C',source,'apply',ROOT/'patches'/patch])
    build_dir = deps/'build32'
    run(['meson','setup',build_dir,source,'--cross-file',source/'build-win32.txt','--buildtype','release',
         '-Denable_d3d8=false','-Denable_d3d10=false','-Dbuild_id=true'])
    run(['ninja','-C',build_dir,'-j',str(min(os.cpu_count() or 2, 8))])
    staged = deps/'dxvk-staged'
    staged.mkdir()
    for stem in ('d3d9','d3d11','dxgi'):
        shutil.copy2(build_dir/'src'/stem/(stem+'.dll'),staged/(stem+'.dll'))
    write_json(staged/'hashes.json',{n:digest(staged/n) for n in KNOWN_RENDERER})
    write_json(staged/'build.json',{'revision':REVISION,'patches':{p:digest(ROOT/'patches'/p) for p in PATCHES}})
    staged.rename(renderer)


class Transaction:
    """Write-ahead file journal; preserves symlinks and refuses rollback conflicts."""
    def __init__(self, manifest):
        self.path = Path(manifest)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.exists():
            raise ValueError('transaction already exists')
        (self.path.parent/'backups').mkdir()
        self.data = {'format':1,'status':'applying','files':[]}
        write_json(self.path,self.data)

    def replace(self, path, data, mode=None):
        path = Path(path).absolute()
        entry = {'path':str(path), 'after':hashlib.sha256(data).hexdigest()}
        if path.is_symlink():
            entry.update(kind='symlink',target=os.readlink(path),before=None)
        elif path.exists():
            backup = self.path.parent/'backups'/str(len(self.data['files']))
            shutil.copy2(path,backup)
            entry.update(kind='file',backup=str(backup),before=digest(backup),mode=path.stat().st_mode & 0o777)
        else:
            entry.update(kind='absent',before=None)
        self.data['files'].append(entry)
        write_json(self.path,self.data)
        atomic_write(path,data, mode if mode is not None else entry.get('mode',0o644))

    def finish(self, **details):
        self.data.update(details,status='applied')
        write_json(self.path,self.data)


def before_matches(entry):
    path = Path(entry['path'])
    if entry['kind']=='symlink':
        return path.is_symlink() and os.readlink(path)==entry['target']
    if entry['kind']=='absent':
        return not os.path.lexists(path)
    return path.is_file() and not path.is_symlink() and digest(path)==entry['before']


def rollback(manifest, force=False):
    manifest = Path(manifest)
    data = json.loads(manifest.read_text())
    if data.get('format') != 1:
        raise ValueError('unsupported rollback journal')
    if data['status']=='rolled_back':
        return
    # Validate every backup and conflict before restoring any file.
    conflicts = []
    for entry in data['files']:
        if entry['kind']=='file' and digest(entry['backup'])!=entry['before']:
            raise ValueError('rollback backup changed: '+entry['backup'])
        path = Path(entry['path'])
        if before_matches(entry):
            continue
        if not path.is_file() or path.is_symlink() or digest(path)!=entry['after']:
            conflicts.append(entry)
    if conflicts and not force:
        raise ValueError('files changed since apply: '+', '.join(e['path'] for e in conflicts)+
                         '; preserve edits manually or use rollback --force (also saves current files)')
    if conflicts:
        backup = Path(tempfile.mkdtemp(prefix='before-forced-rollback-',dir=manifest.parent))
        for i, entry in enumerate(conflicts):
            path = Path(entry['path'])
            if os.path.lexists(path):
                shutil.copy2(path,backup/str(i),follow_symlinks=False)
        write_json(backup/'paths.json',conflicts)
    for entry in reversed(data['files']):
        path = Path(entry['path'])
        if before_matches(entry):
            continue
        if entry['kind']=='file':
            atomic_write(path,Path(entry['backup']).read_bytes(),entry['mode'])
        elif entry['kind']=='symlink':
            temp = path.with_name('.'+path.name+'.restore-link')
            temp.symlink_to(entry['target']); os.replace(temp,path)
        else:
            path.unlink(missing_ok=True)
    data['status']='rolled_back'
    write_json(manifest,data)
    print('Restored original configuration and compiler files. Private mod/build files retained; saves untouched.')


def patched_modlist(data, c):
    lines = data.decode('utf-8-sig').splitlines()
    result = []
    for line in lines:
        if line.startswith('+'):
            mod = c['instance']/'mods'/line[1:]
            # Disable all previous TTWVR versions and the archived menu addons.
            if (mod/'NVSE/Plugins/TTWVR.dll').exists() or any((mod/'NVSE/Plugins'/name).exists() for name in ('TTWVRRenderingAddon.dll','TTWVRNeuralMenu.dll')) or any(x in line.lower() for x in ('ttwvr rendering addon','ttwvr neural menu')):
                line = '-'+line[1:]
        result.append(line)
    # MO2/Fluorine modlist is descending priority: first mod wins conflicts.
    result.insert(1 if result and result[0].startswith('#') else 0,'+'+c['new_mod'].name)
    return ('\r\n'.join(result)+'\r\n').encode()


def prepare_openxr(proton, fixes):
    """Use the verified built-in fix, or generate the Experimental-only overlay."""
    profile = identify_proton(proton)
    if profile['openxr_builtin']:
        return None
    bridge = fixes/'wineopenxr.so'
    atomic_write(bridge,patch_bytes((proton/'files/lib/wine/x86_64-unix/wineopenxr.so').read_bytes()),0o755)
    return bridge


def apply(c):
    require_closed()
    prepare(c)
    require_closed()
    # Finish expensive preparation before touching any existing configuration.
    private = c['fixes']/'syswow64'
    if private.exists():
        raise ValueError('private syswow64 already exists; use a fresh fixes directory')
    shutil.copytree(c['prefix']/'drive_c/windows/syswow64',private,symlinks=True)
    atomic_write(private/'d3dcompiler_47.dll',(c['fixes']/'dependencies/compiler32.dll').read_bytes())
    bridge = prepare_openxr(c['proton'], c['fixes'])
    shutil.copytree(c['mod'], c['new_mod'], symlinks=False)
    plugin = c['new_mod']/'NVSE/Plugins/TTWVR.dll'
    atomic_write(plugin,bytes(build(plugin.read_bytes(),native_fsr=True)))
    # Keep the user’s original mod INI and assets exactly, including calibration.
    wrapper_args = [sys.executable,str(ROOT/'tools/steam_mo2.py'),'--native-instance',str(c['instance']),
                    '--state-dir',str(c['fixes']/'state'),'--steam-runtime',str(c['steam_runtime']),
                    '--xrizer',str(c['xrizer']),'--xr-runtime',str(c['xr_runtime']),'--']
    wrapper = ' '.join('"'+a+'"' for a in wrapper_args)+' %command%'
    organizer = c['instance']/'ModOrganizer.ini'
    organizer_data = edit_ini(organizer.read_bytes(),{'customExecutables':{c['exe_id']+'\\wrapperOptions':qt_value(wrapper)}})
    compatibility = c['instance']/'ttwvr-linux.ini'
    compatibility_data = edit_ini(compatibility.read_bytes() if compatibility.exists() else b'',{'VR':{
        'author_renderer':'true','neural_rendering':'false','wow64':'true','native_d3dcompiler47':'true',
        'openxr_bridge':str(bridge) if bridge else '','dxvk_renderer':str(c['fixes']/'dependencies/dxvk'),
        'dxvk_syswow64':str(private),'fallback_resolution_source':'','fallback_resolution_patch':''}})
    launch_args = [sys.executable,str(ROOT/'tools/launch_fluorine.py'),'--manager',str(c['manager']),
                   '--instance',str(c['instance']),'--config-home',str(c['config_home']),
                   '--data-home',str(c['data_home']),'--profile',c['profile'],'--executable',c['executable']]
    launcher = c['fixes']/'launch-ttwvr.py'
    launcher_data = ('#!/usr/bin/env python3\nimport os,sys\n'
        'sys.path.insert(0,'+repr(str(ROOT/'tools'))+')\n'
        'from setup_linux import require_closed\n'
        'if "--dry-run" not in sys.argv: require_closed()\n'
        'env=os.environ.copy()\n'
        'for key in list(env):\n'
        '    if key.startswith(("SIMULATED_","XRT_COMPOSITOR_NULL","TF2VR_TEST_","U_PACING_")): env.pop(key,None)\n'
        'command='+repr(launch_args)+'+sys.argv[1:]\n'
        'os.execvpe(command[0],command,env)\n').encode()
    require_closed()
    tx = Transaction(c['fixes']/'transaction.json')
    try:
        for bits, directory in (('32','syswow64'),('64','system32')):
            tx.replace(c['prefix']/'drive_c/windows'/directory/'d3dcompiler_47.dll',
                       (c['fixes']/('dependencies/compiler'+bits+'.dll')).read_bytes())
        tx.replace(compatibility,compatibility_data)
        modlist = c['instance']/'profiles'/c['profile']/'modlist.txt'
        tx.replace(modlist,patched_modlist(modlist.read_bytes(),c))
        tx.replace(organizer,organizer_data)
        tx.replace(launcher,launcher_data,0o755)
        tx.finish(version=c['version'],new_mod=str(c['new_mod']),plugin_sha256=digest(plugin))
    except BaseException:
        rollback(tx.path)
        raise
    steam = 'python3 '+shlex.quote(str(launcher))+' -- %command%'
    atomic_write(c['fixes']/'steam-launch-options.txt',(steam+'\n').encode())
    print('\nApplied TTWVR '+c['version']+' Linux patch. Connect WiVRn, then run:\n'+
          shlex.join(['python3',str(launcher)])+'\n\nSteam launch options:\n'+steam+
          '\n\nRollback:\n'+shlex.join(['python3',str(ROOT/'tools/setup_linux.py'),'rollback',str(tx.path)]))


def main():
    p = argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('action',choices=('check','prepare','apply','rollback'))
    p.add_argument('config',type=Path,help='setup.json, or transaction.json for rollback')
    p.add_argument('--force',action='store_true',help='rollback only: preserve current edits, then restore backups')
    a = p.parse_args()
    try:
        if os.geteuid()==0:
            raise ValueError('run as your desktop user, not root/sudo')
        if a.action=='rollback':
            require_closed(); rollback(a.config,a.force); return 0
        if a.force:
            raise ValueError('--force applies only to rollback')
        c = read_config(a.config)
        errors, notes = check(c)
        for message in notes: print('INFO: '+message)
        for message in errors: print('ERROR: '+message,file=sys.stderr)
        if errors: return 1
        print('Preflight passed: TTWVR '+c['version']+', supported Proton, prepared Fluorine profile.')
        if a.action=='check':
            active = active_processes()
            if active: print('Before apply, close: '+', '.join(active))
        elif a.action=='prepare': prepare(c)
        else: apply(c)
        return 0
    except (OSError,ValueError,KeyError,configparser.Error,subprocess.CalledProcessError) as exc:
        print('ERROR: '+str(exc),file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
