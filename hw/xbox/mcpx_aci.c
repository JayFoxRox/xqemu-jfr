/*
 * QEMU MCPX Audio Codec Interface implementation
 *
 * Copyright (c) 2012 espes
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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/audio/ac97_int.h"

typedef struct MCPXACIState {
    AC97LinkState ac97; /* Includes the PCIDevice */
    MemoryRegion mmio;
} MCPXACIState;


#define MCPX_ACI_DEVICE(obj) \
    OBJECT_CHECK(MCPXACIState, (obj), "mcpx-aci")


static int mcpx_aci_initfn(PCIDevice *dev)
{
    MCPXACIState *d = MCPX_ACI_DEVICE(dev);
//return 0;
    assert(&d->ac97.dev == dev);
    assert(&d->ac97.dev == d);

    //mmio
    memory_region_init(&d->mmio, OBJECT(dev), "mcpx-aci-mmio", 0x1000);

    memory_region_init_io(&d->ac97.io_nam, OBJECT(dev), &ac97_io_nam_ops, &d->ac97,
                          "mcpx-aci-nam", 0x100);
    memory_region_init_io(&d->ac97.io_nabm, OBJECT(dev), &ac97_io_nabm_ops, &d->ac97,
                          "mcpx-aci-nabm", 0x80);

#if 1
#if 0
  // Works?! Maybe also no AC97 detected..
    pci_register_bar(&d->ac97.dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->ac97.io_nam);
    pci_register_bar(&d->ac97.dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->ac97.io_nabm);
#endif
#if 0
    memory_region_init_alias(&d->nam_mmio, NULL, &d->io_nam, 0, 0x100);
    memory_region_add_subregion(&d->mmio, 0x0, &d->nam_mmio);

    memory_region_init_alias(&d->nabm_mmio, NULL, &d->io_nabm, 0, 0x80);
    memory_region_add_subregion(&d->mmio, 0x100, &d->nabm_mmio);*/
#endif

  // Original code which worked pre-2.0.0 or 1.7.0 but fails?!
    memory_region_add_subregion(&d->mmio, 0x0, &d->ac97.io_nam);
    memory_region_add_subregion(&d->mmio, 0x100, &d->ac97.io_nabm);

    pci_register_bar(&d->ac97.dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
#endif

uint8_t* c = d->ac97.dev.config;
printf("c[PCI_INTERRUPT_PIN] = 0x%X\n",c[PCI_INTERRUPT_PIN]);
printf("c[PCI_VENDOR_ID] = 0x%X\n",c[PCI_VENDOR_ID]);
#if 1
    c[PCI_INTERRUPT_PIN] = 0x01;
    c[PCI_INTERRUPT_LINE] = 0x06;
/*
    c[PCI_COMMAND] = 0x00;      //pcicmd pci command rw, ro 
    c[PCI_COMMAND + 1] = 0x00;

    c[PCI_STATUS] = PCI_STATUS_FAST_BACK;      //pcists pci status rwc, ro
    c[PCI_STATUS + 1] = PCI_STATUS_DEVSEL_MEDIUM >> 8;

    c[PCI_CLASS_PROG] = 0x00;      // pi programming interface ro
*/
#endif

    ac97_common_init(&d->ac97);

    return 0;
}

static void mcpx_aci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_ACI;
    k->revision = 210;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->init = mcpx_aci_initfn;

    dc->desc = "MCPX Audio Codec Interface";
}

static const TypeInfo mcpx_aci_info = {
    .name          = "mcpx-aci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXACIState),
    .class_init    = mcpx_aci_class_init,
};

static void mcpx_aci_register(void)
{
    type_register_static(&mcpx_aci_info);
}
type_init(mcpx_aci_register);
