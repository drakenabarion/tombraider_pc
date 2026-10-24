#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""State_Main3D (main menu) P-code: a Quit Game item (the PS3 layout's hidden nav_list_item_6_mc) reported to the port, debug code removed so
the movie keeps its size. Usage: edit_main3d.py <in.pcode> <out.pcode>"""
import re, sys
lines = open(sys.argv[1]).read().replace('\r', '').split('\n')

def label_of(line):
    m = re.match(r'^(loc[0-9a-f]+):(.*)$', line)
    return (m.group(1), m.group(2)) if m else (None, line)

# 1. trace() calls: the expression back to the previous statement end, and the Trace.
STATEMENT_END = ('Pop', 'SetMember', 'CallMethod')
out = []
for line in lines:
    if line == 'Trace':
        k = len(out) - 1
        while k > 0:
            prev = label_of(out[k - 1])[1]
            if prev.endswith('{') or prev in ('Pop', 'SetMember', '}') or prev.startswith(('If ', 'Jump ')):
                break
            k -= 1
        removed = out[k:]
        out = out[:k]
        labels = [label_of(r)[0] for r in removed if label_of(r)[0]]
        pending = getattr(sys.modules[__name__], 'pending', [])
        sys.modules[__name__].pending = pending + labels
        continue
    pending = getattr(sys.modules[__name__], 'pending', [])
    if pending:
        if line.startswith('}'):
            for lab in pending:
                out.append(f'{lab}:Push undefined')
                out.append('Pop')
        else:
            lab, rest = label_of(line)
            assert not lab, ('two labels', line)
            assert len(pending) == 1, pending
            line = f'{pending[0]}:{rest}'
        sys.modules[__name__].pending = []
    out.append(line)
lines = out

text = '\n'.join(lines)
# 2. the keyboard test handler (arrow keys move the highlight, Down starts an infocast test) and
# its listener.
text = text.replace('Push register1, 1, "Key"\nGetVariable\nPush "addListener"\nCallMethod\nPop\n', '', 1)
a = text.index('Push register2, "onKeyDown"\n')
b = text.index('loc1640:SetMember\n', a) + len('loc1640:SetMember\n')
text = text[:a] + text[b:]

CALL = ('Push 1, "flash"\nGetVariable\nPush "external"\nGetMember\nPush "ExternalInterface"\n'
        'GetMember\nPush "call"\nCallMethod\nPop\n')

# 3. RegisterNavListItems: after the base class, the Quit item (not on PC, which has its own); on
# later registrations (OnEnable rebuilds the list from the numbered clips, which puts the reused
# nav_list_item_6_mc before Square Enix) it goes back to its own place in the list.
old = 'Push 0.0, register2, "RegisterNavListItems"\nCallMethod\nPop\n}\n'
assert text.count(old) == 1
a = text.index('Push register2, "RegisterNavListItems"\nDefineFunction2 "", 0, 4,')
text = text[:a] + text[a:].replace('DefineFunction2 "", 0, 4,', 'DefineFunction2 "", 0, 6,', 1)
text = text.replace(old, '''Push 0.0, register2, "RegisterNavListItems"
CallMethod
Pop
Push register3, "isPC"
GetMember
If locpq1
Push register1, "m_nPCCloseButtonID"
GetMember
Push 0.0
Less2
Not
If locpq4
Push 0.0, register1, "AddPortQuitItem"
CallMethod
Pop
Jump locpq1
locpq4:Push register1, "m_navMenuItems_array"
GetMember
StoreRegister 2
Pop
Push register1, "nav_list_item_6_mc"
GetMember
StoreRegister 5
Pop
Push 0.0
StoreRegister 4
Pop
locpq5:Push register4, register2, "length"
GetMember
Less2
Not
If locpq7
Push register2, register4
GetMember
Push register5
StrictEquals
Not
If locpq6
Push 1, register4, 2, register2, "splice"
CallMethod
Pop
Jump locpq7
locpq6:Push register4
Increment
StoreRegister 4
Pop
Jump locpq5
locpq7:Push register5, 0.0, register1, "m_nPCCloseButtonID"
GetMember
Push 3, register2, "splice"
CallMethod
Pop
locpq1:Push undefined
Return
}
''')

# 4. OnEnter: report the Quit item (or -1) whenever the main menu is entered.
a = text.index('Push register2, "OnEnter"\nDefineFunction2')
b = text.index('loc0c82:SetMember', a)
body = text[a:b]
assert body.count('If loc0c82') == 1
body = body.replace('If loc0c82', 'If locpq2')
assert body.endswith('Pop\n}\n')
body = body[:-2] + ('locpq2:Push "BBPortQuitIndex", register1, "m_nPCCloseButtonID"\nGetMember\nAdd2\n'
                    + CALL + '}\n')
text = text[:a] + body + text[b:]

# 5. New methods, defined after OnEnter.
new_methods = '''SetMember
Push register2, "AddPortQuitItem"
DefineFunction2 "", 0, 6, false, true, true, false, true, false, false, true, true {
Push register3, "isPS3"
GetMember
Not
If locpq9
Push register1, "m_navMenuItems_array"
GetMember
Push register1, "m_navItems"
GetMember
Push 1
Subtract
GetMember
StoreRegister 4
Pop
Push register1, "nav_list_item_6_mc"
GetMember
StoreRegister 5
Pop
Push register5, "_visible", true
SetMember
Push register5, "_y", register4, "_y"
GetMember
Push 19
Add2
SetMember
Push register5, "m_text_str", "$Menu_QuitLabel"
SetMember
Push true, "$Menu_QuitLabel", 2, register5, "SetText"
CallMethod
Pop
Push register5, 0.0, register1, "m_navItems"
GetMember
Push 3, register1, "m_navMenuItems_array"
GetMember
Push "splice"
CallMethod
Pop
Push register1, "m_nPCCloseButtonID", register1, "m_navItems"
GetMember
SetMember
Push register1, "m_navItems", register1, "m_navItems"
GetMember
Increment
SetMember
Push register2, "panels"
GetMember
Push 6, register2, "menu_mc"
GetMember
Push "quitgame_mc"
GetMember
SetMember
Push register3, "num_items"
GetMember
Push 7
Less2
Not
If locpq3
Push register3, "num_items", 7
SetMember
locpq3:Push "BBPortQuitIndex", register1, "m_nPCCloseButtonID"
GetMember
Add2
''' + CALL + '''locpq9:Push undefined
Return
}
SetMember
Push register2, "OnExit"
DefineFunction2 "", 2, 5, false, false, false, true, true, false, false, true, false, 3, "p_currState", 4, "p_nextState" {
Push "BBPortQuitIndex-1"
''' + CALL + '''Push register4, register3, 2, register2, "OnExit"
CallMethod
Pop
}
SetMember
'''
a = text.index('Push register2, "OnEnter"\nDefineFunction2')
b = text.index('loc0c82:SetMember\n', a)
text = text[:b] + new_methods.replace('SetMember\n', 'loc0c82:SetMember\n', 1) + text[b + len('loc0c82:SetMember\n'):]

# 6a. GetNavItemPosY: the position of the list item itself (the game orders the items by it; the
# numbered clip it used is a hidden PS3-layout item for the added Quit Game).
old = """DefineFunction2 "", 1, 3, false, false, true, false, true, false, false, true, false, 2, "index" {
Push register1, "nav_list_item_", register2
Add2
Push "_mc"
Add2
GetMember
Push "_y"
GetMember
Return
}"""
assert text.count(old) == 1
text = text.replace(old, """DefineFunction2 "", 1, 3, false, false, true, false, true, false, false, true, false, 2, "index" {
Push register1, "m_navMenuItems_array"
GetMember
Push register2
GetMember
Push "_y"
GetMember
Return
}""")
# 6b. OnHighlight: report the highlighted item (the port quits on Cross over Quit Game).
old = 'Push register2, "OnHighlight"\nDefineFunction2 "", 2, 7, false, true, false, true, true, false, false, true, true, 5, "p_index", 6, "p_bRestoring" {\n'
assert text.count(old) == 1
text = text.replace(old, old + 'Push "BBPortHighlight", register5\nAdd2\n' + CALL)

# 6c. Infocast(title, text): dead (the game calls StartInfocast; nothing calls Infocast).
a = text.index('Push register2, "Infocast"\n')
b = text.index('Push register2, "StartInfocast"\n', a)
assert not re.search(r'loc[0-9a-f]+', text[b:].split('\n', 1)[0])
removed = text[a:b]
text = text[:a] + text[b:]
for lab in set(re.findall(r'^(loc[0-9a-f]+):', removed, re.M)):
    assert lab not in text, ('label used outside Infocast', lab)

# 6. Constant pool: drop strings no longer used anywhere else.
first, rest = text.split('\n', 1)
assert first.startswith('ConstantPool ')
pool = re.findall(r'"((?:[^"\\]|\\.)*)"', first)
kept = [s for s in pool if f'"{s}"' in rest]
text = 'ConstantPool ' + ', '.join(f'"{s}"' for s in kept) + '\n' + rest
print(f'pool {len(pool)} -> {len(kept)} strings; dropped: {[s for s in pool if s not in kept]}', file=sys.stderr)
open(sys.argv[2], 'w').write(text)
