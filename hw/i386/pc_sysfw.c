/*
 * QEMU PC System Firmware
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2011-2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysemu/blockdev.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "hw/block/flash.h"
#include "sysemu/kvm.h"

#define BIOS_FILENAME "bios.bin"

typedef struct PcSysFwDevice {
    SysBusDevice busdev;
    uint8_t isapc_ram_fw;
} PcSysFwDevice;

static void pc_isa_bios_init(MemoryRegion *rom_memory,
                             MemoryRegion *flash_mem,
                             int ram_size)
{
    int isa_bios_size;
    MemoryRegion *isa_bios;
    uint64_t flash_size;
    void *flash_ptr, *isa_bios_ptr;

    flash_size = memory_region_size(flash_mem);

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = MIN(flash_size, 128 * 1024);
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_ram(isa_bios, NULL, "isa-bios", isa_bios_size);
    vmstate_register_ram_global(isa_bios);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);

    /* copy ISA rom image from top of flash memory */
    flash_ptr = memory_region_get_ram_ptr(flash_mem);
    isa_bios_ptr = memory_region_get_ram_ptr(isa_bios);
    memcpy(isa_bios_ptr,
           ((uint8_t*)flash_ptr) + (flash_size - isa_bios_size),
           isa_bios_size);

    memory_region_set_readonly(isa_bios, true);
}

static void pc_system_flash_init(MemoryRegion *rom_memory,
                                 DriveInfo *pflash_drv)
{
    BlockDriverState *bdrv;
    int64_t size;
    hwaddr phys_addr;
    int sector_bits, sector_size;
    pflash_t *system_flash;
    MemoryRegion *flash_mem;

    bdrv = pflash_drv->bdrv;
    size = bdrv_getlength(pflash_drv->bdrv);
    sector_bits = 12;
    sector_size = 1 << sector_bits;

    if ((size % sector_size) != 0) {
        fprintf(stderr,
                "qemu: PC system firmware (pflash) must be a multiple of 0x%x\n",
                sector_size);
        exit(1);
    }

    phys_addr = 0x100000000ULL - size;
    system_flash = pflash_cfi01_register(phys_addr, NULL, "system.flash", size,
                                         bdrv, sector_size, size >> sector_bits,
                                         1, 0x0000, 0x0000, 0x0000, 0x0000, 0);
    flash_mem = pflash_cfi01_get_memory(system_flash);

    pc_isa_bios_init(rom_memory, flash_mem, size);
}

static void old_pc_system_rom_init(MemoryRegion *rom_memory, bool isapc_ram_fw)
{
    char *filename;
    MemoryRegion *bios, *isa_bios;
    int bios_size, isa_bios_size;
    int ret;

    /* BIOS load */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, NULL, "pc.bios", bios_size);
    vmstate_register_ram_global(bios);
    if (!isapc_ram_fw) {
        memory_region_set_readonly(bios, true);
    }
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
    bios_error:
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
        exit(1);
    }
    if (filename) {
        g_free(filename);
    }

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = bios_size;
    if (isa_bios_size > (128 * 1024)) {
        isa_bios_size = 128 * 1024;
    }
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_alias(isa_bios, NULL, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);
    if (!isapc_ram_fw) {
        memory_region_set_readonly(isa_bios, true);
    }

    /* map all the bios at the top of memory */
    memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                bios);
}

void pc_system_firmware_init(MemoryRegion *rom_memory, bool isapc_ram_fw)
{
    DriveInfo *pflash_drv;

    pflash_drv = drive_get(IF_PFLASH, 0, 0);

    if (isapc_ram_fw || pflash_drv == NULL) {
        /* When a pflash drive is not found, use rom-mode */
        old_pc_system_rom_init(rom_memory, isapc_ram_fw);
        return;
    }

    if (kvm_enabled() && !kvm_readonly_mem_enabled()) {
        /* Older KVM cannot execute from device memory. So, flash memory
         * cannot be used unless the readonly memory kvm capability is present. */
        fprintf(stderr, "qemu: pflash with kvm requires KVM readonly memory support\n");
        exit(1);
    }

    pc_system_flash_init(rom_memory, pflash_drv);
}
