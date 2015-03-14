/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014  Mike Walters <mike@flomp.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements nRF51 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 */

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"

static int nrf51_flash_erase(struct target_s *target, uint32_t addr, size_t len);
static int nrf51_flash_write(struct target_s *target, uint32_t dest,
                             const uint8_t *src, size_t len);

static bool nrf51_cmd_erase_all(target *t);
static bool nrf51_cmd_read_hwid(target *t);
static bool nrf51_cmd_read_fwid(target *t);
static bool nrf51_cmd_read_deviceid(target *t);
static bool nrf51_cmd_read_deviceaddr(target *t);
static bool nrf51_cmd_read_help();
static bool nrf51_cmd_read(target *t, int argc, const char *argv[]);

const struct command_s nrf51_cmd_list[] = {
	{"erase_mass", (cmd_handler)nrf51_cmd_erase_all, "Erase entire flash memory"},
	{"read", (cmd_handler)nrf51_cmd_read, "Read device parameters"},
	{NULL, NULL, NULL}
};
const struct command_s nrf51_read_cmd_list[] = {
	{"help", (cmd_handler)nrf51_cmd_read_help, "Display help for read commands"},
	{"hwid", (cmd_handler)nrf51_cmd_read_hwid, "Read hardware identification number"},
	{"fwid", (cmd_handler)nrf51_cmd_read_fwid, "Read pre-loaded firmware ID"},
	{"deviceid", (cmd_handler)nrf51_cmd_read_deviceid, "Read unique device ID"},
	{"deviceaddr", (cmd_handler)nrf51_cmd_read_deviceaddr, "Read device address"},
	{NULL, NULL, NULL}
};

static const char nrf51_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x0\" length=\"0x40000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x10001000\" length=\"0x100\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x4000\"/>"
	"</memory-map>";

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NRF51_NVMC					0x4001E000
#define NRF51_NVMC_READY			(NRF51_NVMC + 0x400)
#define NRF51_NVMC_CONFIG			(NRF51_NVMC + 0x504)
#define NRF51_NVMC_ERASEPAGE		(NRF51_NVMC + 0x508)
#define NRF51_NVMC_ERASEALL			(NRF51_NVMC + 0x50C)
#define NRF51_NVMC_ERASEUICR		(NRF51_NVMC + 0x514)

#define NRF51_NVMC_CONFIG_REN		0x0						// Read only access
#define NRF51_NVMC_CONFIG_WEN		0x1						// Write enable
#define NRF51_NVMC_CONFIG_EEN		0x2						// Erase enable

/* Factory Information Configuration Registers (FICR) */
#define NRF51_FICR				0x10000000
#define NRF51_FICR_CODEPAGESIZE			(NRF51_FICR + 0x010)
#define NRF51_FICR_CODESIZE			(NRF51_FICR + 0x014)
#define NRF51_FICR_CONFIGID			(NRF51_FICR + 0x05C)
#define NRF51_FICR_DEVICEID_LOW			(NRF51_FICR + 0x060)
#define NRF51_FICR_DEVICEID_HIGH		(NRF51_FICR + 0x064)
#define NRF51_FICR_DEVICEADDRTYPE		(NRF51_FICR + 0x0A0)
#define NRF51_FICR_DEVICEADDR_LOW		(NRF51_FICR + 0x0A4)
#define NRF51_FICR_DEVICEADDR_HIGH		(NRF51_FICR + 0x0A8)

/* User Information Configuration Registers (UICR) */
#define NRF51_UICR				0x10001000

#define NRF51_PAGE_SIZE 1024

static const uint16_t nrf51_flash_write_stub[] = {
// _start:
	0x4808, // ldr	r0, [pc, #32]	; (24 <_ready>)
	0x4909, // ldr	r1, [pc, #36]	; (28 <_addr>)
	0x467a, // mov	r2, pc
	0x3228, // adds	r2, #40	; 0x28
	0x4b08, // ldr	r3, [pc, #32]	; (2c <_size>)

// next:
	0x2b00, // cmp	r3, #0
	0xd009, // beq.n	22 <_done>
	0x6814, // ldr	r4, [r2, #0]
	0x600c, // str	r4, [r1, #0]

// wait:
	0x6804, // ldr	r4, [r0, #0]
	0x2601, // movs	r6, #1
	0x4234, // tst	r4, r6
	0xd0fb, // beq.n	12 <_wait>

	0x3b04, // subs	r3, #4
	0x3104, // adds	r1, #4
	0x3204, // adds	r2, #4
	0xe7f3, // b.n	a <_next>

// done:
	0xbe00, // bkpt	0x0000

// ready:
	0xe400, 0x4001 // .word	0x4001e400
// addr:
//	0x0000, 0x0000
// size:
//	0x0000, 0x0000
// data:
//	...

};

bool nrf51_probe(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	target->idcode = adiv5_ap_mem_read(ap, NRF51_FICR_CONFIGID) & 0xFFFF;

	switch (target->idcode) {
	case 0x001D:
	case 0x002A:
	case 0x0044:
	case 0x003C:
	case 0x0020:
	case 0x002F:
	case 0x0040:
	case 0x0047:
	case 0x004D:
	case 0x0026:
	case 0x004C:
		target->driver = "Nordic nRF51";
		target->xml_mem_map = nrf51_xml_memory_map;
		target->flash_erase = nrf51_flash_erase;
		target->flash_write = nrf51_flash_write;
		target_add_commands(target, nrf51_cmd_list, "nRF51");
		return true;
	}

	return false;
}

