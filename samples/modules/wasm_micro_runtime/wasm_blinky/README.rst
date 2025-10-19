.. _wasm-micro-runtime-wasm_hello_world:

WebAssembly Micro Runtime Hello World
#####################################

Overview
********
The sample project illustrates how to run a WebAssembly (WASM) application with
WebAssembly Micro Runtime (WAMR). The WASM application binary is pre-converted into
a byte array defined in ``test_wasm.h``, which is statically linked with the sample.
The bundled WASM program is built with a :abbr:`WIT (WebAssembly Interface Types)`
definition describing its interface to Zephyr. The program configures the
:dtcompatible:`gpio-leds` device referenced by the ``led0`` devicetree alias and
blinks the LED ten times by calling the WIT-described host functions exported from
Zephyr. The WAMR runtime then loads the binary and executes the LED blink loop from
WebAssembly.

Building and Running
********************

This example can be built in the standard way. The target board must provide a
``led0`` devicetree alias that points to a controllable GPIO LED. For example with
the :zephyr:board:`qemu_x86_nommu` target:

.. zephyr-app-commands::
   :zephyr-app: samples/modules/wasm_micro_runtime/wasm_hello_world/
   :board: qemu_x86_nommu
   :goals: build

For QEMU target, we can launch with following command.

.. code-block:: console

    west build -t run

Sample Output
=============

When the application runs, the LED connected to ``led0`` toggles at 250 ms
intervals for ten cycles. The console shows the elapsed time reported by the host
application once execution completes:

.. code-block:: console

    elpase: 60

Build WASM application
**********************

Install WASI-SDK
================
Download WASI-SDK from https://github.com/CraneStation/wasi-sdk/releases and extract the archive to default path "/opt/wasi-sdk"

Generate WIT bindings and build WASM application with WASI-SDK
=============================================================

The WebAssembly application consumes a WIT world defined in ``zephyr-led.wit``.
Regenerate the bindings with ``wit-bindgen`` and rebuild the WASM binary whenever
the interface or guest source changes. The provided ``Makefile`` automates the
binding generation and compilation steps:

.. code-block:: console

    cd wasm-app
    cargo install wit-bindgen-cli  # if wit-bindgen is not already installed
    make

The build rules default to the WASI SDK installation at ``/opt/wasi-sdk`` and
rebuild the bindings when ``zephyr-led.wit`` changes. Override the ``WASI_SDK``
environment variable if your SDK is installed elsewhere.

Dump WASM binary file into byte array file
==========================================

.. code-block:: console

    cd wasm-app
    make embed

The ``embed`` target depends only on standard POSIX tools (``od``, ``tr``, ``sed``
and ``awk``) to regenerate ``../src/test_wasm.h``. The following one-liner mirrors
the rule when the conversion needs to be run manually:

.. code-block:: console

    od -An -v -tx1 test.wasm | tr -s ' ' '\n' | sed '/^$/d' | \
        awk 'BEGIN {print "unsigned char __aligned(4) wasm_test_file[] = {"} \
             {bytes[NR]=$1} \
             END {for (i=1; i<=NR; ++i) { \
                     if ((i-1)%12==0) printf("    "); \
                     printf("0x%s", bytes[i]); \
                     if (i!=NR) { \
                             if (i%12==0) printf(",\\n"); \
                             else printf(", "); \
                     } else { \
                             printf("\\n"); \
                     } \
             } print("};");}' > ../src/test_wasm.h

References
**********

  - WAMR sample: https://github.com/bytecodealliance/wasm-micro-runtime/tree/main/product-mini/platforms/zephyr/simple
