# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

if(${WEST_VERBOSE})
  set(COMMAND_ECHO_OUTPUT STDOUT)
else()
  set(COMMAND_ECHO_OUTPUT NONE)
endif()
