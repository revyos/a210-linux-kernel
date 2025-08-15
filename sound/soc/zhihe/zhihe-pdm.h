#ifndef _ZHIHE_PDM_H
#define _ZHIHE_PDM_H

#include <linux/io.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#include "zhihe-pcm.h"
#include <linux/spinlock.h>

#define	CP_PDM_BADDR		0
#define CP_PERI1_SYS_BADDR 	0x1000000

#define CP_VAD_CTRL		(CP_PDM_BADDR+0X00)
#define CP_ZCR_TH_CTRL		(CP_PDM_BADDR+0X04)
#define CP_STE_HTH_CTRL		(CP_PDM_BADDR+0X08)
#define CP_STE_LTH_CTRL		(CP_PDM_BADDR+0X0C)
#define	CP_VAD_INTR_MFLAG	(CP_PDM_BADDR+0X10)
#define	CP_VAD_INTR_CLR		(CP_PDM_BADDR+0X14)
#define	CP_VAD_INTR_FLAG	(CP_PDM_BADDR+0X18)
#define	CP_VAD_INTR_MASK	(CP_PDM_BADDR+0X1C)
#define CP_DAI_EN		(CP_PDM_BADDR+0X20)
#define CP_DAI_CTRL		(CP_PDM_BADDR+0X24)
#define CP_FIFO_CNT		(CP_PDM_BADDR+0X28)
#define CP_FIFO_TH_CTRL		(CP_PDM_BADDR+0X2C)
#define CP_FIFO_INTR_MFLAG	(CP_PDM_BADDR+0X30)
#define CP_FIFO_INTR_CLR	(CP_PDM_BADDR+0X34)
#define CP_FIFO_INTR_FLAG	(CP_PDM_BADDR+0X38)
#define CP_FIFO_INTR_MASK	(CP_PDM_BADDR+0X3C)
#define CP_CIC_L_ST_OFFSET	(CP_PDM_BADDR+0X40)
#define CP_CIC_L_LP_OFFSET	(CP_PDM_BADDR+0X44)
#define CP_CIC_R_ST_OFFSET	(CP_PDM_BADDR+0X48)
#define CP_CIC_R_LP_OFFSET	(CP_PDM_BADDR+0X4C)

#define CP_FIFO0_L 		(CP_PDM_BADDR+0X50)
#define CP_FIFO0_R 		(CP_PDM_BADDR+0X54)
#define CP_FIFO1_L		(CP_PDM_BADDR+0X58)
#define CP_FIFO1_R		(CP_PDM_BADDR+0X5C)
#define CP_FIFO2_L		(CP_PDM_BADDR+0X60)
#define CP_FIFO2_R		(CP_PDM_BADDR+0X64)
#define CP_FIFO3_L		(CP_PDM_BADDR+0X68)
#define CP_FIFO3_R		(CP_PDM_BADDR+0X6C)
#define CP_VAD_IP_ID		(CP_PDM_BADDR+0X74)

#define VADCTRL_PDM_EN_MSK		(0x3)
#define VADCTRL_VAD_EN_SEL		(0x1U)
#define VADCTRL_DATA_TRANS_EN_SEL	(0x2U)

#define DAI_EN_MSK 			(0x1)
#define DAI_EN_SEL 			(0x1)

#define PDM_STATE_IDLE          	0
#define PDM_STATE_RUNNING	    	1
#define PDM_DMA_MAXBURST		8

struct zhihe_pdm_priv {
	void __iomem *base;
	phys_addr_t phys;

	void __iomem            *regs;
	struct regmap *regmap;
	struct regmap *audio_pin_regmap;
	struct regmap *audio_cpr_regmap;
	struct clk *clk;
	struct snd_dmaengine_dai_dma_data dma_params_rx;

	u32 fmt;
	unsigned int dai_fmt;
	u32 dma_maxburst;
	unsigned int cfg_off;
	u32 mclk_keepon;

	struct device *dev;
	char name[16];
	int chan_num:8;
	unsigned int clk_master:1;

	u32 suspend_pdmen;
	u32 suspend_ctl;
	u32 suspend_fifo_th;
	u32 suspend_fifl_dl;
	u32 suspend_dma_en;
	u32 suspend_dma_th;
	u32 suspend_imr;

	u32 state;
};

#endif /* _ZHIHE_PDM_H */
