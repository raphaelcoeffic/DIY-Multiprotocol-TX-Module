#!/usr/bin/env python3
"""Turn the Arduino sketch into one C++ file, as the Arduino builder does.

The .ino files are concatenated (main sketch first, then the others in name
order) with #line markers, and prototypes are added for the functions they
define, before the first function of the main sketch. The prototypes follow
the rules of the Arduino builder (arduino-cli, ctags based), so the result
compiles the same way:

- only functions defined in the .ino files, after preprocessing with the
  build flags (functions in disabled #if blocks get none);
- the prototype is "return type, name, parameters", plus "static" or
  'extern "C"' when the definition has it, and is only added when this text
  is found in the definition's line, so that a definition like
  'static void __attribute__((unused)) f()' gets none;
- none for functions with default arguments, nor when the sketch already
  declares the same prototype.

Usage: ino2cpp.py --sketch DIR --main NAME.ino --out FILE.cpp
                  --preproc FILE.cpp -- COMPILER FLAGS...

The compiler command (without -c, -o and the source file) preprocesses the
concatenated sketch. --preproc receives the preprocessed lines of the .ino
files, like the builder's ctags_target_for_gcc_minus_e.cpp, which the binary
signature is computed from (multi_firmware.py).

For the other platforms (platforms/<name>), the .ino files can be built
without the main sketch:

- --exclude NAME.ino...: files left out (the platform has its own);
- --prelude FILE.h: included instead of Arduino.h, what the main sketch
  gives the others; without --main the prototypes follow it;
- --extern-c: the .ino files in 'extern "C"' (not the prelude), for a
  platform written in C.
"""

import argparse
import os
import re
import subprocess
import sys


def line_directive(line, path):
    return '#line %d "%s"\n' % (line, path.replace('\\', '\\\\').replace('"', '\\"'))


def sketch_files(sketch_dir, main, exclude=()):
    others = sorted(f for f in os.listdir(sketch_dir)
                    if f.endswith('.ino') and f != main and f not in exclude)
    files = [main] + others if main else others
    return [os.path.abspath(os.path.join(sketch_dir, f)) for f in files]


def read(path):
    with open(path, encoding='utf-8', errors='surrogateescape', newline='') as f:
        return f.read()


def merge(files, prelude=None):
    """The concatenated sketch, as the builder writes it (before prototypes)."""
    out = ['#include "%s"\n' % prelude if prelude else '#include <Arduino.h>\n']
    for path in files:
        out.append(line_directive(1, path))
        out.append(read(path) + '\n')
    return ''.join(out)


LINEMARKER = re.compile(r'#\s*(\d+)\s+"((?:[^"\\]|\\.)*)"')


def filter_sketch(preprocessed, files):
    """Keep the lines of the .ino files (and their line markers)."""
    names = set(files)
    keep = False
    out = []
    for line in preprocessed.splitlines(keepends=True):
        m = LINEMARKER.match(line)
        if m:
            name = m.group(2).encode().decode('unicode_escape')
            keep = os.path.abspath(name) in names
        if keep:
            out.append(line)
    return ''.join(out)


#
# Tokens of the preprocessed .ino files, each with its file, line and its
# position in the preprocessed text
#
TOKEN = re.compile(r'''
    (?P<ws>[ \t\r\f\v]+)
  | (?P<nl>\n)
  | (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
  | (?P<char>(?:u8|u|U|L)?'(?:[^'\\\n]|\\.)*')
  | (?P<number>\.?[0-9](?:[eEpP][+-]|[A-Za-z0-9_.'])*)
  | (?P<ident>[A-Za-z_$][A-Za-z0-9_$]*)
  | (?P<punct>::|->|\.\.\.|[^\sA-Za-z0-9_])
''', re.S | re.X)


class Token:
    __slots__ = ('text', 'file', 'line', 'pos', 'end')

    def __init__(self, text, file, line, pos, end):
        self.text, self.file, self.line, self.pos, self.end = text, file, line, pos, end


