/*
 * Copyright (c) 2026 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#define DT_DRV_COMPAT renesas_rcar_canfd
//#define DEBUG

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/devicetree/clocks.h>
#include <zephyr/drivers/clock_control/renesas_cpg_mssr.h>
#include <zephyr/dt-bindings/clock/r8a779g0_cpg_mssr.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/logging/log.h>
// #include <zephyr/irq.h>

LOG_MODULE_REGISTER(can_rcar, CONFIG_CAN_LOG_LEVEL);

#include <zephyr/sys/util_internal.h> // for BIT macro
/* Start: Port from Linux driver */
/* Global register bits */

/* RSCFDnCFDGRMCFG */
#define RCANFD_GRMCFG_RCMC		BIT(0)

/* RSCFDnCFDGCFG / RSCFDnGCFG */
#define RCANFD_GCFG_EEFE		BIT(6)
#define RCANFD_GCFG_CMPOC		BIT(5)	/* CAN FD only */
#define RCANFD_GCFG_DCS			BIT(4)
#define RCANFD_GCFG_DCE			BIT(1)
#define RCANFD_GCFG_TPRI		BIT(0)

/* RSCFDnCFDGCTR / RSCFDnGCTR */
#define RCANFD_GCTR_TSRST		BIT(16)
#define RCANFD_GCTR_CFMPOFIE		BIT(11)	/* CAN FD only */
#define RCANFD_GCTR_THLEIE		BIT(10)
#define RCANFD_GCTR_MEIE		BIT(9)
#define RCANFD_GCTR_DEIE		BIT(8)
#define RCANFD_GCTR_GSLPR		BIT(2)
#define RCANFD_GCTR_GMDC_MASK		(0x3)
#define RCANFD_GCTR_GMDC_GOPM		(0x0)
#define RCANFD_GCTR_GMDC_GRESET		(0x1)
#define RCANFD_GCTR_GMDC_GTEST		(0x2)

/* RSCFDnCFDGSTS / RSCFDnGSTS */
#define RCANFD_GSTS_GRAMINIT		BIT(3)
#define RCANFD_GSTS_GSLPSTS		BIT(2)
#define RCANFD_GSTS_GHLTSTS		BIT(1)
#define RCANFD_GSTS_GRSTSTS		BIT(0)
/* Non-operational status */
#define RCANFD_GSTS_GNOPM		(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* RSCFDnCFDGERFL / RSCFDnGERFL */
#define RCANFD_GERFL_EEF		GENMASK(23, 16)
#define RCANFD_GERFL_CMPOF		BIT(3)	/* CAN FD only */
#define RCANFD_GERFL_THLES		BIT(2)
#define RCANFD_GERFL_MES		BIT(1)
#define RCANFD_GERFL_DEF		BIT(0)

#define RCANFD_GERFL_ERR(gpriv, x) \
({\
	typeof(gpriv) (_gpriv) = (gpriv); \
	((x) & ((FIELD_PREP(RCANFD_GERFL_EEF, (_gpriv)->channels_mask)) | \
		RCANFD_GERFL_MES | ((_gpriv)->fdmode ? RCANFD_GERFL_CMPOF : 0))); \
})

/* AFL Rx rules registers */

/* RSCFDnCFDGAFLECTR / RSCFDnGAFLECTR */
#define RCANFD_GAFLECTR_AFLDAE		BIT(8)
#define RCANFD_GAFLECTR_AFLPN(gpriv, page_num)	((page_num) & (gpriv)->info->max_aflpn)

/* RSCFDnCFDGAFLIDj / RSCFDnGAFLIDj */
#define RCANFD_GAFLID_GAFLLB		BIT(29)

/* RSCFDnCFDGAFLP1_j / RSCFDnGAFLP1_j */
#define RCANFD_GAFLP1_GAFLFDP(x)	(1 << (x))

/* Channel register bits */

/* RSCFDnCmCFG - Classical CAN only */
#define RCANFD_CFG_SJW		GENMASK(25, 24)
#define RCANFD_CFG_TSEG2	GENMASK(22, 20)
#define RCANFD_CFG_TSEG1	GENMASK(19, 16)
#define RCANFD_CFG_BRP		GENMASK(9, 0)

/* RSCFDnCFDCmNCFG - CAN FD only */
#define RCANFD_NCFG_NBRP	GENMASK(9, 0)

/* RSCFDnCFDCmCTR / RSCFDnCmCTR */
#define RCANFD_CCTR_CTME		BIT(24)
#define RCANFD_CCTR_ERRD		BIT(23)
#define RCANFD_CCTR_BOM_MASK		(0x3 << 21)
#define RCANFD_CCTR_BOM_ISO		(0x0 << 21)
#define RCANFD_CCTR_BOM_BENTRY		(0x1 << 21)
#define RCANFD_CCTR_BOM_BEND		(0x2 << 21)
#define RCANFD_CCTR_TDCVFIE		BIT(19)
#define RCANFD_CCTR_SOCOIE		BIT(18)
#define RCANFD_CCTR_EOCOIE		BIT(17)
#define RCANFD_CCTR_TAIE		BIT(16)
#define RCANFD_CCTR_ALIE		BIT(15)
#define RCANFD_CCTR_BLIE		BIT(14)
#define RCANFD_CCTR_OLIE		BIT(13)
#define RCANFD_CCTR_BORIE		BIT(12)
#define RCANFD_CCTR_BOEIE		BIT(11)
#define RCANFD_CCTR_EPIE		BIT(10)
#define RCANFD_CCTR_EWIE		BIT(9)
#define RCANFD_CCTR_BEIE		BIT(8)
#define RCANFD_CCTR_CSLPR		BIT(2)
#define RCANFD_CCTR_CHMDC_MASK		(0x3)
#define RCANFD_CCTR_CHDMC_COPM		(0x0)
#define RCANFD_CCTR_CHDMC_CRESET	(0x1)
#define RCANFD_CCTR_CHDMC_CHLT		(0x2)

/* RSCFDnCFDCmSTS / RSCFDnCmSTS */
#define RCANFD_CSTS_COMSTS		BIT(7)
#define RCANFD_CSTS_RECSTS		BIT(6)
#define RCANFD_CSTS_TRMSTS		BIT(5)
#define RCANFD_CSTS_BOSTS		BIT(4)
#define RCANFD_CSTS_EPSTS		BIT(3)
#define RCANFD_CSTS_SLPSTS		BIT(2)
#define RCANFD_CSTS_HLTSTS		BIT(1)
#define RCANFD_CSTS_CRSTSTS		BIT(0)

#define RCANFD_CSTS_TECCNT(x)		(((x) >> 24) & 0xff)
#define RCANFD_CSTS_RECCNT(x)		(((x) >> 16) & 0xff)

/* RSCFDnCFDCmERFL / RSCFDnCmERFL */
#define RCANFD_CERFL_ADERR		BIT(14)
#define RCANFD_CERFL_B0ERR		BIT(13)
#define RCANFD_CERFL_B1ERR		BIT(12)
#define RCANFD_CERFL_CERR		BIT(11)
#define RCANFD_CERFL_AERR		BIT(10)
#define RCANFD_CERFL_FERR		BIT(9)
#define RCANFD_CERFL_SERR		BIT(8)
#define RCANFD_CERFL_ALF		BIT(7)
#define RCANFD_CERFL_BLF		BIT(6)
#define RCANFD_CERFL_OVLF		BIT(5)
#define RCANFD_CERFL_BORF		BIT(4)
#define RCANFD_CERFL_BOEF		BIT(3)
#define RCANFD_CERFL_EPF		BIT(2)
#define RCANFD_CERFL_EWF		BIT(1)
#define RCANFD_CERFL_BEF		BIT(0)

