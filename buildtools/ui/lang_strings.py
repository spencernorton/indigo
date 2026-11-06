#!/usr/bin/env python3
"""The English strings Menu Language can translate, from the interface's code.

Every string literal in the files Indigo draws its interface from (adjacent
literals joined, C escapes decoded as the compiler would, bytes read as
Latin-1, the IPL font's encoding), less those that are never shown: the
arguments of debug prints, file writes, comparisons and path builders, and
strings without a word in them.

usage: lang_strings.py            the strings, one JSON object per line
       lang_strings.py --missing  those that cube/swiss/source/gui/lang/uk.po lacks
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'cube/swiss/source'
FILES = sorted((SOURCE / 'gui').glob('*.c')) + [SOURCE / 'swiss.c', SOURCE / 'main.c',
                                                SOURCE / 'config/config.c']
PO = SOURCE / 'gui/lang/uk.po'

# Calls whose strings never reach the screen.
HIDDEN = {
    'print_debug', 'fprintf', 'printf', 'strcmp', 'strncmp', 'strcasecmp', 'strncasecmp',
    'strstr', 'strcasestr', 'concat_path', 'ensure_path', 'fopen', 'open', 'assert',
    '_Static_assert', 'static_assert', 'getenv', 'setenv', 'endsWith', 'config_file_read',
    'config_file_write', 'config_file_delete', 'kprintf', 'puts', 'strchr', 'strrchr',
    'strspn', 'strcspn', 'sscanf', 'memcmp', 'indigo_log', 'UILog_Line', 'fputs', 'LogHost',
    'lab_log', 'usb_sendbuffer', 'DCFlushRange', 'TPL_OpenTPLFromMemory', 'mkdir', 'rename',
    'remove', 'unlink', 'stat', 'opendir', 'f_open', 'f_stat', 'f_opendir', 'f_unlink',
}

TOKEN = re.compile(r'''
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<char>'(?:\\.|[^'\\\n])*')
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<space>\s+)
  | (?P<other>.)
''', re.S | re.X)

ESCAPE = re.compile(r'\\(x[0-9A-Fa-f]+|[0-7]{1,3}|.)', re.S)
SIMPLE = {'n': '\n', 't': '\t', 'r': '\r', '0': '\0', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v',
          '\\': '\\', '"': '"', "'": "'", '?': '?', 'e': '\x1b'}


def decode(body):
    def one(match):
        e = match.group(1)
        if e[0] == 'x':
            return chr(int(e[1:], 16) & 0xff)
        if e[0] in '01234567':
            return chr(int(e, 8) & 0xff)
        return SIMPLE.get(e, e)
    return ESCAPE.sub(one, body)


def literals(path):
    """(line, text, enclosing call) for each string, adjacent ones joined."""
    source = path.read_text(encoding='latin-1')
    tokens = [(m.lastgroup, m.group(), m.start()) for m in TOKEN.finditer(source)]
    tokens = [t for t in tokens if t[0] not in ('comment', 'space')]
    stack = []  # the identifier before each open '('
    i = 0
    while i < len(tokens):
        kind, text, at = tokens[i]
        if kind == 'other' and text == '(':
            stack.append(tokens[i - 1][1] if i and tokens[i - 1][0] == 'ident' else '')
        elif kind == 'other' and text == ')' and stack:
            stack.pop()
        elif kind == 'string':
            parts = [text[1:-1]]
            # Adjacent literals are one string, and a macro between them
            # (PRIu32 and the like) is part of a format: such strings are left out.
            j = i + 1
            while j < len(tokens) and tokens[j][0] == 'string':
                parts.append(tokens[j][1][1:-1])
                j += 1
            joined = j < len(tokens) and tokens[j][0] == 'ident' and tokens[j][1].isupper()
            preceded = i and tokens[i - 1][0] == 'ident' and tokens[i - 1][1].isupper() and \
                tokens[i - 1][1] not in ('return',)
            if not joined:
                yield source.count('\n', 0, at) + 1, decode(''.join(parts)), \
                    (stack[-1] if stack else ''), preceded
            i = j
            continue
        i += 1


def shown(text, call):
    if call in ('UI_T', 'UILang_Text'):
        return re.search(r'[A-Za-z]{2}', text) is not None  # looked up on purpose
    if call in HIDDEN or not re.search(r'[A-Za-z]{2}', text):
        return False
    if re.fullmatch(r'[\w./%-]*', text) and ('/' in text or '.' in text or '_' in text):
        return False  # a path, file name or identifier
    if re.fullmatch(r'[a-z0-9_]+', text):
        return False  # a key or code, never a label
    if '=' in text and re.search(r'^[A-Z][\w ]*=', text):
        return False  # a settings-file line
    return True


# gui/ui_hint.c's control names, longest first. A hint line ("A  OPEN    B
# BACK") is split into items before it is drawn, so each label is looked
# up on its own; the names themselves are drawn as icons and never change.
CONTROLS = ('STICK / D-PAD', '\x8b  \x9b', 'D-PAD', 'STICK', 'START', 'L/R', 'L+A',
            'A', 'B', 'X', 'Y', 'Z', 'L', 'R')


def hint_labels(text):
    """The labels of text's hint items, when it is a hint line."""
    labels = []
    for item in re.split(r' {3,}|\xb7', text):
        item = item.strip()
        for name in CONTROLS:
            if item.startswith(name + '  ') and item[len(name) + 2:].strip():
                labels.append(item[len(name) + 2:].strip())
                break
    return labels


def strings():
    seen = {}
    for path in FILES:
        for line, text, call, preceded in literals(path):
            if preceded or not shown(text, call):
                continue
            where = f'{path.relative_to(ROOT).as_posix()}:{line}'
            seen.setdefault(text, []).append(where)
            for label in hint_labels(text):
                if label not in CONTROLS:
                    seen.setdefault(label, []).append(where)
    return seen


def po_ids(path):
    """The msgids a .po file has, decoded."""
    ids, current, key = set(), None, None
    for line in path.read_text(encoding='utf-8').splitlines() + ['']:
        line = line.strip()
        m = re.match(r'(msgid|msgstr)\s+"(.*)"$', line)
        if m:
            if m.group(1) == 'msgid':
                current, key = [m.group(2)], 'id'
            else:
                if current is not None:
                    ids.add(decode(''.join(current)).encode('utf-8').decode('utf-8'))
                key = 'str'
        elif line.startswith('"') and key == 'id':
            current.append(line[1:-1])
    return ids


def main():
    sys.stdout.reconfigure(encoding='utf-8', newline='\n')
    found = strings()
    if '--missing' in sys.argv:
        have = po_ids(PO) if PO.exists() else set()
        found = {k: v for k, v in found.items() if k not in have}
    for text, where in found.items():
        print(json.dumps({'english': text, 'where': where[:3]}, ensure_ascii=False))


if __name__ == '__main__':
    main()
