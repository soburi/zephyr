# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0
#
# pylint: disable=duplicate-code

import argparse
import os
from unittest.mock import call, patch

from conftest import RC_KERNEL_BIN

from runners.openfpgaloader import OpenFPGALoaderBinaryRunner

TEST_BOARD = "test-board"
TEST_OFFSET = "0x600000"

EXPECTED_COMMAND = [
    [
        "openFPGALoader",
        "--board",
        TEST_BOARD,
        "--write-flash",
        RC_KERNEL_BIN,
    ],
]

EXPECTED_COMMAND_WITH_OPTIONS = [
    [
        "openFPGALoader",
        "--board",
        TEST_BOARD,
        "--external-flash",
        "--write-flash",
        "--offset",
        TEST_OFFSET,
        "--verify",
        RC_KERNEL_BIN,
    ],
]


def require_patch(program):
    assert program == "openFPGALoader"


os_path_isfile = os.path.isfile


def os_path_isfile_patch(filename):
    if filename == RC_KERNEL_BIN:
        return True
    return os_path_isfile(filename)


@patch("runners.core.ZephyrBinaryRunner.require", side_effect=require_patch)
@patch("runners.core.ZephyrBinaryRunner.check_call")
def test_openfpgaloader_init(cc, req, runner_config):
    runner = OpenFPGALoaderBinaryRunner(runner_config, board=TEST_BOARD)
    with patch("os.path.isfile", side_effect=os_path_isfile_patch):
        runner.run("flash")
    assert cc.call_args_list == [call(x) for x in EXPECTED_COMMAND]


@patch("runners.core.ZephyrBinaryRunner.require", side_effect=require_patch)
@patch("runners.core.ZephyrBinaryRunner.check_call")
def test_openfpgaloader_create(cc, req, runner_config):
    args = [
        "--board",
        TEST_BOARD,
        "--external-flash",
        "--offset",
        TEST_OFFSET,
        "--verify",
    ]
    parser = argparse.ArgumentParser(allow_abbrev=False)
    OpenFPGALoaderBinaryRunner.add_parser(parser)
    arg_namespace = parser.parse_args(args)
    runner = OpenFPGALoaderBinaryRunner.create(runner_config, arg_namespace)
    with patch("os.path.isfile", side_effect=os_path_isfile_patch):
        runner.run("flash")
    assert cc.call_args_list == [call(x) for x in EXPECTED_COMMAND_WITH_OPTIONS]
