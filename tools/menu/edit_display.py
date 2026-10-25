#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""State_OptionsDisplay (Options > Display) ActionScript: rows the port adds after Subtitles.

The game drives the page natively (OnHighlight, UpdateOption) and knows only its four rows; the
added rows are the port's. The movie tells the port which one is highlighted
(ExternalInterface "BBPortFocus:<key>", "BBPortFocus:" for none) and shows the value the port
returns for "BBPortGet:<key>" (a "$" key is localized), polled every frame while the row is
highlighted, so a change the port makes (left/right on the pad) shows at once.
Usage: edit_display.py <in.as> <out.as>"""
import re
import sys

ROWS = [('fullscreen', 'Fullscreen', 'Show the game on the whole screen or in a window.'),
        ('fps', 'Frame Rate', 'Frames per second. Above 60 is experimental: the game was made for 30 and 60.'),
        ('render_res', 'Render Resolution', 'The resolution the game renders at. Applies the next time the game starts.'),
        ('shadows', 'Shadow Quality', 'PS4: the original shadows. High: twice the resolution. Highest: also softer edges. Applies the next time the game starts.'),
        ('hair_shadows', 'Hair Shadows', "Shadows cast by Lara's hair. Off removes their flicker for a more stable image.")]

src = open(sys.argv[1], encoding='utf-8').read().replace('\r', '')
# trace() calls: the game's debug output, nothing reads it (keeps the movie in its space).
src = re.sub(r'\n[ \t]*trace\((?:[^;"]|"(?:[^"\\]|\\.)*")*\);', '', src)

hook = '      this.SwitchToSafeFrame(ID == 1);\n'
assert src.count(hook) == 1
src = src.replace(hook, hook + '''      this.BBPortFocus(ID);
      if(this.bbport_keys[ID] != undefined)
      {
         return undefined;
      }
''')

enter = '      this.OnHighlight(0,false);\n'
assert src.count(enter) == 1
src = src.replace(enter, '      this.BBPortLayout();\n' + enter)

exit_ = '''   function OnExit(p_prevState, p_currState)
   {
'''
assert src.count(exit_) == 1
src = src.replace(exit_, exit_ + '      this.BBPortFocus(-1);\n')

rows = ''.join(f'      this.BBPortAddRow("{k}","{l}","{d}");\n' for k, l, d in ROWS)
methods = '''   function onLoad()
   {
      super.onLoad();
      this.bbport_keys = new Array();
      this.bbport_desc = new Array();
      this.bbport_poll = this.createEmptyMovieClip("bbport_poll_mc",this.getNextHighestDepth());
''' + rows + '''   }
   function BBPortAddRow(key, label, desc)
   {
      var last = this.m_navMenuItems_array[this.m_navMenuItems_array.length - 1];
      var row = this.attachMovie("Menu Label","nav_list_item_" + this.TotalNavListItems() + "_mc",this.getNextHighestDepth(),last);
      row.m_text_str = label;
      row.m_localized_bln = false;
      row.SetText(label,false);
      row._visible = true;
      this.bbport_keys[this.m_navMenuItems_array.length] = key;
      this.bbport_desc[this.m_navMenuItems_array.length] = desc;
      this.m_navMenuItems_array.push(row);
      this.m_navItems = this.m_navItems + 1;
   }
   function BBPortLayout()
   {
      var step = this.clip.nav_list_item_1_mc._y - this.clip.nav_list_item_0_mc._y;
      var y = this.clip.nav_list_item_0_mc._y;
      var i = 0;
      var item;
      while(i < this.m_navMenuItems_array.length)
      {
         item = this.m_navMenuItems_array[i];
         if(this.bbport_keys[i] != undefined)
         {
            item._x = this.clip.nav_list_item_0_mc._x;
            item._y = y + step;
         }
         if(item._visible && item._y > y)
         {
            y = item._y;
         }
         i++;
      }
   }
   function BBPortFocus(ID)
   {
      var key = this.bbport_keys[ID];
      flash.external.ExternalInterface.call("BBPortFocus:" + (key == undefined ? "" : key));
      this.bbport_poll.onEnterFrame = undefined;
      if(key == undefined)
      {
         return undefined;
      }
      this.clip.thumbnails_dynamic_mc._visible = false;
      this.clip.selection_desc.SetText(this.bbport_desc[ID],false);
      var self = this;
      var shown = null;
      this.bbport_poll.onEnterFrame = function()
      {
         var v = String(flash.external.ExternalInterface.call("BBPortGet:" + key));
         if(v != shown)
         {
            shown = v;
            self.clip.selection_title.SetText(v.charAt(0) == "$" ? LocalizationManager.getGlobalLocalizedString(v,self) : v,false);
            self.clip.selection_title.ShowArrows(true,true);
         }
      };
      this.bbport_poll.onEnterFrame();
   }
'''
last = src.rstrip().rfind('}')
src = src[:last] + methods + '}\n'
src = src.replace('   var clip = null;\n', '   var clip = null;\n   var bbport_keys;\n   var bbport_desc;\n   var bbport_poll;\n', 1)
open(sys.argv[2], 'w', encoding='utf-8').write(src)
