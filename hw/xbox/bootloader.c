/*
 * QEMU Xbox bootloader implementation
 *
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

/*
    See http://hackspot.net/XboxBlog/?p=1 for more info 

    Notes:

    - Arguments/Keys can be anywhere because they will be copied from kernel..
      just make sure we are not coliding with kernel and data
*/


//FIXME: tons of useless include here
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "exec/memory.h"
#include "qemu/config-file.h"
#include "target-i386/helper.h"
#include "hw/xbox/xbox.h"
#include "exec/cpu-all.h"

#include "hw/xbox/bootloader.h"

#include "arguments.fox" /* FIXME: local file.. */
#include "cheat.fox" /* FIXME: local file.. */
#define warningPrintf(x,...) printf(x, ## __VA_ARGS__)
#define errorPrintf(x,...) fprintf(stderr,x, ## __VA_ARGS__);
#define debugPrintf(x,...) fprintf(stdout,x, ## __VA_ARGS__);

/*FIXME: hwaddr type! */
const uint32_t bootloader_size = 0x6000;
const uint32_t bootloader_original_address = 0x80090000;
const uint32_t bootloader_address = 0x80400000;
const uint32_t bootloader_physical_address = 0x80400000;
const uint32_t kernel_address = 0x80010000;
const uint32_t preloader_stack = 0x0008F000;
/*
    The arguments are hardcoded in the ROM image.. yep.
*/
const uint32_t kernel_arguments_address = bootloader_address;
/*
    The kernel keys are normally part of the 2BL image, however, we don't have
    that when using HLE, so we put it where code would normally end up
*/
const uint32_t kernel_keys_address = bootloader_address+64;

static void bootloader_prepare_kernel_registers(void)
{

    X86CPU *cpu = X86_CPU(first_cpu);
    CPUX86State *env = &cpu->env;
    
    /* Load the arguments into xbox RAM */
    {
        /*FIXME: Zero terminated? it's 64 bytes max though */
        const char* kernel_arguments = jayfoxrox_kernel_arguments; //FIXME: cli
        assert((strlen(kernel_arguments)+1) <= 64);
        cpu_physical_memory_write(kernel_arguments_address, kernel_arguments, strlen(kernel_arguments)+1);
    }

    /* Load the keys from file to RAM */
    {
        uint8_t kernel_keys[32];
        const char* kernel_keys_filename = jayfoxrox_kernel_keys_filename; //FIXME: cli
        int kernel_keys_size = get_image_size(kernel_keys_filename);
        assert(kernel_keys_size == 32);

        int fd = open(kernel_keys_filename, O_RDONLY | O_BINARY);
        if (fd == -1) {
            fprintf(stderr, "qemu: could not load xbox kernel keys '%s'\n", kernel_keys_filename);
            exit(1);
        }
        assert(fd != -1);
        ssize_t rc = read(fd, kernel_keys, kernel_keys_size);
        assert(rc == kernel_keys_size);
        close(fd);

        cpu_physical_memory_write(kernel_keys_address, kernel_keys, kernel_keys_size);
    }  

    /*
        FIXME: If the kernel NT header can't be found we are supposed to
        shut down. The real loader uses RtlImageNtHeader and probably
        checks for more
    */

    /*FIXME: hwaddr type */
    uint32_t kernel_nt_header = kernel_address;
    uint32_t entry_point = jayfoxrox_kernel_entry_point;

    /* Put pointers on stack and jump to entry point */
    {    

        struct {
            uint32_t args; /* Pointer to 64 bytes */
            uint32_t keys; /* Pointer to 32 bytes */
        } kernel_parameters;

        kernel_parameters.args = kernel_arguments_address;
        kernel_parameters.keys = kernel_keys_address;

        env->regs[R_ESP] -= 8;
        cpu_physical_memory_write(env->regs[R_ESP], &kernel_parameters,
                                  sizeof(kernel_parameters));
        env->eip = entry_point;

        printf("Trying to boot kernel from 0x%X\n",env->eip);

    }

}

static void bootloader_shutdown_system(void)
{
    /* FIXME: I forgot to copy this from IDA.. meh! */
    assert(0);
}

