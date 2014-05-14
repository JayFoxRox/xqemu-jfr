/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "acpi-build.h"
#include <stddef.h>
#include <glib.h>
#include "qemu-common.h"
#include "qemu/bitmap.h"
#include "qemu/range.h"
#include "hw/pci/pci.h"
#include "qom/cpu.h"
#include "hw/i386/pc.h"
#include "target-i386/cpu.h"
#include "hw/timer/hpet.h"
#include "hw/i386/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "bios-linker-loader.h"
#include "hw/loader.h"

/* Supported chipsets: */
#include "hw/acpi/piix4.h"
#include "hw/i386/ich9.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/q35.h"

#include "hw/i386/q35-acpi-dsdt.hex"
#include "hw/i386/acpi-dsdt.hex"

#include "qapi/qmp/qint.h"
#include "qom/qom-qobject.h"

typedef struct AcpiCpuInfo {
    DECLARE_BITMAP(found_cpus, MAX_CPUMASK_BITS + 1);
} AcpiCpuInfo;

typedef struct AcpiMcfgInfo {
    uint64_t mcfg_base;
    uint32_t mcfg_size;
} AcpiMcfgInfo;

typedef struct AcpiPmInfo {
    bool s3_disabled;
    bool s4_disabled;
    uint8_t s4_val;
    uint16_t sci_int;
    uint8_t acpi_enable_cmd;
    uint8_t acpi_disable_cmd;
    uint32_t gpe0_blk;
    uint32_t gpe0_blk_len;
    uint32_t io_base;
} AcpiPmInfo;

typedef struct AcpiMiscInfo {
    bool has_hpet;
    DECLARE_BITMAP(slot_hotplug_enable, PCI_SLOT_MAX);
    const unsigned char *dsdt_code;
    unsigned dsdt_size;
    uint16_t pvpanic_port;
} AcpiMiscInfo;

static void acpi_get_dsdt(AcpiMiscInfo *info)
{
    Object *piix = piix4_pm_find();
    Object *lpc = ich9_lpc_find();
    assert(!!piix != !!lpc);

    if (piix) {
        info->dsdt_code = AcpiDsdtAmlCode;
        info->dsdt_size = sizeof AcpiDsdtAmlCode;
    }
    if (lpc) {
        info->dsdt_code = Q35AcpiDsdtAmlCode;
        info->dsdt_size = sizeof Q35AcpiDsdtAmlCode;
    }
}

static
int acpi_add_cpu_info(Object *o, void *opaque)
{
    AcpiCpuInfo *cpu = opaque;
    uint64_t apic_id;

    if (object_dynamic_cast(o, TYPE_CPU)) {
        apic_id = object_property_get_int(o, "apic-id", NULL);
        assert(apic_id <= MAX_CPUMASK_BITS);

        set_bit(apic_id, cpu->found_cpus);
    }

    object_child_foreach(o, acpi_add_cpu_info, opaque);
    return 0;
}

static void acpi_get_cpu_info(AcpiCpuInfo *cpu)
{
    Object *root = object_get_root();

    memset(cpu->found_cpus, 0, sizeof cpu->found_cpus);
    object_child_foreach(root, acpi_add_cpu_info, cpu);
}

static void acpi_get_pm_info(AcpiPmInfo *pm)
{
    Object *piix = piix4_pm_find();
    Object *lpc = ich9_lpc_find();
    Object *obj = NULL;
    QObject *o;

    if (piix) {
        obj = piix;
    }
    if (lpc) {
        obj = lpc;
    }
    assert(obj);

    /* Fill in optional s3/s4 related properties */
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S3_DISABLED, NULL);
    if (o) {
        pm->s3_disabled = qint_get_int(qobject_to_qint(o));
    } else {
        pm->s3_disabled = false;
    }
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S4_DISABLED, NULL);
    if (o) {
        pm->s4_disabled = qint_get_int(qobject_to_qint(o));
    } else {
        pm->s4_disabled = false;
    }
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S4_VAL, NULL);
    if (o) {
        pm->s4_val = qint_get_int(qobject_to_qint(o));
    } else {
        pm->s4_val = false;
    }

    /* Fill in mandatory properties */
    pm->sci_int = object_property_get_int(obj, ACPI_PM_PROP_SCI_INT, NULL);

    pm->acpi_enable_cmd = object_property_get_int(obj,
                                                  ACPI_PM_PROP_ACPI_ENABLE_CMD,
                                                  NULL);
    pm->acpi_disable_cmd = object_property_get_int(obj,
                                                  ACPI_PM_PROP_ACPI_DISABLE_CMD,
                                                  NULL);
    pm->io_base = object_property_get_int(obj, ACPI_PM_PROP_PM_IO_BASE,
                                          NULL);
    pm->gpe0_blk = object_property_get_int(obj, ACPI_PM_PROP_GPE0_BLK,
                                           NULL);
    pm->gpe0_blk_len = object_property_get_int(obj, ACPI_PM_PROP_GPE0_BLK_LEN,
                                               NULL);
}

static void acpi_get_hotplug_info(AcpiMiscInfo *misc)
{
    int i;
    PCIBus *bus = find_i440fx();

    if (!bus) {
        /* Only PIIX supports ACPI hotplug */
        memset(misc->slot_hotplug_enable, 0, sizeof misc->slot_hotplug_enable);
        return;
    }

    memset(misc->slot_hotplug_enable, 0xff,
           DIV_ROUND_UP(PCI_SLOT_MAX, BITS_PER_BYTE));

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        PCIDeviceClass *pc;
        PCIDevice *pdev = bus->devices[i];

        if (!pdev) {
            continue;
        }

        pc = PCI_DEVICE_GET_CLASS(pdev);

        if (pc->no_hotplug) {
            int slot = PCI_SLOT(i);

            clear_bit(slot, misc->slot_hotplug_enable);
        }
    }
}

