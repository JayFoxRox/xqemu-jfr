/*
 * QEMU MCPX (Southbridge) implementation
 *
 * Copyright (c) 2014 espes
 * Copyright (c) 2014 Jannik Vogel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/nv2a.h"

#include "hw/xbox/mcpx.h"

struct XBOX_MCPXState* last = NULL;

void mcpx_reset(struct XBOX_MCPXState* s) {
  // Force reset of all PCI devices
  //FIXME

  // Reset the ROM
if (s==NULL) { s = last; } // Hack to make this compilable
  mcpx_rom_reset(s);

  // Force an NV2A reset
  nv2a_reset();
}

void mcpx_init(struct XBOX_MCPXState* s) {

last = s;

  s->xmode = 0x3; // FIXME: option mcpx_xmode [can be 0x2 or 0x3, later maybe 0x0 or 0x1 too]
  s->revision = 0xB2; //FIXME: option mcpx_revision

  //FIXME: Forward to all MCPX components

  // Only the XMode 3 has an internal ROM
  bool has_internal_rom = (s->xmode == 0x3);
  if (!has_internal_rom) {
    printf("Not XMode-3. Ignoring ROM settings!");
  } else {
    /* southbridge chip contains and controls bootrom image.
     * can't load it through loader.c because it overlaps with the bios...
     * We really should just commandeer the entire top 16Mb.
     */
    mcpx_rom_init(s);
  }

  qemu_register_reset(mcpx_reset,s);
  
}