#define RCANFD_CERFL_ERR(x)		((x) & (0x7fff)) /* above bits 14:0 */

/* RSCFDnCFDCmDCFG */
#define RCANFD_DCFG_DBRP		GENMASK(7, 0)

/* RSCFDnCFDCmFDCFG */
#define RCANFD_GEN4_FDCFG_CLOE		BIT(30)
#define RCANFD_GEN4_FDCFG_FDOE		BIT(28)
#define RCANFD_FDCFG_TDCO		GENMASK(23, 16)
#define RCANFD_FDCFG_TDCE		BIT(9)
#define RCANFD_FDCFG_TDCOC		BIT(8)

/* RSCFDnCFDCmFDSTS */
#define RCANFD_FDSTS_SOC		GENMASK(31, 24)
#define RCANFD_FDSTS_EOC		GENMASK(23, 16)
#define RCANFD_GEN4_FDSTS_TDCVF		BIT(15)
#define RCANFD_GEN4_FDSTS_PNSTS		GENMASK(13, 12)
#define RCANFD_FDSTS_SOCO		BIT(9)
#define RCANFD_FDSTS_EOCO		BIT(8)
#define RCANFD_FDSTS_TDCVF		BIT(7)
#define RCANFD_FDSTS_TDCR		GENMASK(7, 0)

/* RSCFDnCFDRFCCx */
#define RCANFD_RFCC_RFIM		BIT(12)
#define RCANFD_RFCC_RFDC(x)		(((x) & 0x7) << 8)
#define RCANFD_RFCC_RFPLS(x)		(((x) & 0x7) << 4)
#define RCANFD_RFCC_RFIE		BIT(1)
#define RCANFD_RFCC_RFE			BIT(0)

/* RSCFDnCFDRFSTSx */
#define RCANFD_RFSTS_RFIF		BIT(3)
#define RCANFD_RFSTS_RFMLT		BIT(2)
#define RCANFD_RFSTS_RFFLL		BIT(1)
#define RCANFD_RFSTS_RFEMP		BIT(0)

/* RSCFDnCFDRFIDx */
#define RCANFD_RFID_RFIDE		BIT(31)
#define RCANFD_RFID_RFRTR		BIT(30)

/* RSCFDnCFDRFPTRx */
#define RCANFD_RFPTR_RFDLC(x)		(((x) >> 28) & 0xf)

/* RSCFDnCFDRFFDSTSx */
#define RCANFD_RFFDSTS_RFFDF		BIT(2)
#define RCANFD_RFFDSTS_RFBRS		BIT(1)
#define RCANFD_RFFDSTS_RFESI		BIT(0)

/* Common FIFO bits */

/* RSCFDnCFDCFCCk */
#define RCANFD_CFCC_CFTML(gpriv, cftml) \
({\
	typeof(gpriv) (_gpriv) = (gpriv); \
	(((cftml) & (_gpriv)->info->max_cftml) << (_gpriv)->info->sh->cftml); \
})
#define RCANFD_CFCC_CFM(gpriv, x)	(((x) & 0x3) << (gpriv)->info->sh->cfm)
#define RCANFD_CFCC_CFIM		BIT(12)
#define RCANFD_CFCC_CFDC(gpriv, x)	(((x) & 0x7) << (gpriv)->info->sh->cfdc)
#define RCANFD_CFCC_CFPLS(x)		(((x) & 0x7) << 4)
#define RCANFD_CFCC_CFTXIE		BIT(2)
#define RCANFD_CFCC_CFE			BIT(0)

/* RSCFDnCFDCFSTSk */
#define RCANFD_CFSTS_CFMC(x)		(((x) >> 8) & 0xff)
#define RCANFD_CFSTS_CFTXIF		BIT(4)
#define RCANFD_CFSTS_CFMLT		BIT(2)
#define RCANFD_CFSTS_CFFLL		BIT(1)
#define RCANFD_CFSTS_CFEMP		BIT(0)

/* RSCFDnCFDCFIDk */
#define RCANFD_CFID_CFIDE		BIT(31)
#define RCANFD_CFID_CFRTR		BIT(30)

/* RSCFDnCFDCFPTRk */
#define RCANFD_CFPTR_CFDLC(x)		(((x) & 0xf) << 28)

/* RSCFDnCFDCFFDCSTSk */
#define RCANFD_CFFDCSTS_CFFDF		BIT(2)
#define RCANFD_CFFDCSTS_CFBRS		BIT(1)
#define RCANFD_CFFDCSTS_CFESI		BIT(0)

/* This controller supports either Classical CAN only mode or CAN FD only mode.
 * These modes are supported in two separate set of register maps & names.
 * However, some of the register offsets are common for both modes. Those
 * offsets are listed below as Common registers.
 *
 * The CAN FD only mode specific registers & Classical CAN only mode specific
 * registers are listed separately. Their register names starts with
 * RCANFD_F_xxx & RCANFD_C_xxx respectively.
 */

/* Common registers */

/* RSCFDnCFDCmNCFG / RSCFDnCmCFG */
#define RCANFD_CCFG(m)			(0x0000 + (0x10 * (m)))
/* RSCFDnCFDCmCTR / RSCFDnCmCTR */
#define RCANFD_CCTR(m)			(0x0004 + (0x10 * (m)))
/* RSCFDnCFDCmSTS / RSCFDnCmSTS */
#define RCANFD_CSTS(m)			(0x0008 + (0x10 * (m)))
/* RSCFDnCFDCmERFL / RSCFDnCmERFL */
#define RCANFD_CERFL(m)			(0x000C + (0x10 * (m)))

/* RSCFDnCFDGCFG / RSCFDnGCFG */
#define RCANFD_GCFG			(0x0084)
/* RSCFDnCFDGCTR / RSCFDnGCTR */
#define RCANFD_GCTR			(0x0088)
/* RSCFDnCFDGCTS / RSCFDnGCTS */
#define RCANFD_GSTS			(0x008c)
/* RSCFDnCFDGERFL / RSCFDnGERFL */
#define RCANFD_GERFL			(0x0090)
/* RSCFDnCFDGTSC / RSCFDnGTSC */
#define RCANFD_GTSC			(0x0094)
/* RSCFDnCFDGAFLECTR / RSCFDnGAFLECTR */
#define RCANFD_GAFLECTR			(0x0098)
/* RSCFDnCFDGAFLCFG / RSCFDnGAFLCFG */
#define RCANFD_GAFLCFG(w)		(0x009c + (0x04 * (w)))
/* RSCFDnCFDRMNB / RSCFDnRMNB */
#define RCANFD_RMNB			(0x00a4)
/* RSCFDnCFDRMND / RSCFDnRMND */
#define RCANFD_RMND(y)			(0x00a8 + (0x04 * (y)))

/* RSCFDnCFDRFCCx / RSCFDnRFCCx */
//#define RCANFD_RFCC(gpriv, x)		((gpriv)->info->regs->rfcc + (0x04 * (x)))
#define RCANFD_RFCC(gpriv, x)		(0x00c0 + (0x04 * (x)))
/* RSCFDnCFDRFSTSx / RSCFDnRFSTSx */
#define RCANFD_RFSTS(gpriv, x)		(RCANFD_RFCC(gpriv, x) + 0x20)
/* RSCFDnCFDRFPCTRx / RSCFDnRFPCTRx */
#define RCANFD_RFPCTR(gpriv, x)		(RCANFD_RFCC(gpriv, x) + 0x40)

/* Common FIFO Control registers */