static void acpi_get_misc_info(AcpiMiscInfo *info)
{
    info->has_hpet = hpet_find();
    info->pvpanic_port = pvpanic_port();
}

static void acpi_get_pci_info(PcPciInfo *info)
{
    Object *pci_host;
    bool ambiguous;

    pci_host = object_resolve_path_type("", TYPE_PCI_HOST_BRIDGE, &ambiguous);
    g_assert(!ambiguous);
    g_assert(pci_host);

    info->w32.begin = object_property_get_int(pci_host,
                                              PCI_HOST_PROP_PCI_HOLE_START,
                                              NULL);
    info->w32.end = object_property_get_int(pci_host,
                                            PCI_HOST_PROP_PCI_HOLE_END,
                                            NULL);
    info->w64.begin = object_property_get_int(pci_host,
                                              PCI_HOST_PROP_PCI_HOLE64_START,
                                              NULL);
    info->w64.end = object_property_get_int(pci_host,
                                            PCI_HOST_PROP_PCI_HOLE64_END,
                                            NULL);
}

#define ACPI_BUILD_APPNAME  "Bochs"
#define ACPI_BUILD_APPNAME6 "BOCHS "
#define ACPI_BUILD_APPNAME4 "BXPC"

#define ACPI_BUILD_DPRINTF(level, fmt, ...) do {} while (0)

#define ACPI_BUILD_TABLE_FILE "etc/acpi/tables"
#define ACPI_BUILD_RSDP_FILE "etc/acpi/rsdp"

static void
build_header(GArray *linker, GArray *table_data,
             AcpiTableHeader *h, uint32_t sig, int len, uint8_t rev)
{
    h->signature = cpu_to_le32(sig);
    h->length = cpu_to_le32(len);
    h->revision = rev;
    memcpy(h->oem_id, ACPI_BUILD_APPNAME6, 6);
    memcpy(h->oem_table_id, ACPI_BUILD_APPNAME4, 4);
    memcpy(h->oem_table_id + 4, (void *)&sig, 4);
    h->oem_revision = cpu_to_le32(1);
    memcpy(h->asl_compiler_id, ACPI_BUILD_APPNAME4, 4);
    h->asl_compiler_revision = cpu_to_le32(1);
    h->checksum = 0;
    /* Checksum to be filled in by Guest linker */
    bios_linker_loader_add_checksum(linker, ACPI_BUILD_TABLE_FILE,
                                    table_data->data, h, len, &h->checksum);
}

static inline GArray *build_alloc_array(void)
{
        return g_array_new(false, true /* clear */, 1);
}

static inline void build_free_array(GArray *array)
{
        g_array_free(array, true);
}

static inline void build_prepend_byte(GArray *array, uint8_t val)
{
    g_array_prepend_val(array, val);
}

static inline void build_append_byte(GArray *array, uint8_t val)
{
    g_array_append_val(array, val);
}

static inline void build_append_array(GArray *array, GArray *val)
{
    g_array_append_vals(array, val->data, val->len);
}

static void build_append_nameseg(GArray *array, const char *format, ...)
{
    /* It would be nicer to use g_string_vprintf but it's only there in 2.22 */
    char s[] = "XXXX";
    int len;
    va_list args;

    va_start(args, format);
    len = vsnprintf(s, sizeof s, format, args);
    va_end(args);

    assert(len == 4);
    g_array_append_vals(array, s, len);
}

/* 5.4 Definition Block Encoding */
enum {
    PACKAGE_LENGTH_1BYTE_SHIFT = 6, /* Up to 63 - use extra 2 bits. */
    PACKAGE_LENGTH_2BYTE_SHIFT = 4,
    PACKAGE_LENGTH_3BYTE_SHIFT = 12,
    PACKAGE_LENGTH_4BYTE_SHIFT = 20,
};

static void build_prepend_package_length(GArray *package, unsigned min_bytes)
{
    uint8_t byte;
    unsigned length = package->len;
    unsigned length_bytes;

    if (length + 1 < (1 << PACKAGE_LENGTH_1BYTE_SHIFT)) {
        length_bytes = 1;
    } else if (length + 2 < (1 << PACKAGE_LENGTH_3BYTE_SHIFT)) {
        length_bytes = 2;
    } else if (length + 3 < (1 << PACKAGE_LENGTH_4BYTE_SHIFT)) {
        length_bytes = 3;
    } else {
        length_bytes = 4;
    }

    /* Force length to at least min_bytes.
     * This wastes memory but that's how bios did it.
     */
    length_bytes = MAX(length_bytes, min_bytes);

    /* PkgLength is the length of the inclusive length of the data. */
    length += length_bytes;

    switch (length_bytes) {
    case 1:
        byte = length;
        build_prepend_byte(package, byte);
        return;
    case 4:
        byte = length >> PACKAGE_LENGTH_4BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_4BYTE_SHIFT) - 1;
        /* fall through */
    case 3:
        byte = length >> PACKAGE_LENGTH_3BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_3BYTE_SHIFT) - 1;
        /* fall through */
    case 2:
        byte = length >> PACKAGE_LENGTH_2BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_2BYTE_SHIFT) - 1;
        /* fall through */
    }
    /*
     * Most significant two bits of byte zero indicate how many following bytes
     * are in PkgLength encoding.
     */
    byte = ((length_bytes - 1) << PACKAGE_LENGTH_1BYTE_SHIFT) | length;
    build_prepend_byte(package, byte);
}