def tokenize(text):
    tokens = []
    file, line = None, 1
    pos = 0
    at_line_start = True
    while pos < len(text):
        if at_line_start:
            # Line markers and the remaining directives (#pragma)
            j = pos
            while j < len(text) and text[j] in ' \t':
                j += 1
            if j < len(text) and text[j] == '#':
                end = text.find('\n', j)
                end = len(text) if end < 0 else end + 1
                m = LINEMARKER.match(text, j)
                if m:
                    file = os.path.abspath(m.group(2).encode().decode('unicode_escape'))
                    line = int(m.group(1))
                else:
                    line += 1
                pos = end
                continue
        m = TOKEN.match(text, pos)
        if not m:
            raise SystemExit('ino2cpp: cannot tokenize at %r' % text[pos:pos + 40])
        kind = m.lastgroup
        at_line_start = False
        if kind == 'nl':
            line += 1
            at_line_start = True
        elif kind == 'comment':
            line += m.group().count('\n')
        elif kind != 'ws':
            tokens.append(Token(m.group(), file, line, m.start(), m.end()))
        pos = m.end()
    return tokens


SPECIFIERS = {'static', 'inline', 'extern', '"C"', 'virtual', 'explicit', 'friend'}
NOT_FUNCTIONS = {'struct', 'class', 'union', 'enum', 'typedef', 'namespace', 'template'}


def skip_group(tokens, i, open_, close):
    """Index after the group opened at tokens[i]."""
    depth = 0
    while i < len(tokens):
        if tokens[i].text == open_:
            depth += 1
        elif tokens[i].text == close:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def without_attributes(tokens):
    out = []
    i = 0
    while i < len(tokens):
        if tokens[i].text in ('__attribute__', '__attribute') and i + 1 < len(tokens) \
                and tokens[i + 1].text == '(':
            i = skip_group(tokens, i + 1, '(', ')')
            continue
        out.append(tokens[i])
        i += 1
    return out


class Function:
    def __init__(self, decl, text, is_definition):
        self.is_definition = is_definition
        decl = without_attributes(decl)
        # Qualifiers after the parameters
        while decl and decl[-1].text != ')':
            decl.pop()
        close = len(decl) - 1
        depth = 0
        open_ = close
        while open_ >= 0:
            if decl[open_].text == ')':
                depth += 1
            elif decl[open_].text == '(':
                depth -= 1
                if depth == 0:
                    break
            open_ -= 1
        self.valid = open_ > 0 and re.match(r'[A-Za-z_]', decl[open_ - 1].text) is not None
        if not self.valid:
            return
        name = decl[open_ - 1]
        self.name = name.text
        self.file, self.line, self.pos = name.file, name.line, name.pos
        head = decl[:open_ - 1]
        self.static = any(t.text == 'static' for t in head)
        self.extern_c = any(t.text == '"C"' for t in head)
        ret = [t.text for t in without_attributes(head) if t.text not in SPECIFIERS]
        self.returntype = join(ret)
        params = text[decl[open_].pos:decl[close].end]
        params = re.sub(r'//[^\n]*|/\*.*?\*/', ' ', params, flags=re.S)
        self.signature = re.sub(r'\s+', ' ', params)
        self.prototype = '%s %s%s;' % (self.returntype, self.name, self.signature)


def join(words):
    out = ''
    for w in words:
        if out and not (w in ('*', '&', ',', ')', ']', '::') or out[-1] in '(*&[' or out.endswith('::')):
            out += ' '
        out += w
    return out


def functions(tokens, text):
    """Function definitions and declarations at namespace scope."""
    found = []
    scopes = []      # 'linkage' (extern "C" { or namespace {), 'function', 'other'
    stmt = []
    paren = 0

    def at_top():
        return all(s == 'linkage' for s in scopes)

    def looks_like_function(decl):
        decl = without_attributes(decl)
        while decl and decl[-1].text in ('const', 'noexcept', 'override', 'final'):
            decl.pop()
        if not decl or decl[-1].text != ')' or decl[0].text in NOT_FUNCTIONS:
            return False
        depth = 0
        for t in decl:
            if t.text in '([':
                depth += 1
            elif t.text in ')]':
                depth -= 1
            elif t.text == '=' and depth == 0:
                return False
        return True

    for t in tokens:
        if not at_top():
            if t.text == '{':
                scopes.append('other')
            elif t.text == '}':
                kind = scopes.pop()
                if kind == 'function' and at_top():
                    stmt = []
            continue
        if t.text in '([':
            paren += 1
        elif t.text in ')]':
            paren -= 1
        if paren:
            stmt.append(t)
            continue
        if t.text == '{':
            texts = [s.text for s in stmt]
            if texts in (['extern', '"C"'],) or (texts and texts[0] == 'namespace'):
                scopes.append('linkage')
                stmt = []
            elif looks_like_function(stmt):
                f = Function(stmt, text, True)
                if f.valid:
                    found.append(f)
                scopes.append('function')
                stmt = []
            else:
                scopes.append('other')
                stmt.append(t)
        elif t.text == '}':
            if scopes:
                scopes.pop()
            stmt = []
        elif t.text == ';':
            if looks_like_function(stmt):
                f = Function(stmt, text, False)
                if f.valid:
                    found.append(f)
            stmt = []
        else:
            stmt.append(t)
    return found


