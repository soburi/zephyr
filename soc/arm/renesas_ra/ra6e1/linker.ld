# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

DATA_FLASH_START = 0x08000000;
DATA_FLASH_LENGTH = 0x2000;
OPTION_SETTING_START = 0x0100A100;
OPTION_SETTING_LENGTH = 0x100;
OPTION_SETTING_S_START = 0x0100A200;
OPTION_SETTING_S_LENGTH = 0x100;
ID_CODE_START = 0x00000000;
ID_CODE_LENGTH = 0x0;
SDRAM_START = 0x80010000;
SDRAM_LENGTH = 0x0;
QSPI_FLASH_START = 0x60000000;
QSPI_FLASH_LENGTH = 0x4000000;
OSPI_DEVICE_0_START = 0x80020000;
OSPI_DEVICE_0_LENGTH = 0x0;
OSPI_DEVICE_1_START = 0x80030000;
OSPI_DEVICE_1_LENGTH = 0x0;

OPTION_SETTING_START_NS = DEFINED(PROJECT_NONSECURE) ? OPTION_SETTING_START : OPTION_SETTING_START + 0x80;

QSPI_FLASH_PRV_LENGTH = DEFINED(QSPI_FLASH_SIZE) ? ABSOLUTE(QSPI_FLASH_SIZE) : ABSOLUTE(QSPI_FLASH_LENGTH);
OSPI_DEVICE_0_PRV_LENGTH = DEFINED(OSPI_DEVICE_0_SIZE) ? ABSOLUTE(OSPI_DEVICE_0_SIZE) : ABSOLUTE(OSPI_DEVICE_0_LENGTH);
OSPI_DEVICE_1_PRV_LENGTH = DEFINED(OSPI_DEVICE_1_SIZE) ? ABSOLUTE(OSPI_DEVICE_1_SIZE) : ABSOLUTE(OSPI_DEVICE_1_LENGTH);

/* If a flat (secure) project has DEFINED RAM_NS_BUFFER_LENGTH, then emit IDAU symbols to allocate non-secure RAM. */
__RESERVE_NS_RAM = !DEFINED(PROJECT_NONSECURE) && DEFINED(RAM_NS_BUFFER_LENGTH) && (OPTION_SETTING_S_LENGTH != 0);

NS_IMAGE_OFFSET = DEFINED(PROJECT_NONSECURE) ? NS_OFFSET_START : 0;

OPTION_SETTING_START_NS = DEFINED(PROJECT_NONSECURE) ? OPTION_SETTING_START : OPTION_SETTING_START + 0x80;

PROJECT_SECURE_OR_FLAT = (!DEFINED(PROJECT_NONSECURE) || DEFINED(PROJECT_SECURE)) && OPTION_SETTING_LENGTH && !DEFINED(FSP_BOOTABLE_IMAGE);
USE_OPTION_SETTING_NS = DEFINED(PROJECT_NONSECURE) && !DEFINED(FSP_BOOTABLE_IMAGE);

MEMORY
{
	DATA_FLASH (rx)         : ORIGIN = DATA_FLASH_START + NS_IMAGE_OFFSET, LENGTH = DATA_FLASH_LENGTH
	QSPI_FLASH (rx)         : ORIGIN = QSPI_FLASH_START, LENGTH = QSPI_FLASH_PRV_LENGTH
	OSPI_DEVICE_0 (rx)      : ORIGIN = OSPI_DEVICE_0_START, LENGTH = OSPI_DEVICE_0_PRV_LENGTH
	OSPI_DEVICE_1 (rx)      : ORIGIN = OSPI_DEVICE_1_START, LENGTH = OSPI_DEVICE_1_PRV_LENGTH
	OSPI_DEVICE_0_RAM (rwx) : ORIGIN = OSPI_DEVICE_0_START, LENGTH = OSPI_DEVICE_0_PRV_LENGTH
	OSPI_DEVICE_1_RAM (rwx) : ORIGIN = OSPI_DEVICE_1_START, LENGTH = OSPI_DEVICE_1_PRV_LENGTH
	SDRAM (rwx)             : ORIGIN = SDRAM_START, LENGTH = SDRAM_LENGTH
	OPTION_SETTING (r)      : ORIGIN = OPTION_SETTING_START + NS_IMAGE_OFFSET, LENGTH = OPTION_SETTING_LENGTH
	OPTION_SETTING_OFS (r)  : ORIGIN = OPTION_SETTING_START + NS_IMAGE_OFFSET, LENGTH = 0x18
	OPTION_SETTING_SAS (r)  : ORIGIN = OPTION_SETTING_START + NS_IMAGE_OFFSET + 0x34, LENGTH = OPTION_SETTING_LENGTH - 0x34
	OPTION_SETTING_S (r)    : ORIGIN = OPTION_SETTING_S_START + NS_IMAGE_OFFSET, LENGTH = OPTION_SETTING_S_LENGTH
	ID_CODE (rx)            : ORIGIN = ID_CODE_START, LENGTH = ID_CODE_LENGTH
}