static void build_package(GArray *package, uint8_t op, unsigned min_bytes)
{
    build_prepend_package_length(package, min_bytes);
    build_prepend_byte(package, op);
}

static void build_append_value(GArray *table, uint32_t value, int size)
{
    uint8_t prefix;
    int i;

    switch (size) {
    case 1:
        prefix = 0x0A; /* BytePrefix */
        break;
    case 2:
        prefix = 0x0B; /* WordPrefix */
        break;
    case 4:
        prefix = 0x0C; /* DWordPrefix */
        break;
    default:
        assert(0);
        return;
    }
    build_append_byte(table, prefix);
    for (i = 0; i < size; ++i) {
        build_append_byte(table, value & 0xFF);
        value = value >> 8;
    }
}

static void build_append_notify_target(GArray *method, GArray *target_name,
                                       uint32_t value, int size)
{
    GArray *notify = build_alloc_array();
    uint8_t op = 0xA0; /* IfOp */

    build_append_byte(notify, 0x93); /* LEqualOp */
    build_append_byte(notify, 0x68); /* Arg0Op */
    build_append_value(notify, value, size);
    build_append_byte(notify, 0x86); /* NotifyOp */
    build_append_array(notify, target_name);
    build_append_byte(notify, 0x69); /* Arg1Op */

    /* Pack it up */
    build_package(notify, op, 1);

    build_append_array(method, notify);

    build_free_array(notify);
}

#define ACPI_PORT_SMI_CMD           0x00b2 /* TODO: this is APM_CNT_IOPORT */

static inline void *acpi_data_push(GArray *table_data, unsigned size)
{
    unsigned off = table_data->len;
    g_array_set_size(table_data, off + size);
    return table_data->data + off;
}

static unsigned acpi_data_len(GArray *table)
{
#if GLIB_CHECK_VERSION(2, 22, 0)
    assert(g_array_get_element_size(table) == 1);
#endif
    return table->len;
}

