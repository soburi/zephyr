.. zephyr:code-sample:: aw88298
   :name: AW88298 smart amplifier playback
   :relevant-api: audio_codec_interface, i2s_interface

   Play a generated sine wave through the M5Stack CoreS3's AW88298 smart
   amplifier using the Zephyr audio codec API together with the ESP32-S3 I2S
   driver.

Overview
********

This sample demonstrates how to control the on-board AW88298 amplifier on the
:zephyr:board:`m5stack_cores3` via the :ref:`audio codec API <audio_codec_interface>`.
The application configures the codec for playback, enables the amplifier through
its AW9523-controlled power pin, and continuously feeds sine wave data to the
``I2S1`` peripheral.

Requirements
************

* :zephyr:board:`m5stack_cores3/esp32s3/procpu`
* Internal loudspeaker connected through the AW88298 amplifier

The hardware connections follow the official CoreS3 schematic: the amplifier is
on the internal I2C bus at address ``0x36``, its enable pin is routed through the
AW9523 GPIO expander (port 0, pin 2), and the audio stream uses ``I2S1`` with
``GPIO34`` (BCK), ``GPIO33`` (LRCK), and ``GPIO13`` (SDOUT).

Building and Running
********************

The code can be found in :zephyr_file:`samples/drivers/audio/aw88298`.

To build and flash the application on the CoreS3:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/audio/aw88298
   :board: m5stack_cores3/esp32s3/procpu
   :goals: build flash
   :compact:

Once programmed, the sample plays a short sine tone and prints status messages
on the serial console.