/* RSCFDnCFDCFCCx / RSCFDnCFCCx */
#if 0
#define RCANFD_CFCC(gpriv, ch, idx) \
	((gpriv)->info->regs->cfcc + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFSTSx / RSCFDnCFSTSx */
#define RCANFD_CFSTS(gpriv, ch, idx) \
	((gpriv)->info->regs->cfsts + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFPCTRx / RSCFDnCFPCTRx */
#define RCANFD_CFPCTR(gpriv, ch, idx) \
	((gpriv)->info->regs->cfpctr + (0x0c * (ch)) + (0x04 * (idx)))
#endif
#define RCANFD_CFCC(gpriv, ch, idx) (0x0120 + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFSTSx / RSCFDnCFSTSx */
#define RCANFD_CFSTS(gpriv, ch, idx) (0x01e0 + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFPCTRx / RSCFDnCFPCTRx */
#define RCANFD_CFPCTR(gpriv, ch, idx) (0x0240 + (0x0c * (ch)) + (0x04 * (idx)))

/* RSCFDnCFDGRMCFG */
#define RCANFD_GRMCFG			(0x04fc)

/* RSCFDnCFDGAFLIDj / RSCFDnGAFLIDj */
#define RCANFD_GAFLID(offset, j)	((offset) + (0x10 * (j)))
/* RSCFDnCFDGAFLMj / RSCFDnGAFLMj */
#define RCANFD_GAFLM(offset, j)		((offset) + 0x04 + (0x10 * (j)))
/* RSCFDnCFDGAFLP0j / RSCFDnGAFLP0j */
#define RCANFD_GAFLP0(offset, j)	((offset) + 0x08 + (0x10 * (j)))
/* RSCFDnCFDGAFLP1j / RSCFDnGAFLP1j */
#define RCANFD_GAFLP1(offset, j)	((offset) + 0x0c + (0x10 * (j)))

/* Classical CAN only mode register map */

/* RSCFDnGAFLXXXj offset */
#define RCANFD_C_GAFL_OFFSET		(0x0500)

/* RSCFDnRFXXx -> RCANFD_C_RFXX(x) */
#define RCANFD_C_RFOFFSET	(0x0e00)
#define RCANFD_C_RFID(x)	(RCANFD_C_RFOFFSET + (0x10 * (x)))
#define RCANFD_C_RFPTR(x)	(RCANFD_C_RFOFFSET + 0x04 + (0x10 * (x)))
#define RCANFD_C_RFDF(x, df) \
		(RCANFD_C_RFOFFSET + 0x08 + (0x10 * (x)) + (0x04 * (df)))

/* RSCFDnCFXXk -> RCANFD_C_CFXX(ch, k) */
#define RCANFD_C_CFOFFSET		(0x0e80)

#define RCANFD_C_CFID(ch, idx) \
	(RCANFD_C_CFOFFSET + (0x30 * (ch)) + (0x10 * (idx)))

#define RCANFD_C_CFPTR(ch, idx)	\
	(RCANFD_C_CFOFFSET + 0x04 + (0x30 * (ch)) + (0x10 * (idx)))

#define RCANFD_C_CFDF(ch, idx, df) \
	(RCANFD_C_CFOFFSET + 0x08 + (0x30 * (ch)) + (0x10 * (idx)) + (0x04 * (df)))

/* R-Car Gen4 Classical and CAN FD mode specific register map */
#define RCANFD_GEN4_GAFL_OFFSET		(0x1800)

#define RCANFD_GEN4_GAFL_OFFSET		(0x1800)

/* CAN FD mode specific register map */

/* RSCFDnCFDCmXXX -> gpriv->fcbase[m].xxx */
/*
struct rcar_canfd_f_c {
	u32 dcfg;
	u32 cfdcfg;
	u32 cfdctr;
	u32 cfdsts;
	u32 cfdcrc;
	u32 pad[3];
};
*/

/* RSCFDnCFDGAFLXXXj offset */
#define RCANFD_F_GAFL_OFFSET		(0x1000)

/* RSCFDnCFDRFXXx -> RCANFD_F_RFXX(x) */
// #define RCANFD_F_RFOFFSET(gpriv)	((gpriv)->info->regs->rfoffset)
#define RCANFD_F_RFOFFSET(gpriv)	(0x6000)
#define RCANFD_F_RFID(gpriv, x)		(RCANFD_F_RFOFFSET(gpriv) + (0x80 * (x)))
#define RCANFD_F_RFPTR(gpriv, x)	(RCANFD_F_RFOFFSET(gpriv) + 0x04 + (0x80 * (x)))
#define RCANFD_F_RFFDSTS(gpriv, x)	(RCANFD_F_RFOFFSET(gpriv) + 0x08 + (0x80 * (x)))
#define RCANFD_F_RFDF(gpriv, x, df) \
	(RCANFD_F_RFOFFSET(gpriv) + 0x0c + (0x80 * (x)) + (0x04 * (df)))

/* RSCFDnCFDCFXXk -> RCANFD_F_CFXX(ch, k) */
// #define RCANFD_F_CFOFFSET(gpriv)	((gpriv)->info->regs->cfoffset)
#define RCANFD_F_CFOFFSET(gpriv)	(0x6400)

#define RCANFD_F_CFID(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFPTR(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x04 + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFFDCSTS(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x08 + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFDF(gpriv, ch, idx, df) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x0c + (0x180 * (ch)) + (0x80 * (idx)) + \
	 (0x04 * (df)))

/* Constants */
#define RCANFD_FIFO_DEPTH		8	/* Tx FIFO depth */
#define RCANFD_NAPI_WEIGHT		8	/* Rx poll quota */

#define RCANFD_NUM_CHANNELS		8	/* Eight channels max */

#define RCANFD_GAFL_PAGENUM(entry)	((entry) / 16)
#define RCANFD_CHANNEL_NUMRULES		1	/* only one rule per channel */

/* Rx FIFO is a global resource of the controller. There are 8 such FIFOs
 * available. Each channel gets a dedicated Rx FIFO (i.e.) the channel
 * number is added to RFFIFO index.
 */
#define RCANFD_RFFIFO_IDX		0

/* Tx/Rx or Common FIFO is a per channel resource. Each channel has 3 Common
 * FIFOs dedicated to them. Use the first (index 0) FIFO out of the 3 for Tx.
 */
#define RCANFD_CFFIFO_IDX		0

/* End: Port from Linux driver */

#define RSCFD0CFDFESTS			0x2a0
#define RCANFD_CFDCFCCE(d)		(0x180 + 0x004 * d)

/* Required by DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(_dev) \
	((const struct rcar_canfd_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct rcar_canfd_data *)(_dev)->data)

struct rcar_canfd_config {
    const struct pinctrl_dev_config *pincfg;
    DEVICE_MMIO_NAMED_ROM(mmio);
};

struct rcar_canfd_data {
    /* 必要なら状態保持 */
    DEVICE_MMIO_NAMED_RAM(mmio);
};

static inline uint32_t reg_read(const struct device *dev, uint32_t off)
{
    return sys_read32(DEVICE_MMIO_NAMED_GET(dev, mmio) + off);
}
static inline void reg_write(const struct device *dev, uint32_t off, uint32_t v)
{
    sys_write32(v, DEVICE_MMIO_NAMED_GET(dev, mmio) + off);
}
static inline uintptr_t reg_addr(const struct device *dev, uint32_t off)
{
    return (uintptr_t)(DEVICE_MMIO_NAMED_GET(dev, mmio) + off);
}