static void acpi_align_size(GArray *blob, unsigned align)
{
    /* Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

/* Get pointer within table in a safe manner */
#define ACPI_BUILD_PTR(table, size, off, type) \
    ((type *)(acpi_data_get_ptr(table, size, off, sizeof(type))))

static inline void *acpi_data_get_ptr(uint8_t *table_data, unsigned table_size,
                                      unsigned off, unsigned size)
{
    assert(off + size > off);
    assert(off + size <= table_size);
    return table_data + off;
}

static inline void acpi_add_table(GArray *table_offsets, GArray *table_data)
{
    uint32_t offset = cpu_to_le32(table_data->len);
    g_array_append_val(table_offsets, offset);
}

/* FACS */
static void
build_facs(GArray *table_data, GArray *linker, PcGuestInfo *guest_info)
{
    AcpiFacsDescriptorRev1 *facs = acpi_data_push(table_data, sizeof *facs);
    facs->signature = cpu_to_le32(ACPI_FACS_SIGNATURE);
    facs->length = cpu_to_le32(sizeof(*facs));
}

/* Load chipset information in FADT */
static void fadt_setup(AcpiFadtDescriptorRev1 *fadt, AcpiPmInfo *pm)
{
    fadt->model = 1;
    fadt->reserved1 = 0;
    fadt->sci_int = cpu_to_le16(pm->sci_int);
    fadt->smi_cmd = cpu_to_le32(ACPI_PORT_SMI_CMD);
    fadt->acpi_enable = pm->acpi_enable_cmd;
    fadt->acpi_disable = pm->acpi_disable_cmd;
    /* EVT, CNT, TMR offset matches hw/acpi/core.c */
    fadt->pm1a_evt_blk = cpu_to_le32(pm->io_base);
    fadt->pm1a_cnt_blk = cpu_to_le32(pm->io_base + 0x04);
    fadt->pm_tmr_blk = cpu_to_le32(pm->io_base + 0x08);
    fadt->gpe0_blk = cpu_to_le32(pm->gpe0_blk);
    /* EVT, CNT, TMR length matches hw/acpi/core.c */
    fadt->pm1_evt_len = 4;
    fadt->pm1_cnt_len = 2;
    fadt->pm_tmr_len = 4;
    fadt->gpe0_blk_len = pm->gpe0_blk_len;
    fadt->plvl2_lat = cpu_to_le16(0xfff); /* C2 state not supported */
    fadt->plvl3_lat = cpu_to_le16(0xfff); /* C3 state not supported */
    fadt->flags = cpu_to_le32((1 << ACPI_FADT_F_WBINVD) |
                              (1 << ACPI_FADT_F_PROC_C1) |
                              (1 << ACPI_FADT_F_SLP_BUTTON) |
                              (1 << ACPI_FADT_F_RTC_S4));
    fadt->flags |= cpu_to_le32(1 << ACPI_FADT_F_USE_PLATFORM_CLOCK);
}


/* FADT */
static void
build_fadt(GArray *table_data, GArray *linker, AcpiPmInfo *pm,
           unsigned facs, unsigned dsdt)
{
    AcpiFadtDescriptorRev1 *fadt = acpi_data_push(table_data, sizeof(*fadt));

    fadt->firmware_ctrl = cpu_to_le32(facs);
    /* FACS address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
                                   ACPI_BUILD_TABLE_FILE,
                                   table_data, &fadt->firmware_ctrl,
                                   sizeof fadt->firmware_ctrl);

    fadt->dsdt = cpu_to_le32(dsdt);
    /* DSDT address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
                                   ACPI_BUILD_TABLE_FILE,
                                   table_data, &fadt->dsdt,
                                   sizeof fadt->dsdt);

    fadt_setup(fadt, pm);

    build_header(linker, table_data,
                 (void *)fadt, ACPI_FACP_SIGNATURE, sizeof(*fadt), 1);
}

static void
build_madt(GArray *table_data, GArray *linker, AcpiCpuInfo *cpu,
           PcGuestInfo *guest_info)
{
    int madt_start = table_data->len;

    AcpiMultipleApicTable *madt;
    AcpiMadtIoApic *io_apic;
    AcpiMadtIntsrcovr *intsrcovr;
    AcpiMadtLocalNmi *local_nmi;
    int i;

    madt = acpi_data_push(table_data, sizeof *madt);
    madt->local_apic_address = cpu_to_le32(APIC_DEFAULT_ADDRESS);
    madt->flags = cpu_to_le32(1);

    for (i = 0; i < guest_info->apic_id_limit; i++) {
        AcpiMadtProcessorApic *apic = acpi_data_push(table_data, sizeof *apic);
        apic->type = ACPI_APIC_PROCESSOR;
        apic->length = sizeof(*apic);
        apic->processor_id = i;
        apic->local_apic_id = i;
        if (test_bit(i, cpu->found_cpus)) {
            apic->flags = cpu_to_le32(1);
        } else {
            apic->flags = cpu_to_le32(0);
        }
    }
    io_apic = acpi_data_push(table_data, sizeof *io_apic);
    io_apic->type = ACPI_APIC_IO;
    io_apic->length = sizeof(*io_apic);
#define ACPI_BUILD_IOAPIC_ID 0x0
    io_apic->io_apic_id = ACPI_BUILD_IOAPIC_ID;
    io_apic->address = cpu_to_le32(IO_APIC_DEFAULT_ADDRESS);
    io_apic->interrupt = cpu_to_le32(0);

    if (guest_info->apic_xrupt_override) {
        intsrcovr = acpi_data_push(table_data, sizeof *intsrcovr);
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = 0;
        intsrcovr->gsi    = cpu_to_le32(2);
        intsrcovr->flags  = cpu_to_le16(0); /* conforms to bus specifications */
    }
    for (i = 1; i < 16; i++) {
#define ACPI_BUILD_PCI_IRQS ((1<<5) | (1<<9) | (1<<10) | (1<<11))
        if (!(ACPI_BUILD_PCI_IRQS & (1 << i))) {
            /* No need for a INT source override structure. */
            continue;
        }
        intsrcovr = acpi_data_push(table_data, sizeof *intsrcovr);
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = i;
        intsrcovr->gsi    = cpu_to_le32(i);
        intsrcovr->flags  = cpu_to_le16(0xd); /* active high, level triggered */
    }

    local_nmi = acpi_data_push(table_data, sizeof *local_nmi);
    local_nmi->type         = ACPI_APIC_LOCAL_NMI;
    local_nmi->length       = sizeof(*local_nmi);
    local_nmi->processor_id = 0xff; /* all processors */
    local_nmi->flags        = cpu_to_le16(0);
    local_nmi->lint         = 1; /* ACPI_LINT1 */

    build_header(linker, table_data,
                 (void *)(table_data->data + madt_start), ACPI_APIC_SIGNATURE,
                 table_data->len - madt_start, 1);
}

/* Encode a hex value */
static inline char acpi_get_hex(uint32_t val)
{
    val &= 0x0f;
    return (val <= 9) ? ('0' + val) : ('A' + val - 10);
}

#include "hw/i386/ssdt-proc.hex"

/* 0x5B 0x83 ProcessorOp PkgLength NameString ProcID */
#define ACPI_PROC_OFFSET_CPUHEX (*ssdt_proc_name - *ssdt_proc_start + 2)
#define ACPI_PROC_OFFSET_CPUID1 (*ssdt_proc_name - *ssdt_proc_start + 4)
#define ACPI_PROC_OFFSET_CPUID2 (*ssdt_proc_id - *ssdt_proc_start)
#define ACPI_PROC_SIZEOF (*ssdt_proc_end - *ssdt_proc_start)
#define ACPI_PROC_AML (ssdp_proc_aml + *ssdt_proc_start)

/* 0x5B 0x82 DeviceOp PkgLength NameString */
#define ACPI_PCIHP_OFFSET_HEX (*ssdt_pcihp_name - *ssdt_pcihp_start + 1)
#define ACPI_PCIHP_OFFSET_ID (*ssdt_pcihp_id - *ssdt_pcihp_start)
#define ACPI_PCIHP_OFFSET_ADR (*ssdt_pcihp_adr - *ssdt_pcihp_start)
#define ACPI_PCIHP_OFFSET_EJ0 (*ssdt_pcihp_ej0 - *ssdt_pcihp_start)
#define ACPI_PCIHP_SIZEOF (*ssdt_pcihp_end - *ssdt_pcihp_start)
#define ACPI_PCIHP_AML (ssdp_pcihp_aml + *ssdt_pcihp_start)

#define ACPI_SSDT_SIGNATURE 0x54445353 /* SSDT */
#define ACPI_SSDT_HEADER_LENGTH 36

#include "hw/i386/ssdt-misc.hex"
#include "hw/i386/ssdt-pcihp.hex"