def nospace(s):
    return re.sub(r'\s', '', s)


def code_matches(f, text):
    """Is the prototype in the definition's line(s), like the builder checks."""
    start = text.rfind('\n', 0, f.pos) + 1
    end = text.find('\n', f.pos)
    code = re.sub(r'//[^\n]*|/\*.*?\*/', '', text[start:end if end >= 0 else len(text)])
    lines = 0
    while ')' not in code and end >= 0 and lines < 10:
        nxt = text.find('\n', end + 1)
        code += re.sub(r'//[^\n]*|/\*.*?\*/', '', text[end + 1:nxt if nxt >= 0 else len(text)])
        end = nxt
        lines += 1
    proto = nospace(f.prototype)[:-1]
    code = nospace(code)
    if proto in code:
        return True
    # Return type on the previous line(s): "void\nfoo()"
    code = code[:code.find(')') + 1] if ')' in code else code
    n = proto.find(code)
    if n <= 0:
        return False
    before = re.sub(r'//[^\n]*|/\*.*?\*/', '', text[max(0, start - 2000):start], flags=re.S)
    before = re.sub(r'^#[^\n]*$', '', before, flags=re.M)
    return proto in nospace(before)[-n:] + code


def prototypes(funcs, text):
    declared = {nospace(f.prototype) for f in funcs if not f.is_definition}
    seen = set()
    out = []
    for f in funcs:
        if not f.is_definition:
            continue
        key = nospace(f.prototype)
        if key in declared or key in seen:
            continue
        # Default arguments can only be given once
        if '=' in f.signature:
            continue
        if not code_matches(f, text):
            continue
        seen.add(key)
        modifiers = ('static ' if f.static else '') + ('extern "C" ' if f.extern_c else '')
        out.append((f, modifiers + f.prototype))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sketch', required=True)
    ap.add_argument('--main', default=None)
    ap.add_argument('--exclude', nargs='*', default=[])
    ap.add_argument('--prelude', default=None)
    ap.add_argument('--extern-c', action='store_true')
    ap.add_argument('--out', required=True)
    ap.add_argument('--preproc', required=True)
    ap.add_argument('compiler', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.compiler[1:] if args.compiler[:1] == ['--'] else args.compiler

    files = sketch_files(args.sketch, args.main, args.exclude)
    merged = merge(files, args.prelude)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    merged_path = os.path.abspath(args.out) + '.merged'
    with open(merged_path, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
        f.write(merged)
    pre = subprocess.run(cmd + ['-w', '-x', 'c++', '-E', '-CC', merged_path],
                         stdout=subprocess.PIPE, cwd=out_dir)
    if pre.returncode:
        sys.exit(pre.returncode)
    sketch_text = filter_sketch(pre.stdout.decode('utf-8', 'surrogateescape'), files)
    with open(args.preproc, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
        f.write(sketch_text)

    funcs = functions(tokenize(sketch_text), sketch_text)
    protos = prototypes(funcs, sketch_text)

    # Before the first function of the main sketch, else after the prelude
    block = ''.join(line_directive(f.line, f.file) + p + '\n' for f, p in protos)
    lines = merged.splitlines(keepends=True)
    if args.main:
        main_file = files[0]
        first = min((f.line for f in funcs if f.is_definition and f.file == main_file),
                    default=None)
        if first is not None:
            block += line_directive(first, main_file)
        # merged: '#include <Arduino.h>', '#line 1', then the main sketch
        at = 2 + (first - 1 if first is not None else 0)
    else:
        at = 1
    if args.extern_c:
        lines[0] += 'extern "C" {\n'
        lines.append('}\n')
    result = ''.join(lines[:at]) + block + ''.join(lines[at:])

    with open(args.out, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
        f.write(result)


if __name__ == '__main__':
    main()