/* PIC watchdog code from http://www.xbox-linux.org/docs/pichandshake.html */
static uint16_t bootloader_smc_read_command(uint8_t command, bool byte)
{
    uint16_t status;
    uint16_t data;
    helper_outb(0xC004,0x21); /* Address */
    helper_outb(0xC008,command); /* Command */
    status = helper_inw(0xC000); /* read status */
    helper_outw(0xC000,status); /* write back status */
    helper_outb(0xC002,byte?0x0A:0x0B); /* start */
    while(1) {
      status = helper_inb(0xC000); /* read status */
      if (!(status & 0x08)) { break; }
    }
    /* collision? go back to top and re-send address and data */
    if (status & 0x02) {
        return bootloader_smc_read_command(command,byte);
    }
    /* Other errors.. */
    if((status & 0x20) || (status & 0x04) || (!(status & 0x10))) {
        assert(0);
    }

    /* Read value and return */
    data = helper_inw(0xC006);
    if (byte) {
        return data & 0x00FF;
    } else {
        return data;
    }
}

static void bootloader_smc_write_command(uint8_t command, bool byte, uint16_t value)
{
    uint8_t status;
    helper_outb(0xC004, 0x20 & 0xFE); /* address; the lower bit of the address
                                         is clipped in this case */
    helper_outb(0xC008, command); /* command */
    /*FIXME: What happens with words in byte writes? */
    helper_outw(0xC006, value); /* data */
        status = helper_inw(0xC000); /* read status */
    helper_outw(0xC000,status); /* write back status */
    helper_outb(0xC002,byte?0x0A:0x0B); /* start */
    while(1) {
      status = helper_inb(0xC000); /* read status */
      if (!(status & 0x08)) { break; }
    }
    /* collision? go back to top and re-send address and data */
    if (status & 0x02) {
        return bootloader_smc_write_command(command,byte,value);
    }
    /* Other errors.. */
    if((status & 0x20) || (status & 0x04) || (!(status & 0x10))) {
        assert(0);
    }

    return;
}

static void bootloader_stop_smc_watchdog(void) {
    uint8_t challenge[4];
    uint8_t response[2];
    unsigned int i;

    challenge[0] = bootloader_smc_read_command(0x1c,true);
    challenge[1] = bootloader_smc_read_command(0x1d,true);
    challenge[2] = bootloader_smc_read_command(0x1e,true);
    challenge[3] = bootloader_smc_read_command(0x1f,true);

    /*
        FIXME: Some MS bootloaders seem to shut down if challenge is 0x00000000!
               We can't really do that because we might want to use debug imges
               which will always return that..
    */
    if (0) {
        bootloader_shutdown_system();
    }

    uint8_t t1 = (challenge[0] << 2) ^ (challenge[1] + 0x39) ^
                 (challenge[2] >> 2) ^ (challenge[3] + 0x63);    
    uint8_t t2 = (challenge[0] + 0x0b) ^ (challenge[1] >> 2) ^
                 (challenge[2] + 0x1b);

    response[0] = 0x33;
    response[1] = 0xed;

    for(i = 0; i < 4; i++) {
        response[0] += response[1] ^ t1;
        response[1] += response[0] ^ t2;
    }

    bootloader_smc_write_command(0x20, true, response[0]);
    bootloader_smc_write_command(0x21, true, response[1]);
}

static void bootloader_disable_mcpx_rom(void) {
    helper_outl(0xcf8, 0x80000880);
    helper_outb(0xcfc, 0x02);
}

static void bootloader_setup_usb(void) {
    uint8_t mcpx_revision;
    helper_outl(0xcf8, 0x80000808);
    mcpx_revision = helper_inb(0xcfc);    
    /* Auto. slew rate compensation for revisions 0xD1 and above */
    if (mcpx_revision >= 0xD1) {
        helper_outl(0xcf8, 0x800008c8);
        helper_outl(0xcfc, 0x00008f00);
    }
}

static void bootloader_mcpx_reset(void)
{
    helper_outl(0xcf8, 0x8000088c);
    helper_outl(0xcfc, 0x40000000);
}

static void bootloader_preloader(void)
{
  /* This decrypts the actual bootloader and SHA1's it */

  X86CPU *cpu = X86_CPU(first_cpu);
  CPUX86State *env = &cpu->env;
  
  /* Setup segments for preloader */
  helper_load_seg(env, R_DS, 0x10);
  helper_load_seg(env, R_ES, 0x10);
  helper_load_seg(env, R_SS, 0x10);
  env->regs[R_ESP] = preloader_stack;
  helper_load_seg(env, R_FS, 0x00);
  helper_load_seg(env, R_GS, 0x00);

  //FIXME: Segment setup SHA1 etc..

  /* Pass the address of the decrypted bootloader */
  uint32_t decrypt_buffer = bootloader_physical_address - bootloader_size;
  //FIXME: Decrypt from ROM if the key is present!
  env->regs[R_EBP] = decrypt_buffer;
}