static void
build_append_notify(GArray *device, const char *name,
                    const char *format, int skip, int count)
{
    int i;
    GArray *method = build_alloc_array();
    uint8_t op = 0x14; /* MethodOp */

    build_append_nameseg(method, name);
    build_append_byte(method, 0x02); /* MethodFlags: ArgCount */
    for (i = skip; i < count; i++) {
        GArray *target = build_alloc_array();
        build_append_nameseg(target, format, i);
        assert(i < 256); /* Fits in 1 byte */
        build_append_notify_target(method, target, i, 1);
        build_free_array(target);
    }
    build_package(method, op, 2);

    build_append_array(device, method);
    build_free_array(method);
}

static void patch_pcihp(int slot, uint8_t *ssdt_ptr, uint32_t eject)
{
    ssdt_ptr[ACPI_PCIHP_OFFSET_HEX] = acpi_get_hex(slot >> 4);
    ssdt_ptr[ACPI_PCIHP_OFFSET_HEX + 1] = acpi_get_hex(slot);
    ssdt_ptr[ACPI_PCIHP_OFFSET_ID] = slot;
    ssdt_ptr[ACPI_PCIHP_OFFSET_ADR + 2] = slot;

    /* Runtime patching of ACPI_EJ0: to disable hotplug for a slot,
     * replace the method name: _EJ0 by ACPI_EJ0_.
     */
    /* Sanity check */
    assert(!memcmp(ssdt_ptr + ACPI_PCIHP_OFFSET_EJ0, "_EJ0", 4));

    if (!eject) {
        memcpy(ssdt_ptr + ACPI_PCIHP_OFFSET_EJ0, "EJ0_", 4);
    }
}

static void patch_pci_windows(PcPciInfo *pci, uint8_t *start, unsigned size)
{
    *ACPI_BUILD_PTR(start, size, acpi_pci32_start[0], uint32_t) =
        cpu_to_le32(pci->w32.begin);

    *ACPI_BUILD_PTR(start, size, acpi_pci32_end[0], uint32_t) =
        cpu_to_le32(pci->w32.end - 1);

    if (pci->w64.end || pci->w64.begin) {
        *ACPI_BUILD_PTR(start, size, acpi_pci64_valid[0], uint8_t) = 1;
        *ACPI_BUILD_PTR(start, size, acpi_pci64_start[0], uint64_t) =
            cpu_to_le64(pci->w64.begin);
        *ACPI_BUILD_PTR(start, size, acpi_pci64_end[0], uint64_t) =
            cpu_to_le64(pci->w64.end - 1);
        *ACPI_BUILD_PTR(start, size, acpi_pci64_length[0], uint64_t) =
            cpu_to_le64(pci->w64.end - pci->w64.begin);
    } else {
        *ACPI_BUILD_PTR(start, size, acpi_pci64_valid[0], uint8_t) = 0;
    }
}

static void
build_ssdt(GArray *table_data, GArray *linker,
           AcpiCpuInfo *cpu, AcpiPmInfo *pm, AcpiMiscInfo *misc,
           PcPciInfo *pci, PcGuestInfo *guest_info)
{
    int acpi_cpus = MIN(0xff, guest_info->apic_id_limit);
    int ssdt_start = table_data->len;
    uint8_t *ssdt_ptr;
    int i;

    /* Copy header and patch values in the S3_ / S4_ / S5_ packages */
    ssdt_ptr = acpi_data_push(table_data, sizeof(ssdp_misc_aml));
    memcpy(ssdt_ptr, ssdp_misc_aml, sizeof(ssdp_misc_aml));
    if (pm->s3_disabled) {
        ssdt_ptr[acpi_s3_name[0]] = 'X';
    }
    if (pm->s4_disabled) {
        ssdt_ptr[acpi_s4_name[0]] = 'X';
    } else {
        ssdt_ptr[acpi_s4_pkg[0] + 1] = ssdt_ptr[acpi_s4_pkg[0] + 3] =
            pm->s4_val;
    }

    patch_pci_windows(pci, ssdt_ptr, sizeof(ssdp_misc_aml));

    *(uint16_t *)(ssdt_ptr + *ssdt_isa_pest) =
        cpu_to_le16(misc->pvpanic_port);

    {
        GArray *sb_scope = build_alloc_array();
        uint8_t op = 0x10; /* ScopeOp */

        build_append_nameseg(sb_scope, "_SB_");

        /* build Processor object for each processor */
        for (i = 0; i < acpi_cpus; i++) {
            uint8_t *proc = acpi_data_push(sb_scope, ACPI_PROC_SIZEOF);
            memcpy(proc, ACPI_PROC_AML, ACPI_PROC_SIZEOF);
            proc[ACPI_PROC_OFFSET_CPUHEX] = acpi_get_hex(i >> 4);
            proc[ACPI_PROC_OFFSET_CPUHEX+1] = acpi_get_hex(i);
            proc[ACPI_PROC_OFFSET_CPUID1] = i;
            proc[ACPI_PROC_OFFSET_CPUID2] = i;
        }

        /* build this code:
         *   Method(NTFY, 2) {If (LEqual(Arg0, 0x00)) {Notify(CP00, Arg1)} ...}
         */
        /* Arg0 = Processor ID = APIC ID */
        build_append_notify(sb_scope, "NTFY", "CP%0.02X", 0, acpi_cpus);

        /* build "Name(CPON, Package() { One, One, ..., Zero, Zero, ... })" */
        build_append_byte(sb_scope, 0x08); /* NameOp */
        build_append_nameseg(sb_scope, "CPON");

        {
            GArray *package = build_alloc_array();
            uint8_t op = 0x12; /* PackageOp */

            build_append_byte(package, acpi_cpus); /* NumElements */
            for (i = 0; i < acpi_cpus; i++) {
                uint8_t b = test_bit(i, cpu->found_cpus) ? 0x01 : 0x00;
                build_append_byte(package, b);
            }

            build_package(package, op, 2);
            build_append_array(sb_scope, package);
            build_free_array(package);
        }

        {
            GArray *pci0 = build_alloc_array();
            uint8_t op = 0x10; /* ScopeOp */;

            build_append_nameseg(pci0, "PCI0");

            /* build Device object for each slot */
            for (i = 1; i < PCI_SLOT_MAX; i++) {
                bool eject = test_bit(i, misc->slot_hotplug_enable);
                void *pcihp = acpi_data_push(pci0, ACPI_PCIHP_SIZEOF);

                memcpy(pcihp, ACPI_PCIHP_AML, ACPI_PCIHP_SIZEOF);
                patch_pcihp(i, pcihp, eject);
            }

            build_append_notify(pci0, "PCNT", "S%0.02X_", 1, PCI_SLOT_MAX);
            build_package(pci0, op, 3);
            build_append_array(sb_scope, pci0);
            build_free_array(pci0);
        }

        build_package(sb_scope, op, 3);
        build_append_array(table_data, sb_scope);
        build_free_array(sb_scope);
    }

    build_header(linker, table_data,
                 (void *)(table_data->data + ssdt_start),
                 ACPI_SSDT_SIGNATURE, table_data->len - ssdt_start, 1);
}

