#!/usr/bin/env python3
"""Writes RPCS3's English on-screen text as a C++ table, for a module built
without Qt: each case of rpcs3qt/localized_emu.h becomes
{localized_string_id::ID, "text"}, its %0 left for the caller to fill.

    localized_strings.py localized_emu.h OUTPUT
"""
import re
import sys

LITERAL = r'"(?:[^"\\]|\\.)*"'
CASE = re.compile(r'case\s+localized_string_id::(\w+)\s*:\s*return\s+(?:tr\(\s*((?:' + LITERAL + r'\s*)+)|(' + LITERAL + r'))')


def main():
    source, output = sys.argv[1], sys.argv[2]
    text = open(source, encoding='utf-8').read()
    cases = re.findall(r'case\s+localized_string_id::\w+\s*:', text)
    rows = {}
    for match in CASE.finditer(text):
        rows.setdefault(match.group(1), (match.group(2) or match.group(3)).strip())
    if len(rows) != len(cases):
        sys.exit(f'{source}: read {len(rows)} of {len(cases)} cases')
    lines = ['// Written by localized_strings.py from rpcs3qt/localized_emu.h.']
    lines += [f'{{ localized_string_id::{name}, {literal} }},' for name, literal in rows.items()]
    open(output, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
