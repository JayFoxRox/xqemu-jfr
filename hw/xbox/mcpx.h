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

#ifndef HW_XBOX_MCPX_H
#define HW_XBOX_MCPX_H

#include "hw/hw.h"

typedef struct XBOX_MCPX_ROMState {
  bool hle_mcpx_rom_code;
  bool hle_2bl_code;
  bool load_kernel;
  bool enabled;
  uint8_t bootrom_data[512];
  uint8_t flash_data[512];
} XBOX_MCPX_ROMState;

typedef struct XBOX_MCPXState {
  uint8_t xmode;
  uint8_t revision;
  XBOX_MCPX_ROMState rom;
} XBOX_MCPXState;

void mcpx_init(XBOX_MCPXState* s);
void mcpx_reset(XBOX_MCPXState* s);

#include "hw/xbox/mcpx_rom.h"

#endif
