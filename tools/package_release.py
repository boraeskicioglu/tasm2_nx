#!/usr/bin/env python3
"""Package only reviewed wrapper files; game inputs are never traversed."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile
from verify_artifacts import verify

DOCS = ['README.md', 'LICENSE', 'THIRD_PARTY.md', 'TECHNICAL_NOTES.md',
        'REFERENCE_AUDIT.md', 'RELEASE_NOTES_v1.0.0.md', 'VALIDATION.md', 'BUILD_REPORT.md']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(path, pairs):
    names = set()
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for source, name in pairs:
            if name in names or '..' in Path(name).parts:
                raise ValueError('Unsafe or duplicate archive entry: ' + name)
            names.add(name)
            info = zipfile.ZipInfo.from_file(source, arcname=name)
            info.create_system = 3
            info.external_attr = (0o100755 if source.suffix == ".sh" else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, source.read_bytes(), compresslevel=9)
    with zipfile.ZipFile(path) as check:
        if check.testzip() is not None:
            raise ValueError('Archive integrity failed')
        if set(check.namelist()) != names:
            raise ValueError('Archive entry mismatch')


def git_files(root, git):
    result = subprocess.check_output([git, '-C', str(root), 'ls-files', '-z'])
    return [n for n in result.decode('utf-8').split('\0') if n]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source', action='store_true', help='Also archive indexed wrapper and pinned runtime source')
    parser.add_argument('--git', default='git')
    args = parser.parse_args()
    root = args.root.resolve()
    report = verify(root)
    release = root / 'release'
    release.mkdir(exist_ok=True)
    notices = [(f, 'licenses/' + f.name) for f in sorted((root / 'licenses').iterdir()) if f.is_file()]
    pairs = [(root / 'launcher/tasm2_nx.nro', 'switch/tasm2_nx/tasm2_nx.nro')]
    pairs += [(root / n, n) for n in DOCS] + notices
    binary = release / 'tasm2_nx_v1.0.0.zip'
    archive(binary, pairs)
    archives = {binary.name: {'bytes': binary.stat().st_size, 'sha256': sha(binary)}}
    if args.source:
        names = git_files(root, args.git)
        allowed_roots = {'source', 'tests', 'tools', 'licenses', 'launcher', 'runtime'}
        allowed_files = set(DOCS) | {'.gitignore', '.gitattributes', '.gitmodules', 'Makefile', 'build.sh'}
        source_pairs = []
        for name in names:
            if name == 'runtime':
                continue
            parts = Path(name).parts
            if name not in allowed_files and parts[0] not in allowed_roots:
                raise ValueError('Unexpected indexed source file: ' + name)
            if Path(name).suffix.lower() in {'.apk', '.obb', '.so', '.sav', '.nro', '.nsp', '.elf', '.dex', '.log'}:
                raise ValueError('Forbidden indexed file: ' + name)
            if parts[0] == 'launcher' and name not in {'launcher/Makefile', 'launcher/build.sh', 'launcher/icon.jpg'}:
                raise ValueError('Unexpected launcher source entry: ' + name)
            source_pairs.append((root / name, 'tasm2_nx/' + name))
        # A source upload may vendor the pinned runtime as ordinary files.
        # In a development checkout it is a separate Git submodule instead.
        # Do not run ls-files from a vendored folder: Git would return the
        # parent index and prepend runtime/ to unrelated wrapper filenames.
        runtime_names = [] if any(n.startswith('runtime/') for n in names) else git_files(root / 'runtime', args.git)
        for name in runtime_names:
            if Path(name).suffix.lower() in {'.apk', '.obb', '.so', '.sav', '.nro', '.nsp', '.elf', '.dex', '.log'}:
                raise ValueError('Unexpected runtime content: ' + name)
            source_pairs.append((root / 'runtime' / name, 'tasm2_nx/runtime/' + name))
        source = release / 'tasm2_nx_v1.0.0_source.zip'
        archive(source, source_pairs)
        archives[source.name] = {'bytes': source.stat().st_size, 'sha256': sha(source)}
    report['archives'] = archives
    (release / 'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for name, value in archives.items():
        print(f'{name}: {value["bytes"]} bytes; SHA-256 {value["sha256"]}')


if __name__ == '__main__':
    main()
