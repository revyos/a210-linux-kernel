/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#ifndef __P100_PD_H
#define __P100_PD_H

#define P100_PD_NAME_SIZE 20
#define P100_PD_STATE_NAME_SIZE 10

#define BPC_SW_MODEL 1
#define BPC_HW_MODEL 0
#define PCU_REG_TRIGGER 1
#define PCU_R2P_TRIGGER 0

#define  AON_AON_I2C0_BADDR                       0x3084A000

#define  IOPMP_DEVICES_MAX_COUNT    16

enum {
	P100_PD_GPU = 1,
	P100_PD_NPU_WRAPPER,
	P100_PD_NPU_IP,
	P100_PD_D2D,
	P100_PD_PCIE0,
	P100_PD_PCIE1,
	P100_PD_SATA,
	P100_PD_USB,
	P100_PD_VI_WRAP,
	P100_PD_VI_ISP,
	P100_PD_VO,
	P100_PD_VP_WRAP,
	P100_PD_VENC,
	P100_PD_VDEC,
	P100_PD_TOP,
	P100_PD_PERI0,
	P100_PD_PERI1,
	P100_PD_PERI2,
	P100_PD_PERI3,
	P100_PD_TEE,
	P100_POWER_DOMAINS_MAX,
};

enum {
	PMIC_CTRL,
	VP_PCA,
	VP_WRAP_BPC,
	VP_WRAP_PCU,
	VP_VENC_BPC,
	VP_VENC_PCU,
	VP_VDEC_BPC,
	VP_VDEC_PCU,
	VP_R2P,
	GPU_PCA,
	GPU_BPC,
	GPU_PCU,
	NPU_PCA,
	NPU_WRAP_BPC,
	NPU_WRAP_PCU,
	NPU_IP_BPC,
	NPU_IP_PCU,
	PCIE0_BPC,
	PCIE0_PCU,
	PCIE1_BPC,
	PCIE1_PCU,
	SATA_BPC,
	SATA_PCU,
	USB_BPC,
	USB_PCU,
	VI_R2P,
	VI_WRAP_BPC,
	VI_WRAP_PCU,
	VI_ISP_BPC,
	VI_ISP_PCU,
	VO_BPC,
	VO_PCU,
	P100_POWER_DOMAIN_REGS_MAX,
};

struct p100_pd_range {
	char *name;
	u32 index;
};

/* represent power domains info at soc level */
struct p100_pd_soc {
	const struct p100_pd_range *pd_ranges;
	u8 num_ranges;
	struct p100_pm_domain *domains[P100_POWER_DOMAINS_MAX];
	u8 num_domains;
	struct device *dev;
	void __iomem *base[P100_POWER_DOMAIN_REGS_MAX];
};

/* represent a single power domain */
struct p100_pm_domain {
	struct generic_pm_domain pd;
	u16 index;
	struct p100_pd_soc *soc;
	struct reset_control *reset;
	struct clk_bulk_data *clks;
	u32 num_clks;
	u32 device_count;
	u32 device_ids[IOPMP_DEVICES_MAX_COUNT];
};

typedef enum {
    OFF                 = 0x0,
    MEM_SD              = 0x1,
    MEM_RET             = 0x2,
    MEM_SD_ONLY         = 0x9,
    MEM_DSLP            = 0xa,
    MEM_SLP             = 0xb,
    CG                  = 0xf,
    ON                  = 0x1f,
    WAIT_OFF            = 0x100,
    WAIT_MEM_SD         = 0x101,
    WAIT_MEM_RET        = 0x102,
    WAIT_MEM_SD_ONLY    = 0x109,
    WAIT_MEM_DSLP       = 0x10a,
    WAIT_MEM_SLP        = 0x10b,
    WAIT_CG             = 0x10f,
    WAIT_ON             = 0x11f
} power_mode;

#endif
