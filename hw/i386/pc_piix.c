/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#include <glib.h>

#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/usb.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/ide.h"
#include "sysemu/kvm.h"
#include "hw/kvm/clock.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/cpu/icc_bus.h"
#include "sysemu/arch_init.h"
#include "sysemu/blockdev.h"
#include "hw/i2c/smbus.h"
#include "hw/xen/xen.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/acpi/acpi.h"
#include "cpu.h"
#ifdef CONFIG_XEN
#  include <xen/hvm/hvm_info_table.h>
#endif

#define MAX_IDE_BUS 2

static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };

static bool has_pci_info;
static bool has_acpi_build = true;

/* PC hardware initialisation */
static void pc_init1(QEMUMachineInitArgs *args,
                     int pci_enabled,
                     int kvmclock_enabled)
{
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    int i;
    ram_addr_t below_4g_mem_size, above_4g_mem_size;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    PCII440FXState *i440fx_state;
    int piix3_devfn = -1;
    qemu_irq *cpu_irq;
    qemu_irq *gsi;
    qemu_irq *i8259;
    qemu_irq *smi_irq;
    GSIState *gsi_state;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BusState *idebus[MAX_IDE_BUS];
    ISADevice *rtc_state;
    ISADevice *floppy;
    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;
    DeviceState *icc_bridge;
    FWCfgState *fw_cfg = NULL;
    PcGuestInfo *guest_info;

    if (xen_enabled() && xen_hvm_init(&ram_memory) != 0) {
        fprintf(stderr, "xen hardware virtual machine initialisation failed\n");
        exit(1);
    }

    icc_bridge = qdev_create(NULL, TYPE_ICC_BRIDGE);
    object_property_add_child(qdev_get_machine(), "icc-bridge",
                              OBJECT(icc_bridge), NULL);

    pc_cpus_init(args->cpu_model, icc_bridge);

    if (kvm_enabled() && kvmclock_enabled) {
        kvmclock_create();
    }

    if (args->ram_size >= 0xe0000000) {
        above_4g_mem_size = args->ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        above_4g_mem_size = 0;
        below_4g_mem_size = args->ram_size;
    }

    if (pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, NULL, "pci", INT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = system_memory;
    }

    guest_info = pc_guest_info_init(below_4g_mem_size, above_4g_mem_size);

    guest_info->has_acpi_build = has_acpi_build;

    guest_info->has_pci_info = has_pci_info;
    guest_info->isapc_ram_fw = !pci_enabled;

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        fw_cfg = pc_memory_init(system_memory,
                       args->kernel_filename, args->kernel_cmdline,
                       args->initrd_filename,
                       below_4g_mem_size, above_4g_mem_size,
                       rom_memory, &ram_memory, guest_info);
    }

    gsi_state = g_malloc0(sizeof(*gsi_state));
    if (kvm_irqchip_in_kernel()) {
        kvm_pc_setup_irq_routing(pci_enabled);
        gsi = qemu_allocate_irqs(kvm_pc_gsi_handler, gsi_state,
                                 GSI_NUM_PINS);
    } else {
        gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);
    }

    if (pci_enabled) {
        pci_bus = i440fx_init(&i440fx_state, &piix3_devfn, &isa_bus, gsi,
                              system_memory, system_io, args->ram_size,
                              below_4g_mem_size,
                              0x100000000ULL - below_4g_mem_size,
                              above_4g_mem_size,
                              pci_memory, ram_memory);
    } else {
        pci_bus = NULL;
        i440fx_state = NULL;
        isa_bus = isa_bus_new(NULL, system_io);
        no_hpet = 1;
    }
    isa_bus_irqs(isa_bus, gsi);

    if (kvm_irqchip_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    } else if (xen_enabled()) {
        i8259 = xen_interrupt_controller_init();
    } else {
        cpu_irq = pc_allocate_cpu_irq();
        i8259 = i8259_init(isa_bus, cpu_irq[0]);
    }

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    if (pci_enabled) {
        ioapic_init_gsi(gsi_state, "i440fx");
    }
    qdev_init_nofail(icc_bridge);

    pc_register_ferr_irq(gsi[13]);

    pc_vga_init(isa_bus, pci_enabled ? pci_bus : NULL);

    /* init basic PC hardware */
    pc_basic_device_init(isa_bus, gsi, &rtc_state, &floppy, xen_enabled());

    pc_nic_init(isa_bus, pci_bus);

    ide_drive_get(hd, MAX_IDE_BUS);
    if (pci_enabled) {
        PCIDevice *dev;
        if (xen_enabled()) {
            dev = pci_piix3_xen_ide_init(pci_bus, hd, piix3_devfn + 1);
        } else {
            dev = pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1);
        }
        idebus[0] = qdev_get_child_bus(&dev->qdev, "ide.0");
        idebus[1] = qdev_get_child_bus(&dev->qdev, "ide.1");
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            ISADevice *dev;
            dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
                               ide_irq[i],
                               hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
            idebus[i] = qdev_get_child_bus(DEVICE(dev), "ide.0");
        }
    }

    pc_cmos_init(below_4g_mem_size, above_4g_mem_size, args->boot_order,
                 floppy, idebus[0], idebus[1], rtc_state);

    if (pci_enabled && usb_enabled(false)) {
        pci_create_simple(pci_bus, piix3_devfn + 2, "piix3-usb-uhci");
    }

    if (pci_enabled && acpi_enabled) {
        i2c_bus *smbus;

        smi_irq = qemu_allocate_irqs(pc_acpi_smi_interrupt, first_cpu, 1);
        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100,
                              gsi[9], *smi_irq,
                              kvm_enabled(), fw_cfg);
        smbus_eeprom_init(smbus, 8, NULL, 0);
    }

    if (pci_enabled) {
        pc_pci_device_init(pci_bus);
    }
}

