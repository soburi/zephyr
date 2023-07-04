# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

"""Runner for e2-server-gdb ."""

import os
import sys
import shutil
import tempfile

from runners.core import ZephyrBinaryRunner, RunnerCaps

DEFAULT_E2SERVERGDB_GDB_PORT = 61234

# Note:
# We can't know the detailed spec of the command argument for e2-server-gdb.
# (It seems not disclosed.)
# It enumerates strings starting with 'u' found in the executable file.
# These are possible to be options.
UOPTIONS = [
    "AccessCore",
    "ADASReset",
    "AllowClockSourceInternal",
    "AllowRRMDMM",
    "allowWritingProgramerSecurityID",
    "Attach",
    "authenticationDll",
    "AuthKey",
    "AuthLevel",
    "BlankFlashFill",
    "BootCore",
    "BrkSyncSrc",
    "BusMonBaudRate",
    "BusMonCommPort",
    "CECycle",
    "cfiFlashEnd",
    "cfiFlashStart",
    "cfiFlashWorkEnd",
    "cfiFlashWorkStart",
    "ChangeStartupBank",
    "CLLength",
    "CLMaxLength",
    "ClockSrc",
    "ClockSrcHoco",
    "ClusterAttribute",
    "codeFlashID",
    "ComBaudRate",
    "commMethod",
    "ComPort",
    "ConnectionTimeout",
    "ConnectMode",
    "Core",
    "CoreAttribute",
    "CortexInitialState",
    "CoverageEnable",
    "CPUFrequency",
    "CpuVoltage",
    "CTI",
    "customerID",
    "DataBusWidth",
    "dataFlashID",
    "DebugCodeSRAM",
    "DebugConsoleCore",
    "debugContext",
    "DebugMode",
    "dfspEnable",
    "dfspEraseAddress",
    "dfspEraseTime",
    "dfspMSR0",
    "dfspMSR0Address",
    "dfspMSR0Mask",
    "dfspMSR1",
    "dfspMSR1Address",
    "dfspMSR1Mask",
    "dfspMSR2",
    "dfspMSR2Address",
    "dfspMSR2Mask",
    "dfspWriteAddress",
    "dfspWriteTime",
    "DisconnectionMode",
    "displaySimGUI",
    "DTAEventMatching",
    "dtaLogFilePath",
    "DWTEnable",
    "e2asp",
    "efcpDataStart",
    "efcpDwldDataAddr",
    "efcpDwldDataAreaSize",
    "efcpDwldDataAreaStart",
    "efcpDwldModStart",
    "efcpEraseModStart",
    "efcpInitModStart",
    "efcpReadDataAreaSize",
    "efcpReadDataAreaStart",
    "efcpReadModStart",
    "emmcPartition",
    "EmulationAdapter",
    "EnableSciBoot",
    "EnReset",
    "eraseDataRomOnDownload",
    "eraseRom",
    "eraseRomOnDownload",
    "ExcludeFlashCacheRange",
    "ExecuteProgram",
    "ExternalTraceEnable",
    "ExternalTraceLane",
    "ExternalTracePort",
    "ExternalTraceRate",
    "ExtFlashErase",
    "ExtFlashFile",
    "ExtFlashMem",
    "extflDwldAddrInfo",
    "extflEnable",
    "extflTimeout",
    "extflType",
    "FillUnusedFlashOnRewriting",
    "FineBaudRate",
    "FirstGDB",
    "FlashBp",
    "flashBusType",
    "flashMemoryType",
    "ForceReset",
    "fsbFetchFromProtected",
    "fsbIllegalFlash",
    "fsbMisalignWord",
    "fsbNonMapMem",
    "fsbOverflowStack",
    "fsbPeripFSB",
    "fsbReadFromSFR",
    "fsbStackBottom",
    "fsbStackTop",
    "fsbUnderflowStack",
    "fsbUninitRAM",
    "fsbUninitStack",
    "fsbWriteToProtected",
    "fsbWriteToSFR",
    "fspDisableBootWrite",
    "fspDisableErase",
    "fspDisableProgram",
    "fspEnable",
    "fspEraseAddress",
    "fspEraseTime",
    "fspMSR0",
    "fspMSR0Address",
    "fspMSR0Mask",
    "fspMSR1",
    "fspMSR1Address",
    "fspMSR1Mask",
    "fspMSR2",
    "fspMSR2Address",
    "fspMSR2Mask",
    "fspShieldEnd",
    "fspShieldStart",
    "fspWriteAddress",
    "fspWriteTime",
    "hookEnableStart",
    "hookEnableStop",
    "hookStartFunc",
    "hookStopFunc",
    "hookWorkRamAddr",
    "hookWorkRamSize",
    "HotPlug",
    "IdCode",
    "IdCodeBytes",
    "IfSpeed",
    "InitialStop",
    "InitRegisters",
    "InputClock",
    "Inteface",
    "Interface",
    "JLinkLog",
    "JLinkScript",
    "JLinkSetting",
    "JTagClockFreq",
    "LowPower",
    "lowVoltageOcdBoard",
    "lpdBaud",
    "lpdFreq",
    "lpdType",
    "mainClockMode",
    "maskHldrq",
    "maskInternalResetSignal",
    "maskNMISignal",
    "maskReset",
    "maskStop",
    "maskTargetResetSignal",
    "maskWait",
    "mClock",
    "mFreq",
    "mMultip",
    "ModePin",
    "MultiDeviceSync",
    "needAuthentication",
    "NonSecureVectorAddress",
    "NoReset",
    "ocdID",
    "OSRestriction",
    "PerformanceCore",
    "permitFlash",
    "PracticeScript",
    "productSpecificID",
    "programmerSecurityID",
    "ProgReWriteDFlash",
    "ProgReWriteIRom",
    "PTimerClock",
    "RAPISync",
    "RegisterSetting",
    "RelayBreak",
    "ReleaseCM3",
    "RemoteHost",
    "RemoteMemoryHelperPort",
    "RemotePort",
    "RemotePortBMData",
    "RemotePortBMMsg",
    "RemotePortDTAData",
    "RemotePortDTAMsg",
    "RemoteShaderStubPort",
    "ResBreak",
    "ResetBefDownload",
    "ResetCon",
    "ResetControl",
    "resetOnReload",
    "ResetPreRun",
    "scDrPre",
    "scIrPre",
    "SecureVectorAddress",
    "securityID",
    "Select",
    "SelectIP",
    "SelfCodeSet",
    "SemihostingSVCBreakpointAddress",
    "SetArmModeAfterDownload",
    "setDebugContext",
    "sFreq",
    "ShaderHelperCore",
    "ShowGUI",
    "Simulation",
    "SimulatorSetting",
    "skipNonDebugContext",
    "SkipSelectDeviceCheck",
    "SMP",
    "StartStopCore",
    "StartupBank",
    "stopPeriEmu",
    "stopSerialEmu",
    "stopTimerEmu",
    "subClockMode",
    "supplyVoltage",
    "supplyVoltageASP",
    "svrParameter",
    "SWOclockDiv",
    "SWOcoreClock",
    "swOpbt",
    "swOpbtIcum",
    "swOpbtRv",
    "SyncMode",
    "SyncTraceStandard",
    "T32Path",
    "targetBoardConnected",
    "TimeMeasurementEnable",
    "TraceCache",
    "TraceClock",
    "TraceCore",
    "TraceEnable",
    "TraceEnabledCores",
    "TraceMode",
    "TraceMTB",
    "TraceSizeMTB",
    "Trst",
    "Tz",
    "useAuthenticationDll",
    "UseFine",
    "UseSyncTrace",
    "useWideVoltageMode",
    "VDK",
    "verifyOnWritingMemory",
    "WorkRamAddress",
]