static void
build_hpet(GArray *table_data, GArray *linker)
{
    Acpi20Hpet *hpet;

    hpet = acpi_data_push(table_data, sizeof(*hpet));
    /* Note timer_block_id value must be kept in sync with value advertised by
     * emulated hpet
     */
    hpet->timer_block_id = cpu_to_le32(0x8086a201);
    hpet->addr.address = cpu_to_le64(HPET_BASE);
    build_header(linker, table_data,
                 (void *)hpet, ACPI_HPET_SIGNATURE, sizeof(*hpet), 1);
}

static void
acpi_build_srat_memory(AcpiSratMemoryAffinity *numamem,
                       uint64_t base, uint64_t len, int node, int enabled)
{
    numamem->type = ACPI_SRAT_MEMORY;
    numamem->length = sizeof(*numamem);
    memset(numamem->proximity, 0, 4);
    numamem->proximity[0] = node;
    numamem->flags = cpu_to_le32(!!enabled);
    numamem->base_addr = cpu_to_le64(base);
    numamem->range_length = cpu_to_le64(len);
}

static void
build_srat(GArray *table_data, GArray *linker,
           AcpiCpuInfo *cpu, PcGuestInfo *guest_info)
{
    AcpiSystemResourceAffinityTable *srat;
    AcpiSratProcessorAffinity *core;
    AcpiSratMemoryAffinity *numamem;

    int i;
    uint64_t curnode;
    int srat_start, numa_start, slots;
    uint64_t mem_len, mem_base, next_base;

    srat_start = table_data->len;

    srat = acpi_data_push(table_data, sizeof *srat);
    srat->reserved1 = cpu_to_le32(1);
    core = (void *)(srat + 1);

    for (i = 0; i < guest_info->apic_id_limit; ++i) {
        core = acpi_data_push(table_data, sizeof *core);
        core->type = ACPI_SRAT_PROCESSOR;
        core->length = sizeof(*core);
        core->local_apic_id = i;
        curnode = guest_info->node_cpu[i];
        core->proximity_lo = curnode;
        memset(core->proximity_hi, 0, 3);
        core->local_sapic_eid = 0;
        if (test_bit(i, cpu->found_cpus)) {
            core->flags = cpu_to_le32(1);
        } else {
            core->flags = cpu_to_le32(0);
        }
    }


    /* the memory map is a bit tricky, it contains at least one hole
     * from 640k-1M and possibly another one from 3.5G-4G.
     */
    next_base = 0;
    numa_start = table_data->len;

    numamem = acpi_data_push(table_data, sizeof *numamem);
    acpi_build_srat_memory(numamem, 0, 640*1024, 0, 1);
    next_base = 1024 * 1024;
    for (i = 1; i < guest_info->numa_nodes + 1; ++i) {
        mem_base = next_base;
        mem_len = guest_info->node_mem[i - 1];
        if (i == 1) {
            mem_len -= 1024 * 1024;
        }
        next_base = mem_base + mem_len;

        /* Cut out the ACPI_PCI hole */
        if (mem_base <= guest_info->ram_size &&
            next_base > guest_info->ram_size) {
            mem_len -= next_base - guest_info->ram_size;
            if (mem_len > 0) {
                numamem = acpi_data_push(table_data, sizeof *numamem);
                acpi_build_srat_memory(numamem, mem_base, mem_len, i-1, 1);
            }
            mem_base = 1ULL << 32;
            mem_len = next_base - guest_info->ram_size;
            next_base += (1ULL << 32) - guest_info->ram_size;
        }
        numamem = acpi_data_push(table_data, sizeof *numamem);
        acpi_build_srat_memory(numamem, mem_base, mem_len, i - 1, 1);
    }
    slots = (table_data->len - numa_start) / sizeof *numamem;
    for (; slots < guest_info->numa_nodes + 2; slots++) {
        numamem = acpi_data_push(table_data, sizeof *numamem);
        acpi_build_srat_memory(numamem, 0, 0, 0, 0);
    }

    build_header(linker, table_data,
                 (void *)(table_data->data + srat_start),
                 ACPI_SRAT_SIGNATURE,
                 table_data->len - srat_start, 1);
}