static void pc_init_pci(QEMUMachineInitArgs *args)
{
    pc_init1(args, 1, 1);
}

static void pc_compat_1_6(QEMUMachineInitArgs *args)
{
    has_pci_info = false;
    rom_file_in_ram = false;
    has_acpi_build = false;
}

static void pc_compat_1_5(QEMUMachineInitArgs *args)
{
    pc_compat_1_6(args);
}

static void pc_compat_1_4(QEMUMachineInitArgs *args)
{
    pc_compat_1_5(args);
    x86_cpu_compat_set_features("n270", FEAT_1_ECX, 0, CPUID_EXT_MOVBE);
    x86_cpu_compat_set_features("Westmere", FEAT_1_ECX, 0, CPUID_EXT_PCLMULQDQ);
}

static void pc_compat_1_3(QEMUMachineInitArgs *args)
{
    pc_compat_1_4(args);
    enable_compat_apic_id_mode();
}

/* PC compat function for pc-0.14 to pc-1.2 */
static void pc_compat_1_2(QEMUMachineInitArgs *args)
{
    pc_compat_1_3(args);
    disable_kvm_pv_eoi();
}

static void pc_init_pci_1_6(QEMUMachineInitArgs *args)
{
    pc_compat_1_6(args);
    pc_init_pci(args);
}

static void pc_init_pci_1_5(QEMUMachineInitArgs *args)
{
    pc_compat_1_5(args);
    pc_init_pci(args);
}

static void pc_init_pci_1_4(QEMUMachineInitArgs *args)
{
    pc_compat_1_4(args);
    pc_init_pci(args);
}

static void pc_init_pci_1_3(QEMUMachineInitArgs *args)
{
    pc_compat_1_3(args);
    pc_init_pci(args);
}

/* PC machine init function for pc-0.14 to pc-1.2 */
static void pc_init_pci_1_2(QEMUMachineInitArgs *args)
{
    pc_compat_1_2(args);
    pc_init_pci(args);
}

/* PC init function for pc-0.10 to pc-0.13, and reused by xenfv */
static void pc_init_pci_no_kvmclock(QEMUMachineInitArgs *args)
{
    has_pci_info = false;
    has_acpi_build = false;
    disable_kvm_pv_eoi();
    enable_compat_apic_id_mode();
    pc_init1(args, 1, 0);
}

static void pc_init_isa(QEMUMachineInitArgs *args)
{
    has_pci_info = false;
    has_acpi_build = false;
    if (!args->cpu_model) {
        args->cpu_model = "486";
    }
    disable_kvm_pv_eoi();
    enable_compat_apic_id_mode();
    pc_init1(args, 0, 1);
}