#include <zephyr/arch/arm/aarch32/cortex_m/scripts/linker.ld>

SECTIONS
{
	/* Data flash. */
	.data_flash :
	{
		. = ORIGIN(DATA_FLASH);
		__tz_DATA_FLASH_S = .;
		__Data_Flash_Start = .;
		KEEP(*(.data_flash*))
		__Data_Flash_End = .;

		__tz_DATA_FLASH_N = DEFINED(DATA_FLASH_NS_START) ? ABSOLUTE(DATA_FLASH_NS_START) : __RESERVE_NS_RAM ? ABSOLUTE(DATA_FLASH_START + DATA_FLASH_LENGTH) : ALIGN(1024);
	} > DATA_FLASH

	/* Note: There are no secure/non-secure boundaries for SDRAM.  These symbols are provided for the RA configuration tool. */
	__tz_SDRAM_S = ORIGIN(SDRAM);

	/* SDRAM */
	.sdram (NOLOAD):
	{
		__SDRAM_Start = .;
		KEEP(*(.sdram*))
		KEEP(*(.frame*))
		__SDRAM_End = .;
	} > SDRAM

	/* Note: There are no secure/non-secure boundaries for SDRAM.  These symbols are provided for the RA configuration tool. */
	__tz_SDRAM_N = __SDRAM_End;

	/* Note: There are no secure/non-secure boundaries for ID_CODE.  These symbols are provided for the RA configuration tool. */
	__tz_ID_CODE_S = ORIGIN(ID_CODE);

	/* Note: There are no secure/non-secure boundaries for ID_CODE.  These symbols are provided for the RA configuration tool.
	 *       Set this symbol to the same value as __tz_ID_CODE_S so the RA configuration tool does not split the ID_CODE
	 *       memory region between TrustZone projects. */
	__tz_ID_CODE_N = __tz_ID_CODE_S;

	.id_code :
	{
		__ID_Code_Start = .;
		KEEP(*(.id_code*))
		__ID_Code_End = .;
	} > ID_CODE


	/* Symbol required for RA Configuration tool. */
	__tz_OPTION_SETTING_S = ORIGIN(OPTION_SETTING_OFS);

	.option_setting_ofs :
	{
		__OPTION_SETTING_OFS_Start = .;
		KEEP(*(.option_setting_ofs0))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_OFS_Start + 0x04 : __OPTION_SETTING_OFS_Start;
		KEEP(*(.option_setting_ofs2))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_OFS_Start + 0x10 : __OPTION_SETTING_OFS_Start;
		KEEP(*(.option_setting_dualsel))
		__OPTION_SETTING_OFS_End = .;
	} > OPTION_SETTING_OFS = 0xFF

	.option_setting_sas :
	{
		__OPTION_SETTING_SAS_Start = .;
		KEEP(*(.option_setting_sas))
		__OPTION_SETTING_SAS_End = .;
	} > OPTION_SETTING_SAS = 0xFF

	/* Symbol required for RA Configuration tool. */
	__tz_OPTION_SETTING_N = ABSOLUTE(OPTION_SETTING_START_NS);

	.option_setting_ns :
	{
		__OPTION_SETTING_NS_Start = .;
		KEEP(*(.option_setting_ofs1))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x04 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_ofs3))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x10 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_banksel))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x40 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_bps0))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x44 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_bps1))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x48 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_bps2))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x4C : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_bps3))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x60 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_pbps0))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x64 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_pbps1))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x68 : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_pbps2))
		. = USE_OPTION_SETTING_NS ? __OPTION_SETTING_NS_Start + 0x6C : __OPTION_SETTING_NS_Start;
		KEEP(*(.option_setting_pbps3))
		__OPTION_SETTING_NS_End = .;
	} > OPTION_SETTING = 0xFF

	/* Symbol required for RA Configuration tool. */
	__tz_OPTION_SETTING_S_S = ORIGIN(OPTION_SETTING_S);

	.option_setting_s :
	{
		__OPTION_SETTING_S_Start = .;
		KEEP(*(.option_setting_ofs1_sec))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x04 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_ofs3_sec))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x10 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_banksel_sec))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x40 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sec0))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x44 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sec1))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x48 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sec2))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x4C : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sec3))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x60 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_pbps_sec0))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x64 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_pbps_sec1))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x68 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_pbps_sec2))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x6C : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_pbps_sec3))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x80 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_ofs1_sel))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x84 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_ofs3_sel))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0x90 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_banksel_sel))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0xC0 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sel0))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0xC4 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sel1))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0xC8 : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sel2))
		. = PROJECT_SECURE_OR_FLAT ? __OPTION_SETTING_S_Start + 0xCC : __OPTION_SETTING_S_Start;
		KEEP(*(.option_setting_bps_sel3))
		__OPTION_SETTING_S_End = .;
	} > OPTION_SETTING_S = 0xFF

	/* Symbol required for RA Configuration tool. */
	__tz_OPTION_SETTING_S_N = __OPTION_SETTING_S_End;
}
