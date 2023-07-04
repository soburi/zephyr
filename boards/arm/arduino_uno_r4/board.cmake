# SPDX-License-Identifier: Apache-2.0

board_runner_args(e2-server-gdb
	"--target=R7FA4M1AB"
	"--adapter=E2LITE"
	"-uConnectionTimeout=30"
	"-uClockSrc=0"
	"-uAllowClockSourceInternal=1"
	"-uInteface=SWD"
	"-uIfSpeed=auto"
	"-uIdCodeBytes=FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
	"-uResetCon=1"
	"-uLowPower=1"
	"-uresetOnReload=1"
	"-uFlashBp=0"
	"-uhookWorkRamSize=0x400"
	"-ueraseRomOnDownload=0"
	"-ueraseDataRomOnDownload=0"
	"-uTraceMTB=0"
	"-uTraceSizeMTB=1024"
	"-uProgReWriteIRom=0"
	"-uProgReWriteDFlash=0"
	"-uOSRestriction=0"
	"-uTimeMeasurementEnable=1"
	"-uDWTEnable=0"
	"-uMemRegion=0x00000000:0x40000:FLASH:e"
	"-uMemRegion=0x40100000:0x2000:DATA_FLASH:e"
	"-uCore=SINGLE_CORE\|enabled\|1\|main"
	"-uSyncMode=async"
	"-uFirstGDB=main"
	"-w 0"
	"-z 33"
	"-n 0"
	"-l"
	)

board_runner_args(dfu-util "--pid=\"2341:0069,:0369\"" "--alt=0" "--dfuse")

include(${ZEPHYR_BASE}/boards/common/dfu-util.board.cmake)

include(${ZEPHYR_BASE}/boards/common/e2-server-gdb.board.cmake)
