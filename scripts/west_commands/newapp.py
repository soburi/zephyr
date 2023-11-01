# Copyright (c) 2023 TOKITA Hiroshi
#
# SPDX-License-Identifier: Apache-2.0

'''west "newapp" command'''

from west.commands import WestCommand

from run_common import add_parser_common, do_run_common, get_build_dir
from build_helpers import load_domains
import os
import argparse
import shutil

IDE_SETTING_REL_PATH = 'newapp'

class NewApp(WestCommand):

    def __init__(self):
        super(NewApp, self).__init__(
            'newapp',
            # Keep this in sync with the string in west-commands.yml.
            'create new application project',
            "Permanently reprogram a board's newapp with a new binary.",
            accepts_unknown_args=True)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description)

        parser.add_argument('projectname', type=str,
                            help='''Shell that which the completion script is intended for.''')

        parser.add_argument('-t', '--template', type=str, default="samples/hello_world",
                           help='''image signing tool name; imgtool and rimage
                           are currently supported''')

        parser.add_argument('-b', '--board', nargs='+', type=str,
                           help='''image signing tool name; imgtool and rimage
                           are currently supported''')

        parser.add_argument('-f', '--force', type=bool,
                           help='''image signing tool name; imgtool and rimage
                           are currently supported''')

        parser.add_argument('-i', '--ide', choices=['vscode'],
                           help='''image signing tool name; imgtool and rimage
                           are currently supported''')

        return parser

        #return add_parser_common(self, parser_adder)

    def do_run(self, args, runner_args):
        print(args)
        print(runner_args)

        if os.path.exists(args.projectname) and not args.force:
            exit(-1)

        shutil.copytree(os.environ['ZEPHYR_BASE'] + '/' + args.template, args.projectname)

        if args.ide:
            cfg = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                              IDE_SETTING_REL_PATH, args.ide)
            print(cfg)

            shutil.copytree(cfg, args.projectname, dirs_exist_ok=True)

        #do_run_common(self, my_args, runner_args, domains=domains)
