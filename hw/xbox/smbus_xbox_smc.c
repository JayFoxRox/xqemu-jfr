/*
 * QEMU SMBus Xbox System Management Controller
 *
 * Copyright (c) 2014 Jannik Vogel
 * Copyright (c) 2011 espes
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
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus.h"
#include "qemu/timer.h"

#define DEBUG


typedef struct SMBusSMCDevice {
    SMBusDevice smbusdev;
    QEMUTimer *led_timer;
    QEMUTimer *watchdog_timer;
    bool interrupts_enabled;
    uint8_t interrupt_reason;
    uint8_t led_mode;
    uint8_t led_sequence;
    unsigned int led_color; //FIXME: Do we need this? Only helps with printing the LED color
    unsigned int led_step;
    uint8_t fan_speed;
    uint8_t fan_mode;
    uint8_t avpack;
    bool reset_on_press_eject;
    uint8_t scratch_register;
    uint8_t fatal_error_number;
    uint8_t watchdog_challenge[4];
    uint8_t watchdog_response[2];
    unsigned int version_string_index;
    const char* version_string;
    uint8_t scratch;
} SMBusSMCDevice;




/* 
 * Hardware is a PIC16LC
 * http://www.xbox-linux.org/wiki/PIC
 */

#define WATCHDOG_CHALLENGE_TIME_MS 250
#define LED_INTERVAL_MS (1000/(4*2))

#define SMC_REG_VER                 0x01
#define SMC_REG_POWER               0x02
#define         SMC_REG_POWER_RESET         0x01
#define         SMC_REG_POWER_CYCLE         0x40
#define         SMC_REG_POWER_SHUTDOWN      0x80
#define SMC_REG_TRAYSTATE           0x03
#define SMC_REG_AVPACK              0x04
#define         SMC_REG_AVPACK_SCART        0x00
#define         SMC_REG_AVPACK_HDTV         0x01
#define         SMC_REG_AVPACK_VGA_SOG      0x02
#define         SMC_REG_AVPACK_SVIDEO       0x04
#define         SMC_REG_AVPACK_COMPOSITE    0x06
//#define         SMC_REG_AVPACK_VGA          0x07
#define         SMC_REG_AVPACK_NONE         0x07
#define SMC_REG_FANMODE             0x05
#define SMC_REG_FANSPEED            0x06
#define SMC_REG_LEDMODE             0x07
#define SMC_REG_LEDSEQ              0x08
#define SMC_REG_CPUTEMP             0x09
#define SMC_REG_BOARDTEMP           0x0a
#define SMC_REG_TRAYEJECT           0x0c
#define SMC_REG_INTACK		0x0d
#define SMC_REG_INTSTATUS           0x11
#define		SMC_REG_INTSTATUS_POWER		0x01
#define		SMC_REG_INTSTATUS_TRAYCLOSED	0x02
#define		SMC_REG_INTSTATUS_TRAYOPENING	0x04
#define		SMC_REG_INTSTATUS_AVPACK_PLUG	0x08
#define		SMC_REG_INTSTATUS_AVPACK_UNPLUG	0x10
#define		SMC_REG_INTSTATUS_EJECT_BUTTON	0x20
#define		SMC_REG_INTSTATUS_TRAYCLOSING	0x40
#define SMC_REG_RESETONEJECT        0x19
#define SMC_REG_INTEN               0x1a
#define SMC_REG_SCRATCH             0x1b


static void smc_interrupt(SMBusSMCDevice* smc_dev, uint8_t reason) {
  if (smc_dev->interrupts_enabled) {
    smc_dev->interrupt_reason = reason; //FIXME: Maybe OR them together? WWXD?
    //FIXME: Do the interrupt
  }
}

static void smc_eject_tray(SMBusSMCDevice* smc_dev) {
  // FIXME: Ask drive to eject!
  assert(0);
}

static void smc_close_tray(SMBusSMCDevice* smc_dev) {
  //FIXME: Ask to close tray
  assert(0);
}

void smc_shutdown_system(SMBusSMCDevice* smc_dev)
{
    qemu_system_shutdown_request();
}

void smc_warm_reboot_system(SMBusSMCDevice* smc_dev)
{
    qemu_system_reset_request(); //FIXME: This should not clear RAM
}

void smc_cold_reboot_system(SMBusSMCDevice* smc_dev)
{
    qemu_system_reset_request(); //FIXME: This should clear RAM
}

void smc_press_eject(SMBusSMCDevice* smc_dev)
{
  if (smc_dev->reset_on_press_eject) {
    smc_warm_reboot_system(smc_dev);
  } else { //FIXME: Will it still throw the interrupt?
    smc_interrupt(smc_dev,SMC_REG_INTSTATUS_EJECT_BUTTON); 
  }
  smc_eject_tray(smc_dev);
}

void smc_press_power(SMBusSMCDevice* smc_dev)
{
  smc_interrupt(smc_dev,SMC_REG_INTSTATUS_POWER);
}