#ifdef CONFIG_XEN
static void pc_xen_hvm_init(QEMUMachineInitArgs *args)
{
    PCIBus *bus;

    pc_init_pci(args);

    bus = pci_find_primary_bus();
    if (bus != NULL) {
        pci_create_simple(bus, -1, "xen-platform");
    }
}
#endif

#define PC_I440FX_MACHINE_OPTIONS \
    PC_DEFAULT_MACHINE_OPTIONS, \
    .desc = "Standard PC (i440FX + PIIX, 1996)", \
    .hot_add_cpu = pc_hot_add_cpu

#define PC_I440FX_1_7_MACHINE_OPTIONS PC_I440FX_MACHINE_OPTIONS
static QEMUMachine pc_i440fx_machine_v1_7 = {
    PC_I440FX_1_7_MACHINE_OPTIONS,
    .name = "pc-i440fx-1.7",
    .alias = "pc",
    .init = pc_init_pci,
    .is_default = 1,
};

#define PC_I440FX_1_6_MACHINE_OPTIONS PC_I440FX_MACHINE_OPTIONS

static QEMUMachine pc_i440fx_machine_v1_6 = {
    PC_I440FX_1_6_MACHINE_OPTIONS,
    .name = "pc-i440fx-1.6",
    .init = pc_init_pci_1_6,
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_6,
        { /* end of list */ }
    },
};

static QEMUMachine pc_i440fx_machine_v1_5 = {
    PC_I440FX_1_6_MACHINE_OPTIONS,
    .name = "pc-i440fx-1.5",
    .init = pc_init_pci_1_5,
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_5,
        { /* end of list */ }
    },
};

#define PC_I440FX_1_4_MACHINE_OPTIONS \
    PC_I440FX_1_6_MACHINE_OPTIONS, \
    .hot_add_cpu = NULL

static QEMUMachine pc_i440fx_machine_v1_4 = {
    PC_I440FX_1_4_MACHINE_OPTIONS,
    .name = "pc-i440fx-1.4",
    .init = pc_init_pci_1_4,
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_4,
        { /* end of list */ }
    },
};

#define PC_COMPAT_1_3 \
	PC_COMPAT_1_4, \
        {\
            .driver   = "usb-tablet",\
            .property = "usb_version",\
            .value    = stringify(1),\
        },{\
            .driver   = "virtio-net-pci",\
            .property = "ctrl_mac_addr",\
            .value    = "off",      \
        },{ \
            .driver   = "virtio-net-pci", \
            .property = "mq", \
            .value    = "off", \
        }, {\
            .driver   = "e1000",\
            .property = "autonegotiation",\
            .value    = "off",\
        }

static QEMUMachine pc_machine_v1_3 = {
    PC_I440FX_1_4_MACHINE_OPTIONS,
    .name = "pc-1.3",
    .init = pc_init_pci_1_3,
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_3,
        { /* end of list */ }
    },
};

#define PC_COMPAT_1_2 \
        PC_COMPAT_1_3,\
        {\
            .driver   = "nec-usb-xhci",\
            .property = "msi",\
            .value    = "off",\
        },{\
            .driver   = "nec-usb-xhci",\
            .property = "msix",\
            .value    = "off",\
        },{\
            .driver   = "ivshmem",\
            .property = "use64",\
            .value    = "0",\
        },{\
            .driver   = "qxl",\
            .property = "revision",\
            .value    = stringify(3),\
        },{\
            .driver   = "qxl-vga",\
            .property = "revision",\
            .value    = stringify(3),\
        },{\
            .driver   = "VGA",\
            .property = "mmio",\
            .value    = "off",\
        }

#define PC_I440FX_1_2_MACHINE_OPTIONS \
    PC_I440FX_1_4_MACHINE_OPTIONS, \
    .init = pc_init_pci_1_2

static QEMUMachine pc_machine_v1_2 = {
    PC_I440FX_1_2_MACHINE_OPTIONS,
    .name = "pc-1.2",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_2,
        { /* end of list */ }
    },
};