static void
build_mcfg_q35(GArray *table_data, GArray *linker, AcpiMcfgInfo *info)
{
    AcpiTableMcfg *mcfg;
    uint32_t sig;
    int len = sizeof(*mcfg) + 1 * sizeof(mcfg->allocation[0]);

    mcfg = acpi_data_push(table_data, len);
    mcfg->allocation[0].address = cpu_to_le64(info->mcfg_base);
    /* Only a single allocation so no need to play with segments */
    mcfg->allocation[0].pci_segment = cpu_to_le16(0);
    mcfg->allocation[0].start_bus_number = 0;
    mcfg->allocation[0].end_bus_number = PCIE_MMCFG_BUS(info->mcfg_size - 1);

    /* MCFG is used for ECAM which can be enabled or disabled by guest.
     * To avoid table size changes (which create migration issues),
     * always create the table even if there are no allocations,
     * but set the signature to a reserved value in this case.
     * ACPI spec requires OSPMs to ignore such tables.
     */
    if (info->mcfg_base == PCIE_BASE_ADDR_UNMAPPED) {
        sig = ACPI_RSRV_SIGNATURE;
    } else {
        sig = ACPI_MCFG_SIGNATURE;
    }
    build_header(linker, table_data, (void *)mcfg, sig, len, 1);
}

static void
build_dsdt(GArray *table_data, GArray *linker, AcpiMiscInfo *misc)
{
    void *dsdt;
    assert(misc->dsdt_code && misc->dsdt_size);
    dsdt = acpi_data_push(table_data, misc->dsdt_size);
    memcpy(dsdt, misc->dsdt_code, misc->dsdt_size);
}

/* Build final rsdt table */
static void
build_rsdt(GArray *table_data, GArray *linker, GArray *table_offsets)
{
    AcpiRsdtDescriptorRev1 *rsdt;
    size_t rsdt_len;
    int i;

    rsdt_len = sizeof(*rsdt) + sizeof(uint32_t) * table_offsets->len;
    rsdt = acpi_data_push(table_data, rsdt_len);
    memcpy(rsdt->table_offset_entry, table_offsets->data,
           sizeof(uint32_t) * table_offsets->len);
    for (i = 0; i < table_offsets->len; ++i) {
        /* rsdt->table_offset_entry to be filled by Guest linker */
        bios_linker_loader_add_pointer(linker,
                                       ACPI_BUILD_TABLE_FILE,
                                       ACPI_BUILD_TABLE_FILE,
                                       table_data, &rsdt->table_offset_entry[i],
                                       sizeof(uint32_t));
    }
    build_header(linker, table_data,
                 (void *)rsdt, ACPI_RSDT_SIGNATURE, rsdt_len, 1);
}

static GArray *
build_rsdp(GArray *rsdp_table, GArray *linker, unsigned rsdt)
{
    AcpiRsdpDescriptor *rsdp = acpi_data_push(rsdp_table, sizeof *rsdp);

    bios_linker_loader_alloc(linker, ACPI_BUILD_RSDP_FILE, 1,
                             true /* fseg memory */);

    rsdp->signature = cpu_to_le64(ACPI_RSDP_SIGNATURE);
    memcpy(rsdp->oem_id, ACPI_BUILD_APPNAME6, 6);
    rsdp->rsdt_physical_address = cpu_to_le32(rsdt);
    /* Address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_RSDP_FILE,
                                   ACPI_BUILD_TABLE_FILE,
                                   rsdp_table, &rsdp->rsdt_physical_address,
                                   sizeof rsdp->rsdt_physical_address);
    rsdp->checksum = 0;
    /* Checksum to be filled by Guest linker */
    bios_linker_loader_add_checksum(linker, ACPI_BUILD_RSDP_FILE,
                                    rsdp, rsdp, sizeof *rsdp, &rsdp->checksum);

    return rsdp_table;
}

typedef
struct AcpiBuildTables {
    GArray *table_data;
    GArray *rsdp;
    GArray *linker;
} AcpiBuildTables;

static inline void acpi_build_tables_init(AcpiBuildTables *tables)
{
    tables->rsdp = g_array_new(false, true /* clear */, 1);
    tables->table_data = g_array_new(false, true /* clear */, 1);
    tables->linker = bios_linker_loader_init();
}

static inline void acpi_build_tables_cleanup(AcpiBuildTables *tables, bool mfre)
{
    void *linker_data = bios_linker_loader_cleanup(tables->linker);
    if (mfre) {
        g_free(linker_data);
    }
    g_array_free(tables->rsdp, mfre);
    g_array_free(tables->table_data, mfre);
}

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    uint8_t *table_ram;
    uint32_t table_size;
    /* Is table patched? */
    uint8_t patched;
    PcGuestInfo *guest_info;
} AcpiBuildState;

static bool acpi_get_mcfg(AcpiMcfgInfo *mcfg)
{
    Object *pci_host;
    QObject *o;
    bool ambiguous;

    pci_host = object_resolve_path_type("", TYPE_PCI_HOST_BRIDGE, &ambiguous);
    g_assert(!ambiguous);
    g_assert(pci_host);

    o = object_property_get_qobject(pci_host, PCIE_HOST_MCFG_BASE, NULL);
    if (!o) {
        return false;
    }
    mcfg->mcfg_base = qint_get_int(qobject_to_qint(o));

    o = object_property_get_qobject(pci_host, PCIE_HOST_MCFG_SIZE, NULL);
    assert(o);
    mcfg->mcfg_size = qint_get_int(qobject_to_qint(o));
    return true;
}