void bootloader_emulate(void)
{

    X86CPU *cpu = X86_CPU(first_cpu);
    CPUX86State *env = &cpu->env;

    if (1) { //FIXME: preloader if wanted..
      bootloader_preloader();
    }

    //FIXME: disable cache
    //FIXME: setup mtrr
    //FIXME: enable cache

    /* Set up segments for bootloader */
    helper_load_seg(env, R_DS, 0x10);
    helper_load_seg(env, R_ES, 0x10);
    helper_load_seg(env, R_SS, 0x10);
    env->regs[R_ESP] = bootloader_address; //FIXME: MS oddity?! Check IDA again
    helper_load_seg(env, R_FS, 0x00);
    helper_load_seg(env, R_GS, 0x00);

    // Relocate bootloader from ebp or bootloader_original_address
    uint32_t bootloader_old_address;
    if (1) { /*FIXME: I was told this happens, but on what systems?
                      Must be hardcoded! */
        bootloader_old_address = env->regs[R_EBP];
    } else {
        bootloader_old_address = bootloader_original_address;
    }
    printf("Relocating bootloader from 0x%08x to 0x%08x\n",
           bootloader_old_address,bootloader_address);
    unsigned int i;
    for(i = 0; i < bootloader_size/4; i++) {
        uint32_t value;
        cpu_physical_memory_read(bootloader_old_address+i*4, &value, 4);
        cpu_physical_memory_write(bootloader_address+i*4, &value, 4);
    }

    /* FIXME: More Low level stuff: paging etc */


    { /*FIXME: Only done on X3! - not checked at runtime! */
        /* Disable MCPX rom */
        bootloader_disable_mcpx_rom();
        /* Stop SMC timeout */
        bootloader_stop_smc_watchdog();
        /* Reset SMC version string index */
        bootloader_smc_write_command(0x01, true, 0);
        /* MCPX IDE and NIC reset */
        bootloader_mcpx_reset();
    }

    /* Clear memory where the bootloader (should have) used to be */
    {
        unsigned int i;
        for(i = 0; i < bootloader_size/4; i++) {
            uint32_t pattern = 0xcccccccc;
            cpu_physical_memory_write(bootloader_original_address+i*4, &pattern, 4);
        }
    }

    bool setup = true;
    if (0) { /* FIXME: Only done if *NOT* X3 - not checked at runtime! */
        if (0) { /* FIXME: Only done if "SHADOW" was passed! */
            setup = false;
        }
    }

    if (setup) {
        /*Setup RAM slew [FIXME: where do we get the table from?] */
        //FIXME: bootloader_setup_ram_slew();
        /*Setup LDT bus */
        //FIXME: bootloader_setup_ldt_bus(); 
        /*Setup USB */
        bootloader_setup_usb();
    }

    const char* kernel_filename = jayfoxrox_kernel_filename; /* option "kernel" FIXME Read from cli */
    const char* key_kernel = NULL; //jayfoxrox_kernel_key_filename; /* FIXME Read from cli */
    if (key_kernel) {
        if (kernel_filename) {
          warningPrintf("bootloader emulation RC4 key set, ignoring kernel image\n");
        }
        /*Decrypt kernel from flash */
        /*Extract kernel */
        printf("FIXME: MCPX HLE, Bootloader HLE, Kernel\n");
        assert(0);
    } else {
      if(!kernel_filename) {
        errorPrintf("bootloader emulation requires kernel or RC4 key\n");
        exit(1);
      }

      /* Load kernel from file */
      {
        int kernel_size = get_image_size(kernel_filename);
  
        int fd = open(kernel_filename, O_RDONLY | O_BINARY);
        if (fd == -1) {
            fprintf(stderr, "qemu: could not load xbox kernel '%s'\n", kernel_filename);
            exit(1);
        }
        assert(fd != -1);
        uint8_t* kernel_buf = g_malloc(kernel_size);
        ssize_t rc = read(fd, kernel_buf, kernel_size);
        assert(rc == kernel_size);
        close(fd);

        cpu_physical_memory_write(kernel_address, kernel_buf, kernel_size);
        g_free(kernel_buf);

      }  

      /*
        Data section of kernel should be loaded from flash (0-0x200-0x6000)
        to wherever the datapointer points.. then again to kernel
      */
      //FIXME!

    }

    /* Start emulation of the kernel */
    bootloader_prepare_kernel_registers();
}
