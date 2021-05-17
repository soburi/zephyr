# Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
# SPDX-License-Identifier: Apache-2.0
'''Runner for flashing with RV-Link.'''
# https://gitee.com/zoomdy/RV-LINK
# https://gitlab.melroy.org/micha/rv-link

import pathlib
import signal

from runners.core import ZephyrBinaryRunner, RunnerCaps

class RvLinkRunner(ZephyrBinaryRunner):
    '''Runner front-end for RV-Link probe.'''

    def __init__(self, cfg, gdb_serial):
        super().__init__(cfg)
        self.gdb = [cfg.gdb] if cfg.gdb else None
        self.elf_file = pathlib.Path(cfg.elf_file).as_posix()
        self.gdb_serial = gdb_serial

    @classmethod
    def name(cls):
        return 'rvlink'

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={'flash', 'debug', 'attach'})

    @classmethod
    def do_create(cls, cfg, args):
        return RvLinkRunner(cfg, args.gdb_serial)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument('--gdb-serial', default='/dev/ttyACM0',
                            help='GDB serial port')

    def rvl_flash(self, command, **kwargs):
        if self.elf_file is None:
            raise ValueError('Cannot debug; elf file is missing')
        command = (self.gdb +
                   ['-ex', "set confirm off",
                    '-ex', "file {}".format(self.elf_file),
                    '-ex', "target remote {}".format(self.gdb_serial),
                    '-ex', "load",
                    '-ex', "quit",
                    '-silent'])
        self.check_call(command)

    def check_call_ignore_sigint(self, command):
        previous = signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            self.check_call(command)
        finally:
            signal.signal(signal.SIGINT, previous)

    def rvl_attach(self, command, **kwargs):
        if self.elf_file is None:
            command = (self.gdb +
                       ['-ex', "set confirm off",
                        '-ex', "set arch riscv:rv32",
                        '-ex', "target remote {}".format(self.gdb_serial)])
        else:
            command = (self.gdb +
                       ['-ex', "set confirm off",
                        '-ex', "file {}".format(self.elf_file),
                        '-ex', "target remote {}".format(self.gdb_serial)])
        self.check_call_ignore_sigint(command)

    def rvl_debug(self, command, **kwargs):
        if self.elf_file is None:
            raise ValueError('Cannot debug; elf file is missing')
        command = (self.gdb +
                   ['-ex', "set confirm off",
                    '-ex', "file {}".format(self.elf_file),
                    '-ex', "target remote {}".format(self.gdb_serial),
                    '-ex', "load"])
        self.check_call_ignore_sigint(command)

    def do_run(self, command, **kwargs):
        if self.gdb is None:
            raise ValueError('Cannot execute; gdb not specified')
        self.require(self.gdb[0])

        if command == 'flash':
            self.rvl_flash(command, **kwargs)
        elif command == 'debug':
            self.rvl_debug(command, **kwargs)
        elif command == 'attach':
            self.rvl_attach(command, **kwargs)
        else:
            self.rvl_flash(command, **kwargs)
