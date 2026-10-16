#!/usr/bin/env python3
"""Finds which renderer optimizations break a game's picture, while the game runs.

The game must run with BB_TOGGLE_FILE pointing at the same file (default /tmp/vo-toggles). Each
step writes a set of optimizations to switch off; look at the game and answer y (picture right)
or n (still broken). Usage: python3 tools/toggle_bisect.py [toggle file]
"""
import sys

FILE = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vo-toggles'
NAMES = {
    0: 'region cache', 1: 'fetch shader cache', 2: 'page tracking early exit', 3: 'pending-op poll limit',
    4: 'threaded Vulkan recording', 5: 'texture descriptor cache', 6: 'lock-free upload check',
    7: 'image lookup cache', 8: 'deferred buffer uploads', 9: 'barrier tracker memo',
    10: 'texture binding memo', 11: 'page-granular read tracking', 12: 'prepared resources',
    13: 'draw preparation workers', 14: 'deferred stream copies', 15: 'hot pages', 16: 'fault window',
    17: 'parallel copies', 18: 'async fences', 19: 'pooled small copies', 30: 'UpdateImage fast path',
    32: 'record prefetch', 33: 'texture view memo', 34: 'texture bind helper', 35: 'early draw inputs',
    36: 'constant ring', 37: 'draw pipeline', 38: 'pipelined tasks', 39: 'pending fence waits',
    40: 'pipelined dispatch', 41: 'recorder fences', 42: 'pipelined memory writes',
    43: 'multi copy shader', 44: 'render state memo', 45: 'texture set memo',
    46: 'pipelined indirect draws', 47: 'scene attachments only', 48: 'sample scene proxies',
    49: 'ordered guest writes', 50: 'scene half res', 57: 'scene mip bias',
}
BITS = sorted(NAMES)


def mask(bits):
    return sum(1 << b for b in bits)


def write(bits):
    with open(FILE, 'w') as f:
        f.write(f'{mask(bits)}\n')


def ask(bits):
    write(bits)
    label = ', '.join(NAMES[b] for b in bits) if len(bits) <= 4 else f'{len(bits)} optimizations'
    while True:
        answer = input(f'\nOff now: {label}\n  Wait a second or two. Picture right? [y/n] ').strip().lower()
        if answer in ('y', 'n'):
            return answer == 'y'


def find(candidates, kept_off):
    """The candidates that must be off (given kept_off stays off) for a right picture."""
    if len(candidates) == 1:
        return candidates
    half = len(candidates) // 2
    a, b = candidates[:half], candidates[half:]
    if ask(kept_off + a):
        return find(a, kept_off)
    if ask(kept_off + b):
        return find(b, kept_off)
    # Neither half alone: culprits in both. Find those in a with b off, then those in b.
    in_a = find(a, kept_off + b)
    return in_a + find(b, kept_off + in_a)


def main():
    print(f'Toggle file: {FILE}. The game must be running with BB_TOGGLE_FILE={FILE},')
    print('showing the broken picture (menu or match). Ctrl+C stops and switches everything back on.')
    try:
        if not ask(BITS):
            print('\nThe picture is wrong even with every optimization off: the cause is elsewhere.')
            return
        culprits = find(BITS, [])
        write(culprits)
        print('\nThese must be off for a right picture:')
        for b in culprits:
            print(f'  bit {b}: {NAMES[b]}')
        print(f'\nLeft switched off now (mask {mask(culprits)}): check the picture once more.')
    except (KeyboardInterrupt, EOFError):
        write([])
        print('\nStopped; every optimization is back on.')


if __name__ == '__main__':
    main()