#define PC_COMPAT_1_1 \
        PC_COMPAT_1_2,\
        {\
            .driver   = "virtio-scsi-pci",\
            .property = "hotplug",\
            .value    = "off",\
        },{\
            .driver   = "virtio-scsi-pci",\
            .property = "param_change",\
            .value    = "off",\
        },{\
            .driver   = "VGA",\
            .property = "vgamem_mb",\
            .value    = stringify(8),\
        },{\
            .driver   = "vmware-svga",\
            .property = "vgamem_mb",\
            .value    = stringify(8),\
        },{\
            .driver   = "qxl-vga",\
            .property = "vgamem_mb",\
            .value    = stringify(8),\
        },{\
            .driver   = "qxl",\
            .property = "vgamem_mb",\
            .value    = stringify(8),\
        },{\
            .driver   = "virtio-blk-pci",\
            .property = "config-wce",\
            .value    = "off",\
        }

static QEMUMachine pc_machine_v1_1 = {
    PC_I440FX_1_2_MACHINE_OPTIONS,
    .name = "pc-1.1",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_1,
        { /* end of list */ }
    },
};

#define PC_COMPAT_1_0 \
        PC_COMPAT_1_1,\
        {\
            .driver   = TYPE_ISA_FDC,\
            .property = "check_media_rate",\
            .value    = "off",\
        }, {\
            .driver   = "virtio-balloon-pci",\
            .property = "class",\
            .value    = stringify(PCI_CLASS_MEMORY_RAM),\
        },{\
            .driver   = "apic",\
            .property = "vapic",\
            .value    = "off",\
        },{\
            .driver   = TYPE_USB_DEVICE,\
            .property = "full-path",\
            .value    = "no",\
        }

static QEMUMachine pc_machine_v1_0 = {
    PC_I440FX_1_2_MACHINE_OPTIONS,
    .name = "pc-1.0",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_0,
        { /* end of list */ }
    },
    .hw_version = "1.0",
};

#define PC_COMPAT_0_15 \
        PC_COMPAT_1_0

static QEMUMachine pc_machine_v0_15 = {
    PC_I440FX_1_2_MACHINE_OPTIONS,
    .name = "pc-0.15",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_15,
        { /* end of list */ }
    },
    .hw_version = "0.15",
};

#define PC_COMPAT_0_14 \
        PC_COMPAT_0_15,\
        {\
            .driver   = "virtio-blk-pci",\
            .property = "event_idx",\
            .value    = "off",\
        },{\
            .driver   = "virtio-serial-pci",\
            .property = "event_idx",\
            .value    = "off",\
        },{\
            .driver   = "virtio-net-pci",\
            .property = "event_idx",\
            .value    = "off",\
        },{\
            .driver   = "virtio-balloon-pci",\
            .property = "event_idx",\
            .value    = "off",\
        }

static QEMUMachine pc_machine_v0_14 = {
    PC_I440FX_1_2_MACHINE_OPTIONS,
    .name = "pc-0.14",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_14, 
        {
            .driver   = "qxl",
            .property = "revision",
            .value    = stringify(2),
        },{
            .driver   = "qxl-vga",
            .property = "revision",
            .value    = stringify(2),
        },
        { /* end of list */ }
    },
    .hw_version = "0.14",
};

#define PC_COMPAT_0_13 \
        PC_COMPAT_0_14,\
        {\
            .driver   = TYPE_PCI_DEVICE,\
            .property = "command_serr_enable",\
            .value    = "off",\
        },{\
            .driver   = "AC97",\
            .property = "use_broken_id",\
            .value    = stringify(1),\
        }

#define PC_I440FX_0_13_MACHINE_OPTIONS \
    PC_I440FX_1_2_MACHINE_OPTIONS, \
    .init = pc_init_pci_no_kvmclock

static QEMUMachine pc_machine_v0_13 = {
    PC_I440FX_0_13_MACHINE_OPTIONS,
    .name = "pc-0.13",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_13,
        {
            .driver   = "virtio-9p-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "VGA",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "vmware-svga",
            .property = "rombar",
            .value    = stringify(0),
        },
        { /* end of list */ }
    },
    .hw_version = "0.13",
};