#define CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE		64
static uint32_t get_dlc_from_length(uint32_t length, uint32_t *adjusted_length){
	struct length_code_t
	{
		uint32_t dlc;
		uint32_t adjusted_length;
	};
	static struct length_code_t length_code_table[CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE + 1] =
	{
		{ 0x0,  0 }, { 0x1,  1 }, { 0x2,  2 }, { 0x3,  3 }, { 0x4,  4 }, { 0x5,  5 }, { 0x6,  6 }, { 0x7,  7 },
		{ 0x8,  8 }, { 0x9, 12 }, { 0x9, 12 }, { 0x9, 12 }, { 0x9, 12 }, { 0xA, 16 }, { 0xA, 16 }, { 0xA, 16 },
		{ 0xA, 16 }, { 0xB, 20 }, { 0xB, 20 }, { 0xB, 20 }, { 0xB, 20 }, { 0xC, 24 }, { 0xC, 24 }, { 0xC, 24 },
		{ 0xC, 24 }, { 0xD, 32 }, { 0xD, 32 }, { 0xD, 32 }, { 0xD, 32 }, { 0xD, 32 }, { 0xD, 32 }, { 0xD, 32 },
		{ 0xD, 32 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 },
		{ 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 }, { 0xE, 48 },
		{ 0xE, 48 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 },
		{ 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 }, { 0xF, 64 },
		{ 0xF, 64 }
	};

	if (length > CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE)
		return 0;

	if (adjusted_length != NULL)
		*adjusted_length = length_code_table[length].adjusted_length;

	return length_code_table[length].dlc;
}

uint32_t get_length_from_dlc(uint32_t dlc)
{
	static uint32_t length[16] =
	{
		 0,  1,  2,  3,  4,  5,  6,  7,
		 8, 12, 16, 20, 24, 32, 48, 64
	};

	if (dlc > 0x0F)
		return 0;

	return length[dlc];
}

#define CAN_ID_STANDARD_MASK                   (0x7FFU)
#define CAN_ID_EXTENDED_MASK                   (0x1FFFFFFFU)
#define CAN_CLASSIC_FRAME_MAXIMUM_PAYLOAD_SIZE (8)
#define CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE      (64)

#define CAN_ID_TYPE_MASK        (0xC0000000U) // (0x03 << 30)
#define CAN_ID_TYPE_STANDARD    (0x00U << 30)
#define CAN_ID_TYPE_FD_STANDARD (0x01U << 30)
#define CAN_ID_TYPE_EXTENDED    (0x02U << 30)
#define CAN_ID_TYPE_FD_EXTENDED (0x03U << 30)

int rcar_canfd_send(const struct device *dev, int ch, uint32_t id, const uint8_t *data, uint8_t len)
{
	int is_can_fd;
	volatile uint8_t *src, *dest;
	uint32_t i, val, adjusted_payload_length;
	uint32_t id_flags, id_without_flags;;

	// Cache the protocol type of the frame
	id_flags = id & CAN_ID_TYPE_MASK;
	if ((id_flags == CAN_ID_TYPE_FD_STANDARD) || (id_flags == CAN_ID_TYPE_FD_EXTENDED))
		is_can_fd = 1;
	else
		is_can_fd = 0;

	reg_write(dev, RCANFD_F_CFID(-1, ch, 0), id);

	// Set the payload size
	val = get_dlc_from_length(len, &adjusted_payload_length);
	reg_write(dev, RCANFD_F_CFPTR(-1, ch, 0), RCANFD_CFPTR_CFDLC(val));

	// Set the frame payload
	if (!is_can_fd && len > CAN_CLASSIC_FRAME_MAXIMUM_PAYLOAD_SIZE)
		return -1;
	if (len > CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE)
		return -1;

	src = data;
	dest = (uint8_t *)reg_addr(dev, RCANFD_F_CFDF(-1, ch, 0, 0));
	// Use a for loop instead of memcpy() to make sure the buffer registers are accessed one byte at a time
	// Using memcpy() triggers a data abort exception for a 7-byte CAN payload
	for (i = 0; i < len; i++)
	{
		*dest = *src;
		src++;
		dest++;
	}
	// Pad the remaining data if needed if this is a CAN-FD frame
	for (; i < adjusted_payload_length; i++)
	{
		*dest = 0;
		dest++;
	}

	// Configure the frame format
	val = 0; // This corresponds to a classic CAN frame and also initializes the register bits not changed by the CAN-FD specific code
	if (is_can_fd)
	{
		val |= 1 << 2; // Tell to transmit a FD frame
	//	if (priv->is_can_fd_bit_rate_switch_enabled)
	//	{
	//		// TODO add BRS support
	//	}
	}
	reg_write(dev, RCANFD_F_CFFDCSTS(-1, ch, 0), val);
	reg_write(dev, RCANFD_CFPCTR(-1, ch, 0), 0x000000FF);

	return 0;
}

// Inverted Empty flag -> is_data_avaiable
#define CAN_RECEIVED_DATA_FLAG(dev, ch) (!(reg_read(dev, RCANFD_RFSTS(-1, ch)) & RCANFD_RFSTS_RFEMP))

int rcar_canfd_poll_recv(const struct device *dev, int ch, uint32_t *id, uint8_t *len, uint8_t *data)
{
	int i, is_extended_id, is_can_fd;
	volatile uint8_t *src, *dest;
	int ret = 0;
	uint32_t val;

	// Do not block if no data are available
	if ( !CAN_RECEIVED_DATA_FLAG(dev, ch))
		return -1;

	// Retrieve the CAN ID
	val = reg_read(dev, RCANFD_F_RFID(-1, ch));
	if (val & RCANFD_RFID_RFIDE)
		is_extended_id = 1;
	else
		is_extended_id = 0;

	if (reg_read(dev, RCANFD_F_RFFDSTS(-1, ch)) & RCANFD_RFFDSTS_RFFDF )
		is_can_fd = 1;
	else
		is_can_fd = 0;

	*id = val | (is_extended_id <<31) | (is_can_fd << 30);

	// Retrieve the frame length
	val = RCANFD_RFPTR_RFDLC(reg_read(dev, RCANFD_F_RFPTR(-1, ch)));
	*len = get_length_from_dlc(val);
	if (!is_can_fd && *len > CAN_CLASSIC_FRAME_MAXIMUM_PAYLOAD_SIZE)
		goto Exit;
	if (*len > CAN_FD_FRAME_MAXIMUM_PAYLOAD_SIZE)
		goto Exit;

	// Retrieve the frame payload
	src = (uint8_t *)reg_addr(dev, RCANFD_F_RFDF(-1, ch, 0));
	dest = data;
	// Use a for loop instead of memcpy() to make sure the buffer registers are accessed one byte at a time
	// Using memcpy() triggers a data abort exception for a 7-byte CAN payload
	for (i = 0; i < *len; i++)
	{
		*dest = *src;
		src++;
		dest++;
	}

	ret = 0;

Exit:
	// Increment the FIFO read pointer to get access to the next received frame
	reg_write(dev, RCANFD_RFPCTR(-1, ch), 0x000000FF);

	return ret;
}

#define CANFD_NODE DT_NODELABEL(canfd)

