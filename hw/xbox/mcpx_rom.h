/*
 * QEMU MCPX internal ROM implementation
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

#ifndef HW_XBOX_MCPX_ROM_H
#define HW_XBOX_MCPX_ROM_H

void mcpx_rom_hide(XBOX_MCPXState* s);
void mcpx_rom_show(XBOX_MCPXState* s);
void mcpx_rom_init(XBOX_MCPXState* s);
void mcpx_rom_reset(XBOX_MCPXState* s);

#endif