#define PC_COMPAT_0_12 \
        PC_COMPAT_0_13,\
        {\
            .driver   = "virtio-serial-pci",\
            .property = "max_ports",\
            .value    = stringify(1),\
        },{\
            .driver   = "virtio-serial-pci",\
            .property = "vectors",\
            .value    = stringify(0),\
        },{\
            .driver   = "usb-mouse",\
            .property = "serial",\
            .value    = "1",\
        },{\
            .driver   = "usb-tablet",\
            .property = "serial",\
            .value    = "1",\
        },{\
            .driver   = "usb-kbd",\
            .property = "serial",\
            .value    = "1",\
        }

static QEMUMachine pc_machine_v0_12 = {
    PC_I440FX_0_13_MACHINE_OPTIONS,
    .name = "pc-0.12",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_12,
        {
            .driver   = "VGA",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "vmware-svga",
            .property = "rombar",
            .value    = stringify(0),
        },
        { /* end of list */ }
    },
    .hw_version = "0.12",
};

#define PC_COMPAT_0_11 \
        PC_COMPAT_0_12,\
        {\
            .driver   = "virtio-blk-pci",\
            .property = "vectors",\
            .value    = stringify(0),\
        },{\
            .driver   = TYPE_PCI_DEVICE,\
            .property = "rombar",\
            .value    = stringify(0),\
        }

static QEMUMachine pc_machine_v0_11 = {
    PC_I440FX_0_13_MACHINE_OPTIONS,
    .name = "pc-0.11",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_11,
        {
            .driver   = "ide-drive",
            .property = "ver",
            .value    = "0.11",
        },{
            .driver   = "scsi-disk",
            .property = "ver",
            .value    = "0.11",
        },
        { /* end of list */ }
    },
    .hw_version = "0.11",
};

static QEMUMachine pc_machine_v0_10 = {
    PC_I440FX_0_13_MACHINE_OPTIONS,
    .name = "pc-0.10",
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_0_11,
        {
            .driver   = "virtio-blk-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_STORAGE_OTHER),
        },{
            .driver   = "virtio-serial-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_DISPLAY_OTHER),
        },{
            .driver   = "virtio-net-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "ide-drive",
            .property = "ver",
            .value    = "0.10",
        },{
            .driver   = "scsi-disk",
            .property = "ver",
            .value    = "0.10",
        },
        { /* end of list */ }
    },
    .hw_version = "0.10",
};

static QEMUMachine isapc_machine = {
    PC_COMMON_MACHINE_OPTIONS,
    .name = "isapc",
    .desc = "ISA-only PC",
    .init = pc_init_isa,
    .max_cpus = 1,
    .compat_props = (GlobalProperty[]) {
        { /* end of list */ }
    },
};

#ifdef CONFIG_XEN
static QEMUMachine xenfv_machine = {
    PC_COMMON_MACHINE_OPTIONS,
    .name = "xenfv",
    .desc = "Xen Fully-virtualized PC",
    .init = pc_xen_hvm_init,
    .max_cpus = HVM_MAX_VCPUS,
    .default_machine_opts = "accel=xen",
    .hot_add_cpu = pc_hot_add_cpu,
};
#endif

static void pc_machine_init(void)
{
    qemu_register_machine(&pc_i440fx_machine_v1_7);
    qemu_register_machine(&pc_i440fx_machine_v1_6);
    qemu_register_machine(&pc_i440fx_machine_v1_5);
    qemu_register_machine(&pc_i440fx_machine_v1_4);
    qemu_register_machine(&pc_machine_v1_3);
    qemu_register_machine(&pc_machine_v1_2);
    qemu_register_machine(&pc_machine_v1_1);
    qemu_register_machine(&pc_machine_v1_0);
    qemu_register_machine(&pc_machine_v0_15);
    qemu_register_machine(&pc_machine_v0_14);
    qemu_register_machine(&pc_machine_v0_13);
    qemu_register_machine(&pc_machine_v0_12);
    qemu_register_machine(&pc_machine_v0_11);
    qemu_register_machine(&pc_machine_v0_10);
    qemu_register_machine(&isapc_machine);
#ifdef CONFIG_XEN
    qemu_register_machine(&xenfv_machine);
#endif
}

machine_init(pc_machine_init);
