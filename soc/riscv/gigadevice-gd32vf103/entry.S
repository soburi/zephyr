
#include <autoconf.h>
#include <toolchain.h>

#define MSTATUS_MIE   0x8
#define CSR_MMISC_CTL 0x7d0
#define CSR_MTVT2     0x7ec

vector_base:
	j __start_gd32vf103
	.align    2
	.word     0
	.word     0
	.word     trap_entry
	.word     0
	.word     0
	.word  	  0
	.word  	  trap_entry
	.word  	  0
	.word  	  0
	.word  	  0
	.word  	  0
	.word  	  0
	.word  	  0
	.word  	  0
	.word 	  0
	.word  	  0
	.word  	  trap_entry
	.word  	  trap_entry
	.word  	  trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     0
	.word     0
	.word     0
	.word     0
	.word     0
	.word     trap_entry
	.word     0
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     0
	.word     0
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry
	.word     trap_entry

GTEXT(__start_gd32vf103)
SECTION_FUNC(vectors, __start_gd32vf103)
	csrc	mstatus,	MSTATUS_MIE
	/* Jump to logical address first to ensure correct operation of RAM region  */
	la	a0, __start_gd32vf103
	li	a1, 1
	slli	a1, a1, 29
	bleu	a1, a0, _start0800
	srli	a1, a1, 2
	bleu	a1, a0, _start0800
	la	a0, _start0800
	add	a0, a0, a1
	jr	a0


_start0800:

	/* Set the the NMI base to share with mtvec by setting CSR_MMISC_CTL */
	li t0, 0x200
	csrs CSR_MMISC_CTL, t0

	/* Intial the mtvt*/
	la t0, vector_base
	csrw mtvt, t0

	/* Intial the mtvt2 and enable it*/
	la t0, __irq_wrapper
	csrw CSR_MTVT2, t0
	csrs CSR_MTVT2, 0x1

	/* Intial the CSR MTVEC for the Trap ane NMI base addr*/
	la t0, trap_entry
	csrw mtvec, t0

	tail __initialize


.align 6
trap_entry:
	tail __irq_wrapper