void smc_avpack_unplug(SMBusSMCDevice* smc_dev)
{
  smc_interrupt(smc_dev,SMC_REG_INTSTATUS_AVPACK_UNPLUG);
  smc_dev->avpack = SMC_REG_AVPACK_NONE;
}

void smc_avpack_plug(SMBusSMCDevice* smc_dev, uint8_t avpack)
{
  smc_dev->avpack = avpack;
  smc_interrupt(smc_dev,SMC_REG_INTSTATUS_AVPACK_PLUG);
}

static void smc_start_watchdog(SMBusSMCDevice* smc_dev) {

  // DBX is probably not using the challenge system
  if (!strcmp(smc_dev->version_string,"DBX")) {
    smc_dev->watchdog_challenge[0] = 0x00;
    smc_dev->watchdog_challenge[1] = 0x00;
    smc_dev->watchdog_challenge[2] = 0x00;
    smc_dev->watchdog_challenge[3] = 0x00;
    return;
  }

  //FIXME: Actual random challenge..
  smc_dev->watchdog_challenge[0] = 0x52;
  smc_dev->watchdog_challenge[1] = 0x72;
  smc_dev->watchdog_challenge[2] = 0xea;
  smc_dev->watchdog_challenge[3] = 0x46;

  // Now start the watchdog!
  //FIXME: Check if the watchdog is already active or something?
  qemu_mod_timer_ns(smc_dev->watchdog_timer, qemu_get_clock_ns(vm_clock)+WATCHDOG_CHALLENGE_TIME_MS*1000*1000);

}

void smc_stop_watchdog(SMBusSMCDevice* smc_dev)
{
  //FIXME: Check if challenge was correct
  {
    // Disable watchdog
    qemu_del_timer(smc_dev->watchdog_timer);
    printf("Watchdog stopped!\n");
  }
  //WWXD if a wrong response is uploaded before the timer runs out?
}

void smc_write_command(SMBusSMCDevice* smc_dev, uint8_t command, uint8_t value)
{

    switch(command) {
        case SMC_REG_VER:
            /* version string reset */
            smc_dev->version_string_index = 0;
            break;
        case SMC_REG_POWER:
            if (value == SMC_REG_POWER_RESET) {
              smc_warm_reboot_system(smc_dev);
            } else if (value == SMC_REG_POWER_CYCLE) {
              smc_cold_reboot_system(smc_dev);
            } else if (value == SMC_REG_POWER_SHUTDOWN) {
              smc_shutdown_system(smc_dev);
            } else {
              // Xbox crashes/hangs normally!
              // Not sure if SMC still works, but I believe it does
              assert(0);
            }
            break;
        case SMC_REG_TRAYEJECT:
            if (value == 0x00) {
              smc_eject_tray(smc_dev);
            } else { //FIXME: WWXD != 0x01?
              smc_close_tray(smc_dev);
            }
        case SMC_REG_INTEN:
            smc_dev->interrupts_enabled |= (value == 0x00); //FIXME: WWXD != 0x00?
            break;
        case SMC_REG_RESETONEJECT:
            smc_dev->reset_on_press_eject = (value == 0x00); //FIXME: WWXD != 0x00?
            break;
        case SMC_REG_LEDMODE:
            smc_dev->led_mode = value;
            break;
        case SMC_REG_LEDSEQ:
            smc_dev->led_sequence = value;
            break;
        case SMC_REG_FANMODE:
            smc_dev->fan_mode = value;
            break;
        case SMC_REG_FANSPEED:
            smc_dev->fan_speed = value;
            break;
        case SMC_REG_SCRATCH:
            smc_dev->scratch = value;
            break;
        /* challenge response
         * (http://www.xbox-linux.org/wiki/PIC_Challenge_Handshake_Sequence) */
        case 0x20:
            smc_dev->watchdog_response[0] = value;
            break;
        case 0x21:
            smc_dev->watchdog_response[1] = value;
            smc_stop_watchdog(smc_dev);
            break;


        default:
            break;
    }

}

uint8_t smc_read_command(SMBusSMCDevice* smc_dev, uint8_t command)
{
      switch(command) {
        case SMC_REG_VER:
            return smc_dev->version_string[
                smc_dev->version_string_index++%3];
        case SMC_REG_AVPACK:
            return smc_dev->avpack;
        case SMC_REG_INTSTATUS:
            return smc_dev->interrupt_reason;
        case SMC_REG_SCRATCH:
            return smc_dev->scratch;
        /* challenge request:
         * must be non-0 */
        case 0x1c:
            return smc_dev->watchdog_challenge[0];
        case 0x1d:
            return smc_dev->watchdog_challenge[1];
        case 0x1e:
            return smc_dev->watchdog_challenge[2];
        case 0x1f:
            return smc_dev->watchdog_challenge[3];

        default:
            break;
    }
    
    return 0;
}

static void print_led_color_change(SMBusSMCDevice* smc_dev, uint8_t color) {
  const char* colors[] = {
    "Off",   // 00
    "Green", // 01
    "Red",   // 10
    "Orange" // 11
  };
  if (smc_dev->led_color != color) {
    printf("LED Color changing to '%s'\n",colors[color]);
  }
}