static int nrf51_flash_erase(struct target_s *target, uint32_t addr, size_t len)
{

	ADIv5_AP_t *ap = adiv5_target_ap(target);

	addr &= ~(NRF51_PAGE_SIZE - 1);
	len &= ~(NRF51_PAGE_SIZE - 1);

	/* Enable erase */
	adiv5_ap_mem_write(ap, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_EEN);

	/* Poll for NVMC_READY */
	while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
		if(target_check_error(target))
			return -1;

	while (len) {
		if (addr == NRF51_UICR) { // Special Case
			/* Write to the ERASE_UICR register to erase */
			adiv5_ap_mem_write(ap, NRF51_NVMC_ERASEUICR, 0x1);

		} else { // Standard Flash Page
			/* Write address of first word in page to erase it */
			adiv5_ap_mem_write(ap, NRF51_NVMC_ERASEPAGE, addr);
		}

		/* Poll for NVMC_READY */
		while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
			if(target_check_error(target))
				return -1;

		addr += NRF51_PAGE_SIZE;
		len -= NRF51_PAGE_SIZE;
	}

	/* Return to read-only */
	adiv5_ap_mem_write(ap, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_REN);

	/* Poll for NVMC_READY */
	while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
		if(target_check_error(target))
			return -1;

	return 0;
}

static int nrf51_flash_write(struct target_s *target, uint32_t dest,
                             const uint8_t *src, size_t len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t offset = dest % 4;
	uint32_t words = (offset + len + 3) / 4;
	uint32_t data[2 + words];

	/* Construct data buffer used by stub */
	data[0] = dest - offset;
	data[1] = words * 4;		/* length must always be a multiple of 4 */
	data[2] = 0xFFFFFFFF;		/* pad partial words with all 1s to avoid */
	data[words + 1] = 0xFFFFFFFF;	/* damaging overlapping areas */
	memcpy((uint8_t *)&data[2] + offset, src, len);

	/* Enable write */
	adiv5_ap_mem_write(ap, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_WEN);

	/* Poll for NVMC_READY */
	while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
		if(target_check_error(target))
			return -1;

	/* Write stub and data to target ram and set PC */
	target_mem_write_words(target, 0x20000000, (void*)nrf51_flash_write_stub, 0x28);
	target_mem_write_words(target, 0x20000028, data, len + 8);
	target_pc_write(target, 0x20000000);
	if(target_check_error(target))
		return -1;

	/* Execute the stub */
	target_halt_resume(target, 0);
	while(!target_halt_wait(target));

	/* Return to read-only */
	adiv5_ap_mem_write(ap, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_REN);

	return 0;
}

static bool nrf51_cmd_erase_all(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	gdb_out("erase..\n");

	/* Enable erase */
	adiv5_ap_mem_write(ap, NRF51_NVMC_CONFIG, NRF51_NVMC_CONFIG_EEN);

	/* Poll for NVMC_READY */
	while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
		if(target_check_error(t))
			return false;

	/* Erase all */
	adiv5_ap_mem_write(ap, NRF51_NVMC_ERASEALL, 1);

	/* Poll for NVMC_READY */
	while(adiv5_ap_mem_read(ap, NRF51_NVMC_READY) == 0)
		if(target_check_error(t))
			return false;

	return true;
}

static bool nrf51_cmd_read_hwid(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	uint32_t hwid = adiv5_ap_mem_read(ap, NRF51_FICR_CONFIGID) & 0xFFFF;
	gdb_outf("Hardware ID: 0x%04X\n", hwid);

	return true;
}
static bool nrf51_cmd_read_fwid(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	uint32_t fwid = (adiv5_ap_mem_read(ap, NRF51_FICR_CONFIGID) >> 16) & 0xFFFF;
	gdb_outf("Firmware ID: 0x%04X\n", fwid);

	return true;
}
static bool nrf51_cmd_read_deviceid(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	uint32_t deviceid_low = adiv5_ap_mem_read(ap, NRF51_FICR_DEVICEID_LOW);
	uint32_t deviceid_high = adiv5_ap_mem_read(ap, NRF51_FICR_DEVICEID_HIGH);

	gdb_outf("Device ID: 0x%08X%08X\n", deviceid_high, deviceid_low);

	return true;
}
static bool nrf51_cmd_read_deviceaddr(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	uint32_t addr_type = adiv5_ap_mem_read(ap, NRF51_FICR_DEVICEADDRTYPE);
	uint32_t addr_low = adiv5_ap_mem_read(ap, NRF51_FICR_DEVICEADDR_LOW);
	uint32_t addr_high = adiv5_ap_mem_read(ap, NRF51_FICR_DEVICEADDR_HIGH) & 0xFFFF;

	if ((addr_type & 1) == 0) {
		gdb_outf("Publicly Listed Address: 0x%04X%08X\n", addr_high, addr_low);
	} else {
		gdb_outf("Randomly Assigned Address: 0x%04X%08X\n", addr_high, addr_low);
	}

	return true;
}
static bool nrf51_cmd_read_help()
{
	const struct command_s *c;

	gdb_out("Read commands:\n");
	for(c = nrf51_read_cmd_list; c->cmd; c++)
		gdb_outf("\t%s -- %s\n", c->cmd, c->help);

	return true;
}
static bool nrf51_cmd_read(target *t, int argc, const char *argv[])
{
	const struct command_s *c;

	for(c = nrf51_read_cmd_list; c->cmd; c++) {
		/* Accept a partial match as GDB does.
		 * So 'mon ver' will match 'monitor version'
		 */
		if(!strncmp(argv[1], c->cmd, strlen(argv[1])))
			return !c->handler(t, argc - 1, &argv[1]);
	}

	return nrf51_cmd_read_help(t);
}

