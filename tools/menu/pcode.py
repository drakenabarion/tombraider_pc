# SPDX-License-Identifier: GPL-2.0-or-later
"""FFDec P-code (AS2) edits shared by the menu scripts: trace() removal and constant-pool
pruning, which make room for added code (an edited movie must fit in the original's space)."""
import re


def label_of(line):
    m = re.match(r'^(loc[0-9a-f]+):(.*)$', line)
    return (m.group(1), m.group(2)) if m else (None, line)


def strip_traces(text):
    """Every trace(expr): the expression back to the previous statement end, and the Trace. A
    label on the removed code moves to the next statement (or to a no-op before a '}')."""
    out, pending = [], []
    for line in text.replace('\r', '').split('\n'):
        if line == 'Trace':
            k = len(out) - 1
            while k > 0:
                prev = label_of(out[k - 1])[1]
                if prev.endswith('{') or prev in ('Pop', 'SetMember', '}') or prev.startswith(('If ', 'Jump ')):
                    break
                k -= 1
            pending += [label_of(r)[0] for r in out[k:] if label_of(r)[0]]
            del out[k:]
            continue
        if pending:
            if line.startswith('}'):
                for lab in pending:
                    out += [f'{lab}:Push undefined', 'Pop']
            else:
                lab, rest = label_of(line)
                if lab:  # two labels on one place: keep a no-op for the extra ones
                    for extra in pending:
                        out += [f'{extra}:Push undefined', 'Pop']
                else:
                    line = f'{pending[0]}:{rest}'
                    for extra in pending[1:]:
                        out += [f'{extra}:Push undefined', 'Pop']
            pending = []
        out.append(line)
    return '\n'.join(out)


def prune_pool(text):
    """The ConstantPool without strings nothing else in the script uses."""
    first, sep, rest = text.partition('\n')
    if not first.startswith('ConstantPool '):
        return text
    pool = re.findall(r'"((?:[^"\\]|\\.)*)"', first)
    kept = [s for s in pool if f'"{s}"' in rest]
    return 'ConstantPool ' + ', '.join(f'"{s}"' for s in kept) + sep + rest


def main(argv):
    """pcode.py strip <exported P-code dir> <import dir>: every script with trace() calls, without
    them and with its pool pruned, written to the import dir for FFDec -importScript."""
    import sys
    from pathlib import Path
    if len(argv) != 3 or argv[0] != 'strip':
        raise SystemExit(main.__doc__)
    src, dst = Path(argv[1]), Path(argv[2])
    count = 0
    for f in src.rglob('*.pcode'):
        text = f.read_text(encoding='utf-8')
        if '\nTrace\n' not in text:
            continue
        out = dst / f.relative_to(src)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(prune_pool(strip_traces(text)), encoding='utf-8')
        count += 1
    print(f'{count} scripts without trace()', file=sys.stderr)


if __name__ == '__main__':
    import sys
    main(sys.argv[1:])
