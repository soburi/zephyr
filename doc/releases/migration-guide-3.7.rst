:orphan:

.. _migration_3.7:

Migration guide to Zephyr v3.7.0 (Working Draft)
################################################

This document describes the changes required when migrating your application from Zephyr v3.6.0 to
Zephyr v3.7.0.

Any other changes (not directly related to migrating applications) can be found in
the :ref:`release notes<zephyr_3.7>`.

.. contents::
    :local:
    :depth: 2

Build System
************

* Completely overhauled the way SoCs and boards are defined. This requires all
  out-of-tree SoCs and boards to be ported to the new model. See the
  :ref:`board_porting_guide` for more detailed information.
  * Pull Request: https://github.com/zephyrproject-rtos/zephyr/pull/69607
  * Issue: https://github.com/zephyrproject-rtos/zephyr/issues/51831

Kernel
******

Boards
******

Modules
*******

MCUboot
=======

zcbor
=====

Device Drivers and Devicetree
*****************************

Analog-to-Digital Converter (ADC)
=================================

Bluetooth HCI
=============

Controller Area Network (CAN)
=============================

* Removed the following deprecated CAN controller devicetree properties. Out-of-tree boards using
  these properties need to switch to using the ``bus-speed``, ``sample-point``, ``bus-speed-data``,
  and ``sample-point-data`` devicetree properties for specifying the initial CAN bitrate:

  * ``sjw``
  * ``prop-seg``
  * ``phase-seg1``
  * ``phase-seg1``
  * ``sjw-data``
  * ``prop-seg-data``
  * ``phase-seg1-data``
  * ``phase-seg1-data``

* Support for manual bus-off recovery was reworked:

  * Automatic bus recovery will always be enabled upon driver initialization regardless of Kconfig
    options. Since CAN controllers are initialized in "stopped" state, no unwanted bus-off recovery
    will be started at this point.
  * The Kconfig ``CONFIG_CAN_AUTO_BUS_OFF_RECOVERY`` was renamed (and inverted) to
    :kconfig:option:`CONFIG_CAN_MANUAL_RECOVERY_MODE`, which is disabled by default. This Kconfig
    option enables support for the :c:func:`can_recover()` API function and a new manual recovery mode
    (see the next bullet).
  * A new CAN controller operational mode :c:macro:`CAN_MODE_MANUAL_RECOVERY` was added. Support for
    this is only enabled if :kconfig:option:`CONFIG_CAN_MANUAL_RECOVERY_MODE` is enabled. Having
    this as a mode allows applications to inquire whether the CAN controller supports manual
    recovery mode via the :c:func:`can_get_capabilities` API function. The application can then
    either fail initialization or rely on automatic bus-off recovery. Having this as a mode
    furthermore allows CAN controller drivers not supporting manual recovery mode to fail early in
    :c:func:`can_set_mode` during application startup instead of failing when :c:func:`can_recover`
    is called at a later point in time.

Display
=======

Flash
=====

General Purpose I/O (GPIO)
==========================

Input
=====

Interrupt Controller
====================

Sensors
=======

Serial
======

Timer
=====

Bluetooth
*********

Bluetooth Mesh
==============

Bluetooth Audio
===============

Networking
**********

* The zperf zperf_results struct is changed to support 64 bits transferred bytes (total_len)
  and test duration (time_in_us and client_time_in_us), instead of 32 bits. This will make
  the long-duration zperf test show with correct throughput result.
  (:github:`69500`)

Other Subsystems
****************

CFB
===

* Changed API to support multiple displays.

  Introduced :c:struct:`cfb_display` structure to represent a display.
  Therefore, use :c:func:`cfb_display_init` instead of deprecated cfb_framebuffer_init.
  cfb_framebuffer_init has been deprecated.

  Added :c:func:`cfb_display_get_framebuffer` to get the framebuffer associated with a display.

  The following API has changed the first argument pointer of :c:struct:`device` to
  pointer of :c:struct:`cfb_framebuffer`.
  Use the reference obtained with :c:func:`cfb_display_get_framebuffer` above.

  * :c:func:`cfb_print`
  * :c:func:`cfb_draw_text`
  * :c:func:`cfb_draw_point`
  * :c:func:`cfb_draw_line`
  * :c:func:`cfb_draw_rect`
  * :c:func:`cfb_invert_area`
  * :c:func:`cfb_set_kerning`
  * :c:func:`cfb_clear` (renamed from `cfb_framebuffer_clear`)
  * :c:func:`cfb_invert` (renamed from `cfb_framebuffer_invert`)
  * :c:func:`cfb_finalize` (renamed from `cfb_framebuffer_finalize`)
  * :c:func:`cfb_set_font` (renamed from `cfb_framebuffer_finalize`)

  Typical usage is below.

  .. code-block:: c

    struct cfb_display disp;
    struct cfb_framebuffer *fb;

    cfb_display_init(&disp);
    fb = cfb_display_get_framebuffer(&disp);
    cfb_print(fb, "Hello!", 0, 0);

  Also, please see the sample in ``samples/display/cfb``.

* Remove unnecessary arguments for font-related APIs

  The following API has removed the first argument pointer of :c:struct:`device`.
  This argument was not used internally, so simply remove it.

  * :c:func:`cfb_get_font_size`
  * :c:func:`cfb_get_numof_fonts`

* Change coordinate specification to signed integer

  Change the following function arguments and structure fields used to specify
  coordinates to signed integers.

  * :c:func:`cfb_print`
  * :c:func:`cfb_invert_area`
  * :c:struct:`cfb_position`



LoRaWAN
=======

MCUmgr
======

Shell
=====

ZBus
====

Userspace
*********

Architectures
*************

Xtensa
======