static
void acpi_build(PcGuestInfo *guest_info, AcpiBuildTables *tables)
{
    GArray *table_offsets;
    unsigned facs, dsdt, rsdt;
    AcpiCpuInfo cpu;
    AcpiPmInfo pm;
    AcpiMiscInfo misc;
    AcpiMcfgInfo mcfg;
    PcPciInfo pci;
    uint8_t *u;

    acpi_get_cpu_info(&cpu);
    acpi_get_pm_info(&pm);
    acpi_get_dsdt(&misc);
    acpi_get_hotplug_info(&misc);
    acpi_get_misc_info(&misc);
    acpi_get_pci_info(&pci);

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));
    ACPI_BUILD_DPRINTF(3, "init ACPI tables\n");

    bios_linker_loader_alloc(tables->linker, ACPI_BUILD_TABLE_FILE,
                             64 /* Ensure FACS is aligned */,
                             false /* high memory */);

    /*
     * FACS is pointed to by FADT.
     * We place it first since it's the only table that has alignment
     * requirements.
     */
    facs = tables->table_data->len;
    build_facs(tables->table_data, tables->linker, guest_info);

    /* DSDT is pointed to by FADT */
    dsdt = tables->table_data->len;
    build_dsdt(tables->table_data, tables->linker, &misc);

    /* ACPI tables pointed to by RSDT */
    acpi_add_table(table_offsets, tables->table_data);
    build_fadt(tables->table_data, tables->linker, &pm, facs, dsdt);
    acpi_add_table(table_offsets, tables->table_data);

    build_ssdt(tables->table_data, tables->linker, &cpu, &pm, &misc, &pci,
               guest_info);
    acpi_add_table(table_offsets, tables->table_data);

    build_madt(tables->table_data, tables->linker, &cpu, guest_info);
    acpi_add_table(table_offsets, tables->table_data);
    if (misc.has_hpet) {
        build_hpet(tables->table_data, tables->linker);
    }
    if (guest_info->numa_nodes) {
        acpi_add_table(table_offsets, tables->table_data);
        build_srat(tables->table_data, tables->linker, &cpu, guest_info);
    }
    if (acpi_get_mcfg(&mcfg)) {
        acpi_add_table(table_offsets, tables->table_data);
        build_mcfg_q35(tables->table_data, tables->linker, &mcfg);
    }

    /* Add tables supplied by user (if any) */
    for (u = acpi_table_first(); u; u = acpi_table_next(u)) {
        unsigned len = acpi_table_len(u);

        acpi_add_table(table_offsets, tables->table_data);
        g_array_append_vals(tables->table_data, u, len);
    }

    /* RSDT is pointed to by RSDP */
    rsdt = tables->table_data->len;
    build_rsdt(tables->table_data, tables->linker, table_offsets);

    /* RSDP is in FSEG memory, so allocate it separately */
    build_rsdp(tables->rsdp, tables->linker, rsdt);

    /* We'll expose it all to Guest so align size to reduce
     * chance of size changes.
     * RSDP is small so it's easy to keep it immutable, no need to
     * bother with alignment.
     */
    acpi_align_size(tables->table_data, 0x1000);

    acpi_align_size(tables->linker, 0x1000);

    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
}

static void acpi_build_update(void *build_opaque, uint32_t offset)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = 1;

    acpi_build_tables_init(&tables);

    acpi_build(build_state->guest_info, &tables);

    assert(acpi_data_len(tables.table_data) == build_state->table_size);
    memcpy(build_state->table_ram, tables.table_data->data,
           build_state->table_size);

    acpi_build_tables_cleanup(&tables, true);
}

static void acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = 0;
}

static void *acpi_add_rom_blob(AcpiBuildState *build_state, GArray *blob,
                               const char *name)
{
    return rom_add_blob(name, blob->data, acpi_data_len(blob), -1, name,
                        acpi_build_update, build_state);
}

static const VMStateDescription vmstate_acpi_build = {
    .name = "acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void acpi_setup(PcGuestInfo *guest_info)
{
    AcpiBuildTables tables;
    AcpiBuildState *build_state;

    if (!guest_info->fw_cfg) {
        ACPI_BUILD_DPRINTF(3, "No fw cfg. Bailing out.\n");
        return;
    }

    if (!guest_info->has_acpi_build) {
        ACPI_BUILD_DPRINTF(3, "ACPI build disabled. Bailing out.\n");
        return;
    }

    if (!acpi_enabled) {
        ACPI_BUILD_DPRINTF(3, "ACPI disabled. Bailing out.\n");
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    build_state->guest_info = guest_info;

    acpi_build_tables_init(&tables);
    acpi_build(build_state->guest_info, &tables);

    /* Now expose it all to Guest */
    build_state->table_ram = acpi_add_rom_blob(build_state, tables.table_data,
                                               ACPI_BUILD_TABLE_FILE);
    build_state->table_size = acpi_data_len(tables.table_data);

    acpi_add_rom_blob(NULL, tables.linker, "etc/table-loader");

    /*
     * RSDP is small so it's easy to keep it immutable, no need to
     * bother with ROM blobs.
     */
    fw_cfg_add_file(guest_info->fw_cfg, ACPI_BUILD_RSDP_FILE,
                    tables.rsdp->data, acpi_data_len(tables.rsdp));

    qemu_register_reset(acpi_build_reset, build_state);
    acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
