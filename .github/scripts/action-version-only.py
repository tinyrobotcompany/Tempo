#!/usr/bin/env python3
"""Conservatively recognize existing workflow action SHA updates using only stdlib."""
import re
import subprocess
import sys


ACTION = re.compile(
    r"^([ ]*-[ ]+uses:[ ]+)([\w.-]+/[\w.-]+(?:/[\w.-]+)*)@"
    r"[a-f0-9]{40}([ ]*(?:#[ ]*v\d+(?:\.\d+)*(?:[-+][\w.-]+)?)?[ ]*)$"
)
BLOCK = re.compile(r":\s*[|>][\d+-]*\s*(?:#.*)?$")
QUOTED_VALUE = re.compile(r'''(?:^\s*-\s+|:\s*)(['"])(.*)$''')


def normalized(text):
    """Ignore only pinned refs/version annotations, never text inside block scalars."""
    lines = []
    block_indent = None
    for line in text.splitlines(keepends=True):
        indent = len(line) - len(line.lstrip(' '))
        if block_indent is not None:
            if not line.strip() or indent > block_indent:
                lines.append(line)
                continue
            block_indent = None
        if BLOCK.search(line.rstrip('\n')):
            # A sequence key starts after '- ', so its scalar content is deeper.
            block_indent = indent + (2 if line.lstrip().startswith('- ') else 0)
        quoted = QUOTED_VALUE.search(line.rstrip('\n'))
        if quoted and quoted[1] not in quoted[2]:
            # Multiline quoted YAML needs full parsing; require a note instead.
            return None
        match = ACTION.fullmatch(line.rstrip('\n'))
        lines.append(f'{match[1]}{match[2]}@PIN\n' if match else line)
    return ''.join(lines)


def git(*args):
    return subprocess.check_output(['git', *args])


def action_versions_only(base, head):
    base = git('merge-base', base, head).decode().strip()
    # Added/deleted/renamed files, symlinks and mode changes need normal review notes.
    if git('diff', '--summary', base, head):
        return False
    names = git('diff', '--name-only', '-z', base, head).decode().split('\0')[:-1]
    if not names:
        return False
    for name in names:
        if not re.fullmatch(r'\.github/workflows/[^/]+\.ya?ml', name):
            return False
        before = git('show', f'{base}:{name}').decode()
        after = git('show', f'{head}:{name}').decode()
        original = normalized(before)
        if original is None or original != normalized(after):
            return False
    return True


if __name__ == '__main__':
    sys.exit(0 if action_versions_only(*sys.argv[1:]) else 1)
