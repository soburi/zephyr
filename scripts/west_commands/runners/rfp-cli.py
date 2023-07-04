# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Runner for flashing with rfp-cli."""

from runners.core import ZephyrBinaryRunner, RunnerCaps

class RfpCliBinaryRunner(ZephyrBinaryRunner):
    """Runner front-end for rfp-cli."""

    def __init__(
        self,
        cfg,
        device,
        tool,
        interface,
        speed,
        reset=False,
        verify=False,
        rfp_cli="rfp-cli",
    ):
        super().__init__(cfg)

        self.device = device
        self.tool = tool
        self.interface = interface
        self.speed = speed
        self.reset = reset
        self.verify = verify
        self.rfp_cli = rfp_cli
        self.hex_file = cfg.hex_file

    @classmethod
    def name(cls):
        return "rfp-cli"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"})

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument(
            "--device",
            required=True,
            choices=[
                "RA",
                "RH850",
                "RH850/E2x",
                "RH850/U2x",
                "RL78",
                "RL78/G10",
                "RL78/G1M",
                "RL78/G1N",
                "RL78/G15",
                "RL78/G16",
                "RL78/G2x",
                "Renesas Synergy",
                "RE",
                "RX72x",
                "RX71x",
                "RX67x",
                "RX66x",
                "RX65x",
                "RX64x",
                "RX63x",
                "RX62x",
                "RX61x",
                "RX200",
                "RX100",
                "RISC-V/MC",
                "RISC-V/VC",
            ],
            help="target device type",
        )

        parser.add_argument(
            "--tool",
            required=True,
            choices=["jlink", "e1", "e20", "e2", "e2l"],
            help="adapter type",
        )

        parser.add_argument(
            "--interface",
            required=True,
            choices=["swd", "uart1", "uart", "fine"],
            help="adapter type",
        )

        parser.add_argument(
            "--speed",
            default="auto",
            required=False,
            help="Specify baud rate or communication speed (default: auto)",
        )

        parser.add_argument(
            "--reset",
            default=False,
            required=False,
            action="store_true",
            help="reset device at exit, default False",
        )

        parser.add_argument(
            "--verify",
            default=False,
            required=False,
            action="store_true",
            help="verify writes, default False",
        )

        parser.add_argument(
            "--rfp-cli",
            default="rfp-cli",
            help="The path of rfp-cli program"
        )

    @classmethod
    def do_create(cls, cfg, args):
        return RfpCliBinaryRunner(
            cfg,
            device=args.device,
            tool=args.tool,
            interface=args.interface,
            speed=args.speed,
            reset=args.reset,
            rfp_cli=args.rfp_cli,
            verify=args.verify,
        )

    def do_run(self, command, **kwargs):
        cmd_flash = [self.rfp_cli, "-p", "-e", "-run"]

        if self.reset:
            cmd_flash.extend(["-reset"])

        if self.verify:
            cmd_flash.extend(["-v"])

        cmd_flash.extend(["-device", self.device])
        cmd_flash.extend(["-tool", self.tool])
        cmd_flash.extend(["-if", self.interface])
        cmd_flash.extend(["-file", self.hex_file])

        self.require(cmd_flash[0])

        self.check_call(cmd_flash)