UOPTIONS_APPEND = [
    "MemRegion",
]


class E2ServerGdbBinaryRunner(ZephyrBinaryRunner):
    """Runner front-end for e2-server-gdb."""

    def __init__(
        self,
        cfg,
        target,
        adapter,
        n_opt,
        w_opt,
        z_opt,
        l_opt,
        e2_server_gdb="e2-server-gdb",
        erase=False,
        gdb_port=DEFAULT_E2SERVERGDB_GDB_PORT,
        tui=False,
        uargs=None,
    ):
        super().__init__(cfg)

        self.target = target
        self.adapter = adapter
        self.n_opt = n_opt
        self.w_opt = w_opt
        self.z_opt = z_opt
        self.l_opt = l_opt
        self.e2_server_gdb = e2_server_gdb

        self.erase = erase
        self.gdb_cmd = [cfg.gdb] if cfg.gdb is not None else None
        self.gdb_port = gdb_port
        self.tui_args = ["-tui"] if tui else []
        self.hex_name = cfg.hex_file
        self.bin_name = cfg.bin_file
        self.elf_name = cfg.elf_file
        self.JLinkSetting = ""

        if sys.platform == "win32" and not self.e2_server_gdb.endswith(".exe"):
            self.e2_server_gdb = self.e2_server_gdb + ".exe"

        if not os.path.isfile(self.e2_server_gdb):
            self.e2_server_gdb = shutil.which(self.e2_server_gdb)
            if shutil.which(self.e2_server_gdb) is not None:
                raise ValueError("Cannot debug; e2-server-gdb is missing")

        for uopt in UOPTIONS:
            uvar = eval("uargs." + uopt)
            if uvar:
                setattr(self, uopt, uvar)

        for uopt in UOPTIONS_APPEND:
            uvar = eval("uargs." + uopt)
            if uvar:
                setattr(self, uopt, uvar)

    @classmethod
    def name(cls):
        return "e2-server-gdb"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"debug", "debugserver"}, erase=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument("--target", required=True, help="target override")
        parser.add_argument(
            "--adapter", required=True, help="select adapter [E2LITE|SEGGERJLINKARM]"
        )
        parser.add_argument(
            "--e2-server-gdb",
            default="e2-server-gdb",
            help="path to e2-server-gdb tool, default is e2-server-gdb",
        )
        parser.add_argument(
            "--gdb-port",
            default=DEFAULT_E2SERVERGDB_GDB_PORT,
            help="e2-server-gdb gdb port, defaults to {}".format(
                DEFAULT_E2SERVERGDB_GDB_PORT
            ),
        )
        parser.add_argument(
            "--tui", default=False, action="store_true", help="if given, GDB uses -tui"
        )
        parser.add_argument(
            "-n", dest="n_opt", type=str, default="--", help="e2-server-gdb -n option"
        )
        parser.add_argument(
            "-w", dest="w_opt", type=str, default="--", help="e2-server-gdb -w option"
        )
        parser.add_argument(
            "-z", dest="z_opt", type=str, default="--", help="e2-server-gdb -z option"
        )
        parser.add_argument(
            "-l",
            dest="l_opt",
            default=False,
            action="store_true",
            help="e2-server-gdb -l option",
        )

        for uopt in UOPTIONS:
            parser.add_argument("-u" + uopt, dest=uopt, type=str, help=uopt)

        for uopt in UOPTIONS_APPEND:
            parser.add_argument(
                "-u" + uopt, dest=uopt, type=str, action="append", help=uopt
            )

    @classmethod
    def do_create(cls, cfg, args):
        ret = E2ServerGdbBinaryRunner(
            cfg,
            args.target,
            args.adapter,
            args.n_opt,
            args.w_opt,
            args.z_opt,
            args.l_opt,
            e2_server_gdb=args.e2_server_gdb,
            erase=args.erase,
            gdb_port=args.gdb_port,
            tui=args.tui,
            uargs=args,
        )

        return ret

    def do_run(self, command, **kwargs):
        self.require(self.e2_server_gdb)
        if command == "flash":
            self.flash(**kwargs)
        else:
            self.debug_or_debugserver(command, **kwargs)

    def log_gdbserver_message(self):
        self.logger.info(
            "e2-server-gdb GDB server running on port {}".format(self.gdb_port)
        )

    def debug_or_debugserver(self, command, **kwargs):
        with tempfile.TemporaryDirectory(suffix="e2-server-gdb") as d:
            fname = os.path.join(d, "e2-server-gdb.jlink")

            server_cmd = (
                [self.e2_server_gdb]
                + ["-p", str(self.gdb_port)]
                + ["-g", str(self.adapter)]
                + ["-t", str(self.target)]
            )

            if self.JLinkSetting:
                shutil.copyfile(self.JLinkSetting, fname)
                self.JLinkSetting = fname

            if self.n_opt != "--":
                server_cmd = server_cmd + ["-n", self.n_opt.strip()]
            if self.w_opt != "--":
                server_cmd = server_cmd + ["-w", self.w_opt.strip()]
            if self.z_opt != "--":
                server_cmd = server_cmd + ["-z", self.z_opt.strip()]
            if self.l_opt:
                server_cmd = server_cmd + ["-l"]

            for uopt in UOPTIONS:
                uvar = getattr(self, uopt, None)
                if uvar:
                    uvar = uvar.lstrip(" ")
                    server_cmd = server_cmd + ["-u" + uopt + "=", uvar]

            for uopt in UOPTIONS_APPEND:
                uvar_array = getattr(self, uopt, None)
                if uvar_array:
                    for uvar in uvar_array:
                        uv = uvar.lstrip(" ")
                        server_cmd = server_cmd + ["-u" + uopt + "=", uvar]

            if command == "debugserver":
                self.log_gdbserver_message()
                self.check_call(server_cmd, cwd=os.path.dirname(self.e2_server_gdb))
            else:
                if self.gdb_cmd is None:
                    raise ValueError("Cannot debug; gdb is missing")
                if self.elf_name is None:
                    raise ValueError("Cannot debug; elf is missing")

                gdb_script = [
                    "set non-stop on",
                    "set step-mode off",
                    "set mi-async on",
                    "set breakpoint always-inserted on",
                    "set remotetimeout 10",
                    "set tcp connect-timeout 30",
                    "set confirm off",
                    "target extended-remote :" + str(self.gdb_port),
                    "monitor do_nothing",
                    "set backtrace limit 11",
                    "monitor is_target_connected",
                    "monitor set_target," + str(self.target),
                    "monitor is_target_connected",
                    "monitor prg_download_start_on_connect",
                    "load",
                    "monitor prg_download_end",
                    "monitor reset",
                    "monitor enable_stopped_notify_on_connect",
                    "monitor enable_execute_on_connect",
                    "monitor do_nothing",
                    "set backtrace limit 11",
                ]

                client_cmd = (
                    self.gdb_cmd
                    + self.tui_args
                    + [self.elf_name]
                    + sum([["-ex", cmd] for cmd in gdb_script], [])
                )

                self.require(client_cmd[0])
                self.log_gdbserver_message()
                self.run_server_and_client(
                    server_cmd,
                    client_cmd,
                    server_cwd=os.path.dirname(self.e2_server_gdb),
                )