static void smc_led_timer_cb(SMBusSMCDevice* smc_dev) {
  smc_dev->led_step++;
  smc_dev->led_step %= 4;   
  // Calculate the color
  bool led_green;
  bool led_red;
  //FIXME: respect LED Mode..?
  led_green = ((smc_dev->led_sequence >> 4) >> smc_dev->led_step) & 0x1;
  led_red = ((smc_dev->led_sequence >> 0) >> smc_dev->led_step) & 0x1;
  unsigned int led_color = (led_green?2:0) | (led_red?1:0); //FIXME: Use an enum as type later?
#if 1
  print_led_color_change(smc_dev,led_color);
#endif
  smc_dev->led_color = led_color;
  qemu_mod_timer_ns(smc_dev->led_timer, qemu_get_clock_ns(vm_clock)+LED_INTERVAL_MS*1000*1000);
}

static void smc_watchdog_timer_cb(SMBusSMCDevice* smc_dev) {
  printf("SMC: Watchdog expired, rebooting!\n");
  smc_warm_reboot_system(smc_dev); //FIXME: WWXD?
}

// Now the actual SMBus handlers:

static void smc_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("smc_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static void smc_send_byte(SMBusDevice *dev, uint8_t val)
{
#ifdef DEBUG
    printf("smc_send_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
}

static uint8_t smc_receive_byte(SMBusDevice *dev)
{
#ifdef DEBUG
    printf("smc_receive_byte: addr=0x%02x\n",
           dev->i2c.address);
#endif
    return 0;
}

static void smc_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *) dev;
#ifdef DEBUG
    printf("smc_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif

    assert(len == 1);

    smc_write_command(smc,cmd,buf[0]);
}

static uint8_t smc_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *) dev;
    #ifdef DEBUG
        printf("smc_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
               dev->i2c.address, cmd, n);
    #endif
    
    assert(n == 0);

    return smc_read_command(smc,cmd);
}


static void smbus_smc_reset(SMBusDevice *smc_dev)
{
  // Don't reset because the SMC is always-on. Just set up a new watchdog for the recent boot
  smc_start_watchdog(smc_dev); 
}

static int smbus_smc_init(SMBusDevice *dev)
{
  SMBusSMCDevice *smc_dev = (SMBusSMCDevice *)dev;
      
  //FIXME: Not everything initialized yet

  smc_dev->version_string_index = 0;
  smc_dev->version_string = NULL;

/*
  QemuOpts *machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
  if (machine_opts) {
      smc_dev->version_string = qemu_opt_get(machine_opts, "smc_version");
  }
*/
  //FIXME: Make sure P01, P05 or DXB is passed as version
  if (smc_dev->version_string == NULL) {
    smc_dev->version_string = "P01";
  }

  if (strcmp(smc_dev->version_string,"DBX") &&
      strcmp(smc_dev->version_string,"P01") &&
      strcmp(smc_dev->version_string,"P05") &&
      strcmp(smc_dev->version_string,"P2L")) {
    printf("Unknown smc_version!\n");
  }

  /* pretend to have a composite av pack plugged in */
  smc_dev->avpack = SMC_REG_AVPACK_COMPOSITE;
  
  // We want to be really annoying by default
  smc_dev->reset_on_press_eject = true;

  // Disable interrupts on startup
  smc_dev->interrupts_enabled = false;
  
  //FIXME: Remove or make an option
  {
    // Skip boot anim hack
    smc_dev->scratch = 0x04;
  }

  // Create some timers for us
  smc_dev->watchdog_timer = qemu_new_timer_ns(vm_clock, smc_watchdog_timer_cb, smc_dev);
  smc_dev->led_timer = qemu_new_timer_ns(vm_clock, smc_led_timer_cb, smc_dev);
  
  // Start the LED timer
  qemu_mod_timer_ns(smc_dev->led_timer, qemu_get_clock_ns(vm_clock));

  // Request that we are informed about resets //FIXME: Why is this not part of the smbus device crap?
  qemu_register_reset(smbus_smc_reset,smc_dev);

  return 0;
}

static void smbus_smc_class_initfn(ObjectClass *klass, void *data)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    sc->init = smbus_smc_init;
    sc->quick_cmd = smc_quick_cmd;
    sc->send_byte = smc_send_byte;
    sc->receive_byte = smc_receive_byte;
    sc->write_data = smc_write_data;
    sc->read_data = smc_read_data;
}

static TypeInfo smbus_smc_info = {
    .name = "smbus-xbox-smc",
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusSMCDevice),
    .class_init = smbus_smc_class_initfn,
};



static void smbus_smc_register_devices(void)
{
    type_register_static(&smbus_smc_info);
}

type_init(smbus_smc_register_devices)


void smbus_xbox_smc_init(i2c_bus *smbus, int address)
{
    DeviceState *smc;
    smc = qdev_create((BusState *)smbus, "smbus-xbox-smc");
    qdev_prop_set_uint8(smc, "address", address);
    qdev_init_nofail(smc);
}
