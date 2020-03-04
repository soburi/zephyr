//See LICENSE for license details.
#include <gd32vf103.h>
#include <stdint.h>
#include <stdio.h>
#include "riscv_encoding.h"
#include <init.h>

static int _init(struct device* dev)
{
	ARG_UNUSED(dev);

	SystemInit();

    /* Before enter into main, add the cycle/instret disable by default to save power,
    only use them when needed to measure the cycle/instret */
	__asm__("csrsi " STRINGIFY(CSR_MCOUNTINHIBIT) ",0x5");
	return 0;
}

SYS_INIT(_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