/* “clock-names のトークン”を渡して enable するマクロ */
#define ENABLE_CPG_CLOCK(_name)                                                \
    do {                                                                   \
        const struct device *cpg = DEVICE_DT_GET(                       \
            DT_CLOCKS_CTLR_BY_NAME(CANFD_NODE, _name));             \
        struct rcar_cpg_clk clk = {                                      \
            .domain = DT_CLOCKS_CELL_BY_NAME(CANFD_NODE, _name, domain), \
            .module = DT_CLOCKS_CELL_BY_NAME(CANFD_NODE, _name, module), \
            .rate   = 0,                                             \
        };                                                              \
        int _ret;                                                       \
        if (!device_is_ready(cpg)) {                                    \
            printk("CPG device not ready for %s\n", #_name);         \
            return -ENODEV;                                        \
        }                                                               \
        _ret = clock_control_on(cpg, (clock_control_subsys_t)&clk);      \
        if (_ret) {                                                     \
            printk("Error(%s) ret=%d (domain=%u module=%u)\n",       \
                   #_name, _ret, clk.domain, clk.module);          \
            return _ret;                                            \
        }                                                               \
    } while (0)

static int rcar_canfd_enable_clocks(const struct device *dev)
{
    ARG_UNUSED(dev);

    /* 必須 */
    ENABLE_CPG_CLOCK(fck);
    ENABLE_CPG_CLOCK(canfd);

#if DT_CLOCKS_HAS_NAME(CANFD_NODE, pclk)
    ENABLE_CPG_CLOCK(pclk);
#endif

    return 0;
}


/* ===== Debug dump helpers ===== */

#ifndef CANFD_DBG
#define CANFD_DBG 1
#endif

#if CANFD_DBG
#define CANFD_PR(...) printk(__VA_ARGS__)
#else
#define CANFD_PR(...)
#endif

/* Your code already has these, but keep safe if not */
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

/* --- Gen4 per-channel FD registers (your current assumption) --- */
#ifndef DCFG_OFFSET
#define DCFG_OFFSET(n)   (0x1400U + 0x20U * (uint32_t)(n))
#endif
#ifndef FDCFG_OFFSET
#define FDCFG_OFFSET(n)  (0x1404U + 0x20U * (uint32_t)(n))  /* CFOE/FDOE live here (Gen4 style) */
#endif
#ifndef CFDCTR_OFFSET
#define CFDCTR_OFFSET(n) (0x1408U + 0x20U * (uint32_t)(n))
#endif
#ifndef CFDSTS_OFFSET
#define CFDSTS_OFFSET(n) (0x140CU + 0x20U * (uint32_t)(n))
#endif

static void rcar_canfd_dump_global(const struct device *dev, const char *tag)
{
    uint32_t gcfg  = reg_read(dev, RCANFD_GCFG);
    uint32_t gctr  = reg_read(dev, RCANFD_GCTR);
    uint32_t gsts  = reg_read(dev, RCANFD_GSTS);
    uint32_t gerfl = reg_read(dev, RCANFD_GERFL);
    uint32_t grmcfg = reg_read(dev, RCANFD_GRMCFG);
    uint32_t gaflectr = reg_read(dev, RCANFD_GAFLECTR);

    CANFD_PR("\n[CANFD][%s] GLOBAL:\n", tag);
    CANFD_PR("  GCFG=%08x GCTR=%08x GSTS=%08x GERFL=%08x GRMCFG=%08x GAFLECTR=%08x\n",
             gcfg, gctr, gsts, gerfl, grmcfg, gaflectr);
    CANFD_PR("  GSTS bits: GRAMINIT=%d GSLPSTS=%d GHLTSTS=%d GRSTSTS=%d (GNOPM mask=%08x)\n",
             !!(gsts & RCANFD_GSTS_GRAMINIT),
             !!(gsts & RCANFD_GSTS_GSLPSTS),
             !!(gsts & RCANFD_GSTS_GHLTSTS),
             !!(gsts & RCANFD_GSTS_GRSTSTS),
             (gsts & RCANFD_GSTS_GNOPM));
}

static void rcar_canfd_dump_channel(const struct device *dev, int ch, const char *tag)
{
    uint32_t ccfg  = reg_read(dev, RCANFD_CCFG(ch));
    uint32_t cctr  = reg_read(dev, RCANFD_CCTR(ch));
    uint32_t csts  = reg_read(dev, RCANFD_CSTS(ch));
    uint32_t cerfl = reg_read(dev, RCANFD_CERFL(ch));

    /* Gen4 FD regs (may be unused on some modes, but safe to read) */
    uint32_t dcfg  = reg_read(dev, DCFG_OFFSET(ch));
    uint32_t fdcfg = reg_read(dev, FDCFG_OFFSET(ch));
    uint32_t cfdctr = reg_read(dev, CFDCTR_OFFSET(ch));
    uint32_t cfdsts = reg_read(dev, CFDSTS_OFFSET(ch));

    CANFD_PR("\n[CANFD][%s] CH%d:\n", tag, ch);
    CANFD_PR("  CCFG=%08x CCTR=%08x CSTS=%08x CERFL=%08x\n", ccfg, cctr, csts, cerfl);
    CANFD_PR("  DCFG=%08x FDCFG=%08x CFDCTR=%08x CFDSTS=%08x\n", dcfg, fdcfg, cfdctr, cfdsts);

    CANFD_PR("  CSTS bits: CRSTSTS=%d HLTSTS=%d SLPSTS=%d EPSTS=%d BOSTS=%d (REC=%u TEC=%u)\n",
             !!(csts & RCANFD_CSTS_CRSTSTS),
             !!(csts & RCANFD_CSTS_HLTSTS),
             !!(csts & RCANFD_CSTS_SLPSTS),
             !!(csts & RCANFD_CSTS_EPSTS),
             !!(csts & RCANFD_CSTS_BOSTS),
             (unsigned)RCANFD_CSTS_RECCNT(csts),
             (unsigned)RCANFD_CSTS_TECCNT(csts));

    CANFD_PR("  CCTR bits: CHMDC=%u CSLPR=%d ERRD=%d CTME=%d\n",
             (unsigned)(cctr & RCANFD_CCTR_CHMDC_MASK),
             !!(cctr & RCANFD_CCTR_CSLPR),
             !!(cctr & RCANFD_CCTR_ERRD),
             !!(cctr & RCANFD_CCTR_CTME));

    CANFD_PR("  FDCFG bits: CLOE=%d FDOE=%d\n",
             !!(fdcfg & RCANFD_GEN4_FDCFG_CLOE),
             !!(fdcfg & RCANFD_GEN4_FDCFG_FDOE));
}

static inline void rcar_canfd_set_chmdc(const struct device *dev, int ch, uint32_t mode)
{
    uint32_t before_cctr = reg_read(dev, RCANFD_CCTR(ch));
    uint32_t before_csts = reg_read(dev, RCANFD_CSTS(ch));
    uint32_t after_cctr;

    after_cctr = (before_cctr & ~RCANFD_CCTR_CHMDC_MASK) |
                 (mode & RCANFD_CCTR_CHMDC_MASK);

    reg_write(dev, RCANFD_CCTR(ch), after_cctr);

    printk("CH%d set CHMDC=%u: CCTR %08x -> %08x, CSTS(before)=%08x CSTS(after)=%08x\n",
           ch,
           mode & RCANFD_CCTR_CHMDC_MASK,
           before_cctr,
           reg_read(dev, RCANFD_CCTR(ch)),
           before_csts,
           reg_read(dev, RCANFD_CSTS(ch)));
}
static int rcar_canfd_ch_transition(const struct device *dev, int ch)
{
    uint32_t cnt;

    printk("CH%d: CRESET -> CHLT -> COPM transition start\n", ch);

    /* 1. Ensure channel reset */
    rcar_canfd_set_chmdc(dev, ch, RCANFD_CCTR_CHDMC_CRESET);
    cnt = 0;
    while (!(reg_read(dev, RCANFD_CSTS(ch)) & RCANFD_CSTS_CRSTSTS)) {
        if (++cnt > 1000000) {
            printk("CH%d: timeout waiting CRSTSTS=1\n", ch);
            return -ETIMEDOUT;
        }
    }

    /* 2. Go HALT */
/*
	rcar_canfd_set_chmdc(dev, ch, RCANFD_CCTR_CHDMC_CHLT);
    cnt = 0;
    while (!(reg_read(dev, RCANFD_CSTS(ch)) & RCANFD_CSTS_HLTSTS)) {
        if (++cnt > 1000000) {
            printk("CH%d: timeout waiting HLTSTS=1\n", ch);
            return -ETIMEDOUT;
        }
    }
*/
    /* 3. Go OP */
    rcar_canfd_set_chmdc(dev, ch, RCANFD_CCTR_CHDMC_COPM);
    cnt = 0;
    while (reg_read(dev, RCANFD_CSTS(ch)) & RCANFD_CSTS_CRSTSTS) {
        if (++cnt > 1000000) {
            printk("CH%d: timeout waiting CRSTSTS=0\n", ch);
            return -ETIMEDOUT;
        }
    }

    printk("CH%d: entered OP mode\n", ch);
    return 0;
}

static inline void rcar_canfd_set_bits(const struct device *dev, uint32_t reg, uint32_t mask)
{
	uint32_t v = reg_read(dev, reg);
	reg_write(dev, reg, v | mask);
}

static inline void rcar_canfd_clear_bits(const struct device *dev, uint32_t reg, uint32_t mask)
{
	uint32_t v = reg_read(dev, reg);
	reg_write(dev, reg, v & ~mask);
}

static inline void rcar_canfd_update_bits(const struct device *dev, uint32_t reg,
					  uint32_t mask, uint32_t val)
{
	uint32_t v = reg_read(dev, reg);

	v &= ~mask;
	v |= (val & mask);
	reg_write(dev, reg, v);
}

static int rcar_canfd_wait_reg(const struct device *dev, uint32_t reg,
			       uint32_t mask, uint32_t expect,
			       uint32_t timeout_us)
{
	uint32_t i;

	for (i = 0; i < timeout_us; i++) {
		if ((reg_read(dev, reg) & mask) == expect) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int rcar_canfd_global_reset_phase(const struct device *dev)
{
    int ret;

    ret = rcar_canfd_wait_reg(dev, RCANFD_GSTS,
                              RCANFD_GSTS_GRAMINIT, 0, 500000);
    if (ret) {
        printk("global raminit timeout GSTS=%08x\n", reg_read(dev, RCANFD_GSTS));
        return ret;
    }

    rcar_canfd_clear_bits(dev, RCANFD_GCTR, RCANFD_GCTR_GSLPR);
    rcar_canfd_update_bits(dev, RCANFD_GCTR,
                           RCANFD_GCTR_GMDC_MASK,
                           RCANFD_GCTR_GMDC_GRESET);

    ret = rcar_canfd_wait_reg(dev, RCANFD_GSTS,
                              RCANFD_GSTS_GRSTSTS,
                              RCANFD_GSTS_GRSTSTS, 500000);
    if (ret) {
        printk("global reset failed GCTR=%08x GSTS=%08x\n",
               reg_read(dev, RCANFD_GCTR), reg_read(dev, RCANFD_GSTS));
        return ret;
    }

    reg_write(dev, RCANFD_GERFL, 0);
    return 0;
}

static int rcar_canfd_channel_reset_phase(const struct device *dev, uint32_t ch)
{
    int ret;

    rcar_canfd_clear_bits(dev, RCANFD_CCTR(ch), RCANFD_CCTR_CSLPR);

    rcar_canfd_update_bits(dev, RCANFD_CCTR(ch),
                           RCANFD_CCTR_CHMDC_MASK,
                           RCANFD_CCTR_CHDMC_CRESET);

    ret = rcar_canfd_wait_reg(dev, RCANFD_CSTS(ch),
                              RCANFD_CSTS_CRSTSTS,
                              RCANFD_CSTS_CRSTSTS, 500000);
    if (ret) {
        printk("channel %u reset failed CCTR=%08x CSTS=%08x\n",
               ch, reg_read(dev, RCANFD_CCTR(ch)), reg_read(dev, RCANFD_CSTS(ch)));
        return ret;
    }

    rcar_canfd_clear_bits(dev, FDCFG_OFFSET(ch),
                          RCANFD_GEN4_FDCFG_FDOE |
                          RCANFD_GEN4_FDCFG_CLOE |
                          RCANFD_FDCFG_TDCO |
                          RCANFD_FDCFG_TDCE |
                          RCANFD_FDCFG_TDCOC);

    return 0;
}

static int rcar_canfd_reset_controller_phase(const struct device *dev, uint32_t ch)
{
	int ret;

	/* Wait RAM init complete */
	ret = rcar_canfd_wait_reg(dev, RCANFD_GSTS,
				  RCANFD_GSTS_GRAMINIT, 0, 500000);
	if (ret) {
		printk("global raminit timeout GSTS=%08x\n", reg_read(dev, RCANFD_GSTS));
		return ret;
	}

	/* Global reset mode */
	rcar_canfd_clear_bits(dev, RCANFD_GCTR, RCANFD_GCTR_GSLPR);
	rcar_canfd_update_bits(dev, RCANFD_GCTR,
			       RCANFD_GCTR_GMDC_MASK,
			       RCANFD_GCTR_GMDC_GRESET);

	ret = rcar_canfd_wait_reg(dev, RCANFD_GSTS,
				  RCANFD_GSTS_GRSTSTS,
				  RCANFD_GSTS_GRSTSTS, 500000);
	if (ret) {
		printk("global reset failed GCTR=%08x GSTS=%08x\n",
		       reg_read(dev, RCANFD_GCTR), reg_read(dev, RCANFD_GSTS));
		return ret;
	}

	/* Clear global errors */
	reg_write(dev, RCANFD_GERFL, 0);

	/* Channel: clear sleep request */
	rcar_canfd_clear_bits(dev, RCANFD_CCTR(ch), RCANFD_CCTR_CSLPR);

	/* Channel: reset request */
	rcar_canfd_update_bits(dev, RCANFD_CCTR(ch),
			       RCANFD_CCTR_CHMDC_MASK,
			       RCANFD_CCTR_CHDMC_CRESET);

	ret = rcar_canfd_wait_reg(dev, RCANFD_CSTS(ch),
				  RCANFD_CSTS_CRSTSTS,
				  RCANFD_CSTS_CRSTSTS, 500000);
	if (ret) {
		printk("channel %u reset failed CCTR=%08x CSTS=%08x\n",
		       ch, reg_read(dev, RCANFD_CCTR(ch)), reg_read(dev, RCANFD_CSTS(ch)));
		return ret;
	}

	/* Gen4 FD mode: Linuxに合わせて FDOE/CLOE/TDC を一旦 clear */
	rcar_canfd_clear_bits(dev, FDCFG_OFFSET(ch),
			      RCANFD_GEN4_FDCFG_FDOE |
			      RCANFD_GEN4_FDCFG_CLOE |
			      RCANFD_FDCFG_TDCO |
			      RCANFD_FDCFG_TDCE |
			      RCANFD_FDCFG_TDCOC);

	return 0;
}

static void rcar_canfd_configure_global_phase(const struct device *dev, bool extclk)
{
    uint32_t gcfg = 0;

    gcfg |= RCANFD_GCFG_EEFE;
    gcfg |= RCANFD_GCFG_CMPOC;
    if (extclk) {
        gcfg |= RCANFD_GCFG_DCS;
    }

    reg_write(dev, RCANFD_GCFG, gcfg);
}

static void rcar_canfd_configure_channel_phase(const struct device *dev, uint32_t ch)
{
    rcar_canfd_set_bits(dev, RCANFD_CCTR(ch), RCANFD_CCTR_ERRD);
    rcar_canfd_update_bits(dev, RCANFD_CCTR(ch),
                           RCANFD_CCTR_BOM_MASK,
                           RCANFD_CCTR_BOM_BENTRY);
}

#define CAN_CLOCK (40000000)
#define CAN_COMPUTE_PRESCALER(baud_rate, tseg1, tseg2) ((CAN_CLOCK / (baud_rate*1000 * (1 + tseg1 + tseg2))) - 1)
#define CALC_BRP(baudrate, tseg1, tseg2) ((CAN_CLOCK / (baudrate * 1000 * (1 + tseg1 + tseg2))) - 1)

static inline uint32_t rcar_canfd_compute_data_bit_rate_cfg(uint32_t prop_seg, uint32_t seg1, uint32_t seg2, uint32_t sjw, uint32_t baudrate)
{
	uint32_t ntseg1, ntseg2, nsjw, nbrp;
	ntseg1 = (prop_seg + seg1 -1) << 8;
	ntseg2 = (seg2-1) << 16;
	nsjw = (sjw-1) << 24;
	nbrp = CALC_BRP(baudrate, prop_seg+seg1, seg2);

printk("dcfg: %08x\n", (ntseg1 | ntseg2 | nsjw | nbrp));
	return (ntseg1 | ntseg2 | nsjw | nbrp);
}
static inline uint32_t rcar_canfd_compute_nominal_bit_rate_cfg(uint32_t prop_seg, uint32_t seg1, uint32_t seg2, uint32_t sjw, uint32_t baudrate)
{
	uint32_t ntseg1, ntseg2, nsjw, nbrp;
	ntseg1 = (prop_seg + seg1 - 1) << 17;
	ntseg2 = (seg2-1) << 25;
	nsjw = (sjw-1) << 10;
	nbrp = CALC_BRP(baudrate, prop_seg+seg1, seg2);

printk("ncfg: %08x\n", (ntseg1 | ntseg2 | nsjw | nbrp));
	return (ntseg1 | ntseg2 | nsjw | nbrp);
}

void rcar_canfd_set_nominal_bit_rate_cfg(const struct device *dev, uint32_t ch, uint32_t prop_seg, uint32_t seg1, uint32_t seg2, uint32_t sjw, uint32_t baudrate)
{
	uint32_t cancfg;
	cancfg = rcar_canfd_compute_nominal_bit_rate_cfg(prop_seg, seg1, seg2, sjw, baudrate);
	reg_write(dev, RCANFD_CCFG(ch), cancfg);
}

void rcar_canfd_set_data_bit_rate_cfg(const struct device *dev, uint32_t ch, uint32_t prop_seg, uint32_t seg1, uint32_t seg2, uint32_t sjw, uint32_t baudrate)
{
	uint32_t cancfg;
	cancfg = rcar_canfd_compute_data_bit_rate_cfg(prop_seg, seg1, seg2, sjw, baudrate);
	reg_write(dev, DCFG_OFFSET(ch), cancfg);
        printf("dcfg: 0x%08x -> %08x\n", 0xe6660000 + DCFG_OFFSET(ch), reg_read(dev, DCFG_OFFSET(ch)));
}


static void rcar_canfd_set_bittiming_phase(const struct device *dev, uint32_t ch)
{
    const uint32_t bitrate  = 500; // kbps
    const uint32_t dbitrate = 5000; // kbps

    // setup CAN device config
    // config value is gotten from linux:
    //    ex.) ip -d link show can0
    //        tq 50 prop-seg 69 phase-seg1 70 phase-seg2 20 sjw 10 brp 2
    //        dtq 25 dprop-seg 2 dphase-seg1 3 dphase-seg2 2 dsjw 1 dbrp 1
    switch (bitrate) {
    case 1000: // 1000kbps = 1Mbps
        rcar_canfd_set_nominal_bit_rate_cfg(dev, ch, 14, 15, 10, 5, bitrate); break;
    case 500: // 500kbps
        rcar_canfd_set_nominal_bit_rate_cfg(dev, ch, 34, 35, 10, 5, bitrate); break;
    case 250: // 250kbps
        rcar_canfd_set_nominal_bit_rate_cfg(dev, ch, 69, 70, 20, 10, bitrate); break;
    case 125: // 125kbps
        rcar_canfd_set_nominal_bit_rate_cfg(dev, ch, 69, 70, 20, 10, bitrate); break;
    default: // Same as 1Mbps
        rcar_canfd_set_nominal_bit_rate_cfg(dev, ch, 14, 15, 10, 5, bitrate); break;
    }

    switch (dbitrate) {
    case 8000: // 8000kbps = 8Mbps
        rcar_canfd_set_data_bit_rate_cfg(dev, ch, 1, 1, 2, 1, dbitrate); break;
    case 5000: // 5000kbps = 5Mbps
        rcar_canfd_set_data_bit_rate_cfg(dev, ch, 2, 3, 2, 1, dbitrate); break;
    case 4000: // 4000kbps = 4Mbps
        rcar_canfd_set_data_bit_rate_cfg(dev, ch, 3, 3, 3, 1, dbitrate); break;
    case 2000: // 2000kbps = 2Mbps
        rcar_canfd_set_data_bit_rate_cfg(dev, ch, 7, 7, 5, 2, dbitrate); break;
    default: // Same as 5Mbps
        rcar_canfd_set_data_bit_rate_cfg(dev, ch, 2, 3, 2, 1, dbitrate); break;
    }
}

static void rcar_canfd_configure_afl_phase(const struct device *dev, uint32_t ch)
{
	uint32_t val;
	static uint32_t rule_entry = 0;
        uint32_t rule_entry_index = rule_entry % 16;
	uint8_t n, w, wp;
        static bool first_call_flag = false;

	/* AFL write enable */
	reg_write(dev, RCANFD_GAFLECTR, RCANFD_GAFLECTR_AFLDAE);

	/* 全チャネルのルール数を一旦 0 */
        if (!first_call_flag) {
            reg_write(dev, RCANFD_GAFLCFG(0), 0);
            reg_write(dev, RCANFD_GAFLCFG(1), 0);
            reg_write(dev, RCANFD_GAFLCFG(2), 0);
            reg_write(dev, RCANFD_GAFLCFG(3), 0);
            first_call_flag = true;
        }
	/* one rule for channel ch */
	n = ch;
	w = n / 2;
	wp = ((1 - n) + (w * 2));

        printf("GAFLCFG%d: %08x -> ", w, reg_read(dev, RCANFD_GAFLCFG(w)));
	//reg_write(dev, RCANFD_GAFLCFG(w), 1 << (16 * wp));
        rcar_canfd_set_bits(dev, RCANFD_GAFLCFG(w), BIT(16 * wp));
	reg_write(dev, RCANFD_GAFLID(RCANFD_GEN4_GAFL_OFFSET, rule_entry_index), 0);
	reg_write(dev, RCANFD_GAFLM(RCANFD_GEN4_GAFL_OFFSET, rule_entry_index), 0);
	reg_write(dev, RCANFD_GAFLP0(RCANFD_GEN4_GAFL_OFFSET, rule_entry_index), 0);
	rcar_canfd_set_bits(dev, RCANFD_GAFLP1(RCANFD_GEN4_GAFL_OFFSET, rule_entry_index), RCANFD_GAFLP1_GAFLFDP(n));
        printf("%08x (rules=%d)\n", reg_read(dev, RCANFD_GAFLCFG(w)), rule_entry);
	rule_entry += 1;

	/* AFL write disable */
	reg_write(dev, RCANFD_GAFLECTR, 0);
}

static void rcar_canfd_configure_rx_tx_phase(const struct device *dev, uint32_t ch)
{
	uint32_t val;

	/* Disable RX message buffers */
	reg_write(dev, RCANFD_RMNB, 0);

	/* RX FIFO config: disabled yet */
	val = (0x06 << 8);
	val |= 0x07 << 4; // For CAN-FD configuration
	reg_write(dev, RCANFD_RFCC(-1, ch), val);

	/* TX common FIFO config: disabled yet */
	val = (0x06 << 21) | (0x01 << 8); // Gen4 
	val |= 0x07 << 4; // For CAN-FD configuration
	reg_write(dev, RCANFD_CFCC(-1, ch, 0), val);

	/* FD common FIFO control/status clear */
	reg_write(dev, RCANFD_F_CFFDCSTS(-1, ch, 0), 0);
}

static int rcar_canfd_enter_global_op_phase(const struct device *dev)
{
	int ret;

	rcar_canfd_update_bits(dev, RCANFD_GCTR,
			       RCANFD_GCTR_GMDC_MASK,
			       RCANFD_GCTR_GMDC_GOPM);

	ret = rcar_canfd_wait_reg(dev, RCANFD_GSTS,
				  RCANFD_GSTS_GNOPM, 0, 500000);
	if (ret) {
		printk("global operational mode failed GCTR=%08x GSTS=%08x\n",
		       reg_read(dev, RCANFD_GCTR), reg_read(dev, RCANFD_GSTS));
		return ret;
	}

	return 0;
}

static int rcar_canfd_start_channel_phase(const struct device *dev, uint32_t ch)
{
	int ret;

	printk("[start ch%u] before: CCTR=%08x CSTS=%08x\n",
	       ch, reg_read(dev, RCANFD_CCTR(ch)), reg_read(dev, RCANFD_CSTS(ch)));

	/* Linux start(): CHMDC = COPM */
	rcar_canfd_update_bits(dev, RCANFD_CCTR(ch),
			       RCANFD_CCTR_CHMDC_MASK,
			       RCANFD_CCTR_CHDMC_COPM);

	printk("[start ch%u] COPM req: CCTR=%08x CSTS=%08x\n",
	       ch, reg_read(dev, RCANFD_CCTR(ch)), reg_read(dev, RCANFD_CSTS(ch)));

	/* Linuxは COMSTS を待つ */
	ret = rcar_canfd_wait_reg(dev, RCANFD_CSTS(ch),
				  RCANFD_CSTS_COMSTS,
				  RCANFD_CSTS_COMSTS, 500000);
	if (ret) {
		printk("channel %u communication state failed CCTR=%08x CSTS=%08x CERFL=%08x CFDSTS=%08x\n",
		       ch,
		       reg_read(dev, RCANFD_CCTR(ch)),
		       reg_read(dev, RCANFD_CSTS(ch)),
		       reg_read(dev, RCANFD_CERFL(ch)),
		       reg_read(dev, CFDSTS_OFFSET(ch)));
		return ret;
	}

	/* Enable RX FIFO */
	rcar_canfd_set_bits(dev, RCANFD_RFCC(-1, ch), RCANFD_RFCC_RFE);

	/* Enable TX FIFO */
	rcar_canfd_set_bits(dev, RCANFD_CFCC(-1, ch, 0), RCANFD_CFCC_CFE);

	printk("[start ch%u] entered communication state: CCTR=%08x CSTS=%08x\n",
	       ch, reg_read(dev, RCANFD_CCTR(ch)), reg_read(dev, RCANFD_CSTS(ch)));

	return 0;
}

static int rcar_canfd_init(const struct device *dev)
{
    const struct rcar_canfd_config *config = dev->config;
    uint32_t channel_start = 3;
    uint32_t channel_end = 5;   /* 3,4 */
    int ret;
    bool extclk = true;
    uint32_t ch;

    printk("rcar_canfd_init: start\n");

    DEVICE_MMIO_NAMED_MAP(dev, mmio, 0);
    printk("mmio=%p\n", (void *)DEVICE_MMIO_NAMED_GET(dev, mmio));

#ifndef CONFIG_XEN
    ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
    if (ret < 0) {
        printk("pinctrl apply failed: %d\n", ret);
        return ret;
    }
#endif

    ret = rcar_canfd_enable_clocks(dev);
    if (ret) {
        printk("Error: rcar_canfd_enable_clocks(ret = %d)\n", ret);
        return ret;
    }

    /* 1. global reset once */
    ret = rcar_canfd_global_reset_phase(dev);
    if (ret) {
        printk("global_reset_phase failed: %d\n", ret);
        return ret;
    }

    /* 2. all target channels -> reset */
    for (ch = channel_start; ch < channel_end; ch++) {
        ret = rcar_canfd_channel_reset_phase(dev, ch);
        if (ret) {
            printk("channel_reset_phase ch%u failed: %d\n", ch, ret);
            return ret;
        }
    }

    /* 3. global controller config once */
	rcar_canfd_configure_global_phase(dev, extclk);
	for (ch = channel_start; ch < channel_end; ch++) {
		rcar_canfd_configure_channel_phase(dev, ch);
	}

    /* 4. per-channel bittiming */
    for (ch = channel_start; ch < channel_end; ch++) {
        rcar_canfd_set_bittiming_phase(dev, ch);
    }

    /* 5. AFL/FIFO per-channel */
    for (ch = channel_start; ch < channel_end; ch++) {
        rcar_canfd_configure_rx_tx_phase(dev, ch);
        rcar_canfd_configure_afl_phase(dev, ch);
    }

printk("GAFLCFG0=%08x GAFLCFG1=%08x GAFLCFG2=%08x GAFLCFG3=%08x\n",
       reg_read(dev, RCANFD_GAFLCFG(0)),
       reg_read(dev, RCANFD_GAFLCFG(1)),
       reg_read(dev, RCANFD_GAFLCFG(2)),
       reg_read(dev, RCANFD_GAFLCFG(3)));

printk("GAFL0: ID=%08x M=%08x P0=%08x P1=%08x\n",
       reg_read(dev, RCANFD_GAFLID(RCANFD_GEN4_GAFL_OFFSET, 0)),
       reg_read(dev, RCANFD_GAFLM(RCANFD_GEN4_GAFL_OFFSET, 0)),
       reg_read(dev, RCANFD_GAFLP0(RCANFD_GEN4_GAFL_OFFSET, 0)),
       reg_read(dev, RCANFD_GAFLP1(RCANFD_GEN4_GAFL_OFFSET, 0)));
	
    /* 6. global operational once */
    ret = rcar_canfd_enter_global_op_phase(dev);
    if (ret) {
        printk("enter_global_op_phase failed: %d\n", ret);
        return ret;
    }

    /* 7. all channels start */
    for (ch = channel_start; ch < channel_end; ch++) {
        ret = rcar_canfd_start_channel_phase(dev, ch);
        if (ret) {
            printk("start_channel_phase ch%u failed: %d\n", ch, ret);
            return ret;
        }
    }

    return 0;
}

#define RCAR_CANFD_INIT(inst)                                      \
    static struct rcar_canfd_data rcar_canfd_data_##inst;          \
    PINCTRL_DT_INST_DEFINE(inst);                                   \
    static const struct rcar_canfd_config rcar_canfd_cfg_##inst = {\
        DEVICE_MMIO_NAMED_ROM_INIT(mmio, DT_DRV_INST(inst)),       \
        .pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),            \
    };                                                             \
    DEVICE_DT_INST_DEFINE(inst, rcar_canfd_init, NULL,             \
        &rcar_canfd_data_##inst, &rcar_canfd_cfg_##inst,           \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(RCAR_CANFD_INIT)

