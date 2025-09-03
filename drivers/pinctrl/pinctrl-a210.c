// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl driver for the ZhiHe P100 SoC
 *
 * Copyright (C) 2023 Emil Renner Berthing <emil.renner.berthing@canonical.com>
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/clk.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

#define P100_PADCFG_IE    BIT(9)
#define P100_PADCFG_SL    BIT(8)
#define P100_PADCFG_ST    BIT(7)
#define P100_PADCFG_SPU   BIT(6)
#define P100_PADCFG_PS    BIT(5)
#define P100_PADCFG_PE    BIT(4)
#define P100_PADCFG_BIAS  (P100_PADCFG_SPU | P100_PADCFG_PS | P100_PADCFG_PE)
#define P100_PADCFG_DS    GENMASK(3, 0)

#define P100_PULL_DOWN_OHM	  44000 /* typ. 44kOhm */
#define P100_PULL_UP_OHM	  48000 /* typ. 48kOhm */
#define P100_PULL_STRONG_OHM  2100  /* typ. 2.1kOhm */

#define P100_PAD_NO_PADCFG    BIT(63)
#define P100_PAD_MUXDATA      GENMASK(55, 0)

#ifdef CONFIG_PM_SLEEP
#define MAX_CFG_REG_NUMS            25
#define MAX_MUX_REG_NUMS            7
#define P100_AON_CFG_REG_NUMS       22
#define P100_AON_MUX_REG_NUMS       6
#define P100_PADCTRL0_CFG_REG_NUMS  25
#define P100_PADCTRL0_MUX_REG_NUMS  7
#define P100_PADCTRL1_CFG_REG_NUMS  22
#define P100_PADCTRL1_MUX_REG_NUMS  6
#define P100_PADCTRL2_CFG_REG_NUMS  6
#define P100_PADCTRL2_MUX_REG_NUMS  1
#define P100_PM_PAD_CFG(idx)        (pp->base + pp->offset_cfg + idx * 4)
#define P100_PM_PAD_MUX(idx)        (pp->base + pp->offset_mux + idx * 4)
#endif

struct p100_pinctrl;

enum p100_pinctrl_type {
	P100_PADCTRL_AON,
	P100_PADCTRL_0,
	P100_PADCTRL_1,
	P100_PADCTRL_2,
};

struct p100_pad_group {
	const char *name;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int offset_mux;
	unsigned int mask_mux;
	unsigned int offset_cfg;
	unsigned int mask_cfg;
	enum p100_pinctrl_type type;
};

struct p100_pinctrl {
	struct pinctrl_desc desc;
	struct mutex mutex;	/* serialize adding functions */
	raw_spinlock_t lock;	/* serialize register access */
	void __iomem *base;
	struct clk	*clk;
	unsigned int offset_mux;
	unsigned int mask_mux;
	unsigned int offset_cfg;
	unsigned int mask_cfg;
	enum p100_pinctrl_type type;
	struct pinctrl_dev *pctl;
#ifdef CONFIG_PM_SLEEP
	unsigned int cfg_bak[MAX_CFG_REG_NUMS];
	unsigned int mux_bak[MAX_MUX_REG_NUMS];
#endif
};

static const unsigned int m1  = 0x55555555; // 01010101010101010101010101010101
static const unsigned int m2  = 0x33333333; // 00110011001100110011001100110011
static const unsigned int m4  = 0x0f0f0f0f; // 00001111000011110000111100001111
static const unsigned int m8  = 0x00ff00ff; // 00000000111111110000000011111111
static const unsigned int m16 = 0x0000ffff; // 00000000000000001111111111111111

static int __popcount(unsigned int x)
{
	x = (x & m1) + ((x >> 1) & m1);
	x = (x & m2) + ((x >> 2) & m2);
	x = (x & m4) + ((x >> 4) & m4);
	x = (x & m8) + ((x >> 8) & m8);
	x = (x & m16) + ((x >> 16) & m16);
	return x;
}

static void __iomem *p100_padcfg(struct p100_pinctrl *pp,
				   unsigned int pin)
{
	int width = __popcount(pp->mask_cfg);

	return pp->base + pp->offset_cfg + 4 * (pin * width / 32);
}

static unsigned int p100_padcfg_shift(struct p100_pinctrl *pp,
					unsigned int pin)
{
	int width = __popcount(pp->mask_cfg);

	return width * (pin & (32 / width - 1));
}

static void __iomem *p100_muxcfg(struct p100_pinctrl *pp,
					unsigned int pin)
{
	int width = __popcount(pp->mask_mux);

	return pp->base + pp->offset_mux + 4 * (pin * width / 32);
}

static unsigned int p100_muxcfg_shift(struct p100_pinctrl *pp,
					unsigned int pin)
{
	int width = __popcount(pp->mask_mux);

	return width * (pin & (32 / width - 1));
}

enum p100_muxtype {
	P100_MUX_____,
	P100_MUX_AOUART,
	P100_MUX_AOUART_IR,
	P100_MUX_AOGPIO0,
	P100_MUX_AOGPIO1,
	P100_MUX_AOI2C0,
	P100_MUX_AOI2C1,
	P100_MUX_AOSPI,
	P100_MUX_JTAG,
	P100_MUX_RST,
	P100_MUX_POR,
	P100_MUX_PMIC,
	P100_MUX_DVS,
	P100_MUX_UART0,
	P100_MUX_UART1,
	P100_MUX_UART2,
	P100_MUX_UART3,
	P100_MUX_UART4,
	P100_MUX_UART5,
	P100_MUX_UART6,
	P100_MUX_UART7,
	P100_MUX_UART8,
	P100_MUX_UART9,
	P100_MUX_UART0_IR,
	P100_MUX_UART1_IR,
	P100_MUX_UART2_IR,
	P100_MUX_UART3_IR,
	P100_MUX_UART4_IR,
	P100_MUX_UART5_IR,
	P100_MUX_UART6_IR,
	P100_MUX_UART7_IR,
	P100_MUX_UART8_IR,
	P100_MUX_UART9_IR,
	P100_MUX_GPIO0,
	P100_MUX_GPIO1,
	P100_MUX_GPIO2,
	P100_MUX_GPIO3,
	P100_MUX_GPIO4,
	P100_MUX_GMAC0,
	P100_MUX_GMAC1,
	P100_MUX_SPI0,
	P100_MUX_SPI1,
	P100_MUX_QSPI0,
	P100_MUX_QSPI1,
	P100_MUX_PWM0,
	P100_MUX_PWM1,
	P100_MUX_PWM2,
	P100_MUX_CAN0,
	P100_MUX_CAN1,
	P100_MUX_CAN2,
	P100_MUX_I2S0,
	P100_MUX_I2S1,
	P100_MUX_I2S2,
	P100_MUX_I2S3,
	P100_MUX_I2C0,
	P100_MUX_I2C1,
	P100_MUX_I2C2,
	P100_MUX_I2C3,
	P100_MUX_I2C4,
	P100_MUX_I2C5,
	P100_MUX_I2C6,
	P100_MUX_I2C7,
	P100_MUX_I2C7_SMB,
	P100_MUX_SDIO,
	P100_MUX_ISP0,
	P100_MUX_DPTX,
	P100_MUX_USB31,
	P100_MUX_SATA_P0,
	P100_MUX_SATA_P1,
	P100_MUX_HDMI,
	P100_MUX_ZMAC,
	P100_MUX_BSEL,
	P100_MUX_DBGM,
	P100_MUX_CDBG,
	P100_MUX_PDM,
	P100_MUX_TDM,
	P100_MUX_PCIE_X1,
	P100_MUX_PCIE_X4,
	P100_MUX_SEN_VCLK,
	P100_MUX_MAX = 127,
};

static const char *const p100_muxtype_string[] = {
	[P100_MUX_AOUART]    = "aouart",
	[P100_MUX_AOUART_IR] = "aouart_ir",
	[P100_MUX_AOGPIO0]   = "aogpio0",
	[P100_MUX_AOGPIO1]   = "aogpio1",
	[P100_MUX_AOI2C0]    = "aoi2c0",
	[P100_MUX_AOI2C1]    = "aoi2c1",
	[P100_MUX_AOSPI]     = "aospi",
	[P100_MUX_JTAG]      = "jtag",
	[P100_MUX_RST]       = "reset",
	[P100_MUX_POR]       = "por",
	[P100_MUX_PMIC]      = "pmic",
	[P100_MUX_DVS]       = "dvs",
	[P100_MUX_UART0]     = "uart0",
	[P100_MUX_UART1]     = "uart1",
	[P100_MUX_UART2]     = "uart2",
	[P100_MUX_UART3]     = "uart3",
	[P100_MUX_UART4]     = "uart4",
	[P100_MUX_UART5]     = "uart5",
	[P100_MUX_UART6]     = "uart6",
	[P100_MUX_UART7]     = "uart7",
	[P100_MUX_UART8]     = "uart8",
	[P100_MUX_UART9]     = "uart9",
	[P100_MUX_UART0_IR]  = "uart0_ir",
	[P100_MUX_UART1_IR]  = "uart1_ir",
	[P100_MUX_UART2_IR]  = "uart2_ir",
	[P100_MUX_UART3_IR]  = "uart3_ir",
	[P100_MUX_UART4_IR]  = "uart4_ir",
	[P100_MUX_UART5_IR]  = "uart5_ir",
	[P100_MUX_UART6_IR]  = "uart6_ir",
	[P100_MUX_UART7_IR]  = "uart7_ir",
	[P100_MUX_UART8_IR]  = "uart8_ir",
	[P100_MUX_UART9_IR]  = "uart9_ir",
	[P100_MUX_GPIO0]     = "gpio0",
	[P100_MUX_GPIO1]     = "gpio1",
	[P100_MUX_GPIO2]     = "gpio2",
	[P100_MUX_GPIO3]     = "gpio3",
	[P100_MUX_GPIO4]     = "gpio4",
	[P100_MUX_GMAC0]     = "gmac0",
	[P100_MUX_GMAC1]     = "gmac1",
	[P100_MUX_SPI0]      = "spi0",
	[P100_MUX_SPI1]      = "spi1",
	[P100_MUX_QSPI0]     = "qspi0",
	[P100_MUX_QSPI1]     = "qspi1",
	[P100_MUX_PWM0]      = "pwm0",
	[P100_MUX_PWM1]      = "pwm1",
	[P100_MUX_PWM2]      = "pwm2",
	[P100_MUX_CAN0]      = "can0",
	[P100_MUX_CAN1]      = "can1",
	[P100_MUX_CAN2]      = "can2",
	[P100_MUX_I2S0]      = "i2s0",
	[P100_MUX_I2S1]      = "i2s1",
	[P100_MUX_I2S2]      = "i2s2",
	[P100_MUX_I2S3]      = "i2s3",
	[P100_MUX_I2C0]      = "i2c0",
	[P100_MUX_I2C1]      = "i2c1",
	[P100_MUX_I2C2]      = "i2c2",
	[P100_MUX_I2C3]      = "i2c3",
	[P100_MUX_I2C4]      = "i2c4",
	[P100_MUX_I2C5]      = "i2c5",
	[P100_MUX_I2C6]      = "i2c6",
	[P100_MUX_I2C7]      = "i2c7",
	[P100_MUX_I2C7_SMB]  = "i2c7_smb",
	[P100_MUX_SDIO]      = "sdio",
	[P100_MUX_ISP0]      = "isp0",
	[P100_MUX_DPTX]      = "dptx",
	[P100_MUX_USB31]     = "usb31",
	[P100_MUX_SATA_P0]   = "sata_p0",
	[P100_MUX_SATA_P1]   = "sata_p1",
	[P100_MUX_HDMI]      = "hdmi",
	[P100_MUX_ZMAC]      = "zmac",
	[P100_MUX_BSEL]      = "bsel",
	[P100_MUX_DBGM]      = "dbgm",
	[P100_MUX_CDBG]      = "cdbg",
	[P100_MUX_PDM]       = "pdm",
	[P100_MUX_TDM]       = "tdm",
	[P100_MUX_PCIE_X1]   = "pcie_x1",
	[P100_MUX_PCIE_X4]   = "pcie_x4",
	[P100_MUX_SEN_VCLK]  = "sen_vclk",
};

static enum p100_muxtype p100_muxtype_get(const char *str)
{
	enum p100_muxtype mt;

	for (mt = P100_MUX_AOUART; mt < ARRAY_SIZE(p100_muxtype_string); mt++) {
		if (!strcmp(str, p100_muxtype_string[mt]))
			return mt;
	}
	return P100_MUX_____;
}

#define P100_PAD(_nr, _name, m0, m1, m2, m3, m4, m5, m6, m7, _flags)            \
    { .number = _nr, .name = #_name, .drv_data = (void *)((_flags)            | \
        (P100_MUX_##m0 <<  0) | (P100_MUX_##m1 <<  7) | (P100_MUX_##m2 << 14) | \
        (P100_MUX_##m3 << 21) | ((uintptr_t)P100_MUX_##m4 << 28)              | \
        ((uintptr_t)P100_MUX_##m5 << 35) | ((uintptr_t)P100_MUX_##m6 << 42)   | \
        ((uintptr_t)P100_MUX_##m7 << 49)) }

static const struct pinctrl_pin_desc p100_group0_pins[] = {
	P100_PAD(0,  OSC_CLK_IN,   ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(1,  OSC_CLK_OUT,  ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(2,  RST_N_IN,     ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(3,  RTC_CLK_IN,   ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(4,  RTC_CLK_OUT,  ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	/* skip number 5 so we can calculate register offsets and shifts from the pin number */
	P100_PAD(6,  TEST_MODE,    ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(7,  POR_SEL,      ____,    ____,      ____, ____,    ____, ____, ____, ____, P100_PAD_NO_PADCFG),
	P100_PAD(8,  RST_N_OUT,    RST,     ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(9,  BOOT_SEL0,    BSEL,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(10, BOOT_SEL1,    BSEL,    AOUART,    ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(11, DEBUG_MODE,   DBGM,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(12, AOUART_TXD,   AOUART,  AOUART_IR, ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(13, AOUART_RXD,   AOUART,  AOUART_IR, ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(14, AOI2C0_SCL,   AOI2C0,  AOSPI,     ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(15, AOI2C0_SDA,   AOI2C0,  AOSPI,     ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(16, AOI2C1_SCL,   AOI2C1,  AOSPI,     ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(17, AOI2C1_SDA,   AOI2C1,  AOSPI,     ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(18, CPU_JTG_TCLK, JTAG,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(19, CPU_JTG_TMS,  JTAG,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(20, CPU_JTG_TDI,  JTAG,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(21, CPU_JTG_TDO,  JTAG,    POR,       ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(22, CPU_JTG_TRST, JTAG,    ____,      ____, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(23, AOGPIO0_21,   AOGPIO0, AOSPI,     PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(24, AOGPIO0_22,   AOGPIO0, AOSPI,     PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(25, AOGPIO0_23,   BSEL,    AOSPI,     PMIC, AOGPIO0, ____, ____, ____, ____, 0),
	P100_PAD(26, AOGPIO0_24,   AOGPIO0, AOSPI,     PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(27, AOGPIO0_25,   AOGPIO0, AOSPI,     PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(28, AOGPIO0_26,   AOGPIO0, AOI2C1,    PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(29, AOGPIO0_27,   AOGPIO0, AOI2C1,    PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(30, AOGPIO0_28,   AOGPIO0, AOI2C1,    PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(31, AOGPIO0_29,   AOGPIO0, AOI2C1,    PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(32, AOGPIO0_30,   AOGPIO0, AOI2C1,    PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(33, AOGPIO0_31,   AOGPIO0, AOUART,    PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(34, AOGPIO1_0,    AOGPIO1, AOUART,    PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(35, AOGPIO1_1,    AOGPIO1, ____,      PMIC, ____,    ____, ____, ____, ____, 0),
	P100_PAD(36, AOGPIO1_2,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(37, AOGPIO1_3,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(38, AOGPIO1_4,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(39, AOGPIO1_5,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(40, AOGPIO1_6,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(41, AOGPIO1_7,    AOGPIO1, ____,      PMIC, DVS,     ____, ____, ____, ____, 0),
	P100_PAD(42, AOGPIO1_8,    PCIE_X4, ____,      PMIC, AOGPIO1, ____, ____, ____, ____, 0),
	P100_PAD(43, AOGPIO1_9,    AOGPIO1, ____,      PMIC, ____,    ____, ____, ____, ____, 0),
};

static const struct pinctrl_pin_desc p100_group1_pins[] = {
	P100_PAD(0,  GPIO0_0,           GPIO0, GMAC0, ____,     ____,     ____,  ____,    ZMAC,  ____,     0),
	P100_PAD(1,  GPIO0_1,           GPIO0, GMAC0, ____,     ____,     ____,  ____,    ZMAC,  ____,     0),
	P100_PAD(2,  GPIO0_2,           GPIO0, GMAC0, ____,     ____,     ____,  ____,    ZMAC,  ____,     0),
	P100_PAD(3,  GPIO0_3,           GPIO0, GMAC0, UART3,    UART3_IR, ____,  SATA_P0, ZMAC,  ____,     0),
	P100_PAD(4,  GPIO0_4,           GPIO0, GMAC0, UART3,    UART3_IR, ____,  SATA_P1, ZMAC,  ____,     0),
	P100_PAD(5,  GPIO0_5,           GPIO0, GMAC0, UART3,    ____,     ____,  SATA_P1, ZMAC,  ____,     0),
	P100_PAD(6,  GPIO0_6,           GPIO0, GMAC0, UART3,    ____,     ____,  SATA_P1, ZMAC,  ____,     0),
	P100_PAD(7,  GPIO0_7,           GPIO0, GMAC0, ____,     ____,     ____,  SATA_P1, ZMAC,  ____,     0),
	P100_PAD(8,  GPIO0_8,           GPIO0, GMAC0, I2C0,     ____,     ____,  SATA_P1, ZMAC,  ____,     0),
	P100_PAD(9,  GPIO0_9,           GPIO0, GMAC0, I2C0,     ____,     ____,  PCIE_X4, ZMAC,  ____,     0),
	P100_PAD(10, GPIO0_10,          GPIO0, GMAC0, I2C1,     ____,     ____,  PCIE_X4, ZMAC,  ____,     0),
	P100_PAD(11, GPIO0_11,          GPIO0, GMAC0, I2C1,     ____,     ____,  PCIE_X4, ZMAC,  ____,     0),
	P100_PAD(12, GPIO0_12,          GPIO0, GMAC0, ____,     ____,     ____,  PCIE_X4, ZMAC,  ____,     0),
	P100_PAD(13, GPIO0_13,          GPIO0, GMAC0, ____,     ____,     ____,  PWM0,    ZMAC,  ____,     0),
	P100_PAD(14, GPIO0_14,          GPIO0, UART0, UART0_IR, ____,     SATA_P1, I2S0,  ____,  ____,     0),
	P100_PAD(15, GPIO0_15,          GPIO0, UART0, UART0_IR, QSPI0,    SATA_P1, I2S0,  ____,  ____,     0),
	P100_PAD(16, GPIO0_16,          GPIO0, UART0, UART1,    UART1_IR, SATA_P1, I2S0,  ZMAC,  ____,     0),
	P100_PAD(17, GPIO0_17,          GPIO0, UART0, UART1,    UART1_IR, SATA_P1, I2S0,  ZMAC,  ____,     0),
	P100_PAD(18, GPIO0_18,          GPIO0, QSPI0, GMAC0,    SDIO,     SATA_P1, I2S0,  ____,  PCIE_X1,  0),
	P100_PAD(19, GPIO0_19,          GPIO0, QSPI0, GMAC1,    ____,     TDM,   SATA_P0, ____,  PCIE_X1,  0),
	P100_PAD(20, GPIO0_20,          GPIO0, QSPI0, UART3,    UART3_IR, TDM,   SATA_P0, ZMAC,  PCIE_X1,  0),
	P100_PAD(21, GPIO0_21,          GPIO0, QSPI0, UART3,    UART3_IR, TDM,   SATA_P0, ZMAC,  PCIE_X1,  0),
	P100_PAD(22, GPIO0_22,          GPIO0, QSPI0, I2C2,     ____,     TDM,   SATA_P0, ZMAC,  PWM0,     0),
	P100_PAD(23, GPIO0_23,          GPIO0, QSPI0, I2C2,     ____,     ____,  SATA_P0, ZMAC,  PWM0,     0),
	P100_PAD(24, GPIO0_24,          GPIO0, QSPI0, I2C0,     CAN0,     CDBG,  PCIE_X1, UART0, PWM0,     0),
	P100_PAD(25, GPIO0_25,          GPIO0, QSPI0, I2C0,     CAN0,     CDBG,  PCIE_X1, UART0, PWM0,     0),
	P100_PAD(26, GPIO0_26,          GPIO0, QSPI0, I2C1,     CAN1,     CDBG,  PCIE_X1, UART0, UART0_IR, 0),
	P100_PAD(27, GPIO0_27,          GPIO0, QSPI0, I2C1,     CAN1,     CDBG,  PCIE_X1, UART0, UART0_IR, 0),
	P100_PAD(28, GPIO0_28,          GPIO0, PWM0,  SPI0,     UART1_IR, I2S0,  PCIE_X4, UART1, ____,     0),
	P100_PAD(29, GPIO0_29,          GPIO0, PWM0,  SPI0,     UART1_IR, I2S0,  PCIE_X4, UART1, ____,     0),
	P100_PAD(30, GPIO0_30,          GPIO0, PWM0,  SPI0,     ____,     I2S0,  PCIE_X4, UART1, ____,     0),
	P100_PAD(31, GPIO0_31,          GPIO0, PWM0,  SPI0,     QSPI0,    I2S0,  PCIE_X4, UART1, ____,     0),
	P100_PAD(32, GPIO1_0,           GPIO1, PWM0,  UART2,    UART2_IR, I2S0,  SDIO,    GMAC1, SATA_P0,  0),
	P100_PAD(33, GPIO1_1,           GPIO1, PWM0,  UART2,    UART2_IR, I2C2,  SATA_P1, SPI0,  SDIO,     0),
	P100_PAD(34, GPIO1_2,           GPIO1, GMAC1, TDM,      PWM0,     I2C2,  SATA_P1, ____,  ____,     0),
	P100_PAD(35, GPIO1_3,           GPIO1, GMAC1, TDM,      PWM0,     ____,  SATA_P1, ____,  ____,     0),
	P100_PAD(36, GPIO1_4,           GPIO1, GMAC1, TDM,      UART2,    ____,  SATA_P1, ____,  ____,     0),
	P100_PAD(37, GPIO1_5,           GPIO1, GMAC1, ____,     UART2,    ____,  SATA_P1, ____,  ____,     0),
	P100_PAD(38, GPIO1_6,           GPIO1, GMAC1, SPI0,     PWM0,     CDBG,  SATA_P0, ____,  ____,     0),
	P100_PAD(39, GPIO1_7,           GPIO1, GMAC1, SPI0,     PWM0,     CDBG,  SATA_P0, ____,  ____,     0),
	P100_PAD(40, GPIO1_8,           GPIO1, GMAC1, SPI0,     PDM,      CDBG,  SATA_P0, ____,  ____,     0),
	P100_PAD(41, GPIO1_9,           GPIO1, GMAC1, SPI0,     PWM0,     ____,  SATA_P0, GMAC0, ____,     0),
	P100_PAD(42, GPIO1_10,          GPIO1, GMAC1, SPI0,     PWM0,     ____,  SATA_P0, ____,  ____,     0),
	P100_PAD(43, GPIO1_11,          GPIO1, GMAC1, QSPI0,    PDM,      I2S0,  ISP0,    PWM0,  ____,     0),
	P100_PAD(44, GPIO1_12,          GPIO1, GMAC1, QSPI0,    PDM,      I2S0,  ISP0,    ____,  ____,     0),
	P100_PAD(45, GPIO1_13,          GPIO1, GMAC1, QSPI0,    PDM,      I2S0,  ISP0,    ____,  ____,     0),
	P100_PAD(46, GPIO1_14,          GPIO1, GMAC1, ____,     PDM,      I2S0,  ISP0,    ____,  ____,     0),
	P100_PAD(47, GPIO1_15,          GPIO1, GMAC1, SDIO,     PDM,      I2S0,  ISP0,    ____,  ____,     0),
	P100_PAD(48, GPIO1_16,          GPIO1, GMAC0, ____,     ____,     ____,  SDIO,    ____,  ____,     0),
	P100_PAD(49, MULTI_DIE_PACKAGE, GPIO1, ____,  ____,     ____,     ____,  ____,    ____,  ____,     0),
};

static const struct pinctrl_pin_desc p100_group2_pins[] = {
	P100_PAD(0,  GPIO2_0,  GPIO2, UART4, UART4_IR, PWM2,     ____,      I2C3,  ____,     ____,      0),
	P100_PAD(1,  GPIO2_1,  GPIO2, UART4, UART4_IR, PWM2,     ____,      I2C3,  ____,     ____,      0),
	P100_PAD(2,  GPIO2_2,  GPIO2, UART5, UART5_IR, PWM2,     ____,      I2C5,  I2S1,     ____,      0),
	P100_PAD(3,  GPIO2_3,  GPIO2, UART5, UART5_IR, PWM2,     ____,      I2C5,  I2S1,     ____,      0),
	P100_PAD(4,  GPIO2_4,  GPIO2, UART6, UART6_IR, PWM2,     ____,      I2C6,  I2S1,     ____,      0),
	P100_PAD(5,  GPIO2_5,  GPIO2, UART6, UART6_IR, PWM2,     ____,      I2C6,  I2S1,     ____,      0),
	P100_PAD(6,  GPIO2_6,  GPIO2, UART7, UART7_IR, PWM2,     SEN_VCLK,  I2C4,  I2S1,     ____,      0),
	P100_PAD(7,  GPIO2_7,  GPIO2, UART7, UART7_IR, PWM2,     SEN_VCLK,  I2C4,  ____,     ____,      0),
	P100_PAD(8,  GPIO2_8,  GPIO2, I2S2,  ____,     SPI1,     UART6,     I2C6,  PWM2,     ____,      0),
	P100_PAD(9,  GPIO2_9,  GPIO2, I2S2,  ____,     SPI1,     UART6,     I2C6,  PWM2,     ____,      0),
	P100_PAD(10, GPIO2_10, GPIO2, I2S2,  PWM1,     SPI1,     UART9,     I2C7,  ____,     ____,      0),
	P100_PAD(11, GPIO2_11, GPIO2, I2S2,  PWM1,     SPI1,     UART9,     I2C7,  ____,     ____,      0),
	P100_PAD(12, GPIO2_12, GPIO2, I2S2,  PWM1,     SPI1,     UART8,     I2C7,  ____,     ____,      0),
	P100_PAD(13, GPIO2_13, GPIO2, I2S3,  PWM1,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(14, GPIO2_14, GPIO2, I2S3,  PWM1,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(15, GPIO2_15, GPIO2, I2S3,  ____,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(16, GPIO2_16, GPIO2, I2S3,  ____,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(17, GPIO2_17, GPIO2, I2S3,  SPI1,     I2C4,     UART8,     I2C7,  ____,     ____,      0),
	P100_PAD(18, GPIO2_18, GPIO2, I2S3,  SPI1,     I2C4,     PWM2,      UART5, UART5_IR, ____,      0),
	P100_PAD(19, GPIO2_19, GPIO2, I2S3,  SPI1,     ____,     PWM1,      UART5, UART5_IR, ____,      0),
	P100_PAD(20, GPIO2_20, GPIO2, I2S3,  ____,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(21, GPIO2_21, GPIO2, I2S3,  SPI1,     I2C3,     PWM1,      UART8, ____,     ____,      0),
	P100_PAD(22, GPIO2_22, GPIO2, I2S3,  SPI1,     I2C3,     PWM1,      UART8, ____,     ____,      0),
	P100_PAD(23, GPIO2_23, GPIO2, I2S3,  PWM1,     ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(24, GPIO2_24, GPIO2, I2C3,  UART5,    ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(25, GPIO2_25, GPIO2, I2C3,  UART5,    ____,     HDMI,      QSPI1, PWM2,     ____,      0),
	P100_PAD(26, GPIO2_26, GPIO2, I2C4,  UART6,    ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(27, GPIO2_27, GPIO2, I2C4,  UART6,    ____,     ____,      ____,  ____,     ____,      0),
	P100_PAD(28, GPIO2_28, GPIO2, I2C5,  UART7,    PWM1,     ____,      QSPI1, ____,     ____,      0),
	P100_PAD(29, GPIO2_29, GPIO2, I2C5,  UART7,    PWM1,     ____,      QSPI1, ____,     ____,      0),
	P100_PAD(30, GPIO2_30, GPIO2, I2C6,  UART8,    PWM1,     HDMI,      QSPI1, I2S3,     ____,      0),
	P100_PAD(31, GPIO2_31, GPIO2, I2C6,  UART8,    PWM1,     HDMI,      QSPI1, I2S3,     ____,      0),
	P100_PAD(32, GPIO3_0,  GPIO3, I2C7,  UART9,    PWM1,     CAN2,      QSPI1, I2S3,     SEN_VCLK,  0),
	P100_PAD(33, GPIO3_1,  GPIO3, I2C7,  UART9,    PWM1,     CAN2,      QSPI1, I2S3,     SEN_VCLK,  0),
	P100_PAD(34, GPIO3_2,  GPIO3, QSPI1, UART8,    UART8_IR, ____,      ____,  I2S3,     PWM1,      0),
	P100_PAD(35, GPIO3_3,  GPIO3, QSPI1, UART8,    UART8_IR, ____,      ____,  I2S3,     PWM1,      0),
	P100_PAD(36, GPIO3_4,  GPIO3, QSPI1, UART9,    UART9_IR, ____,      ____,  I2S3,     PWM2,      0),
	P100_PAD(37, GPIO3_5,  GPIO3, QSPI1, UART9,    UART9_IR, ____,      ____,  I2S3,     PWM2,      0),
	P100_PAD(38, GPIO3_6,  GPIO3, QSPI1, I2S1,     I2C7_SMB,  ____,     DPTX,  I2S3,     PWM2,      0),
	P100_PAD(39, GPIO3_7,  GPIO3, QSPI1, I2S1,     I2C7_SMB,  UART7,    I2C5,  I2S3,     PWM2,      0),
	P100_PAD(40, GPIO3_8,  GPIO3, QSPI1, I2S1,     I2C7_SMB,  UART7,    I2C5,  I2S3,     PWM2,      0),
	P100_PAD(41, GPIO3_9,  GPIO3, UART4, I2S1,     I2C7_SMB,  UART5,    DPTX,  ____,     PWM2,      0),
	P100_PAD(42, GPIO3_10, GPIO3, UART4, I2S1,     I2C7_SMB,  UART5,    USB31, ____,     ____,      0),
};

static const struct pinctrl_pin_desc p100_group3_pins[] = {
	P100_PAD(0,  SDIO_CLK,  SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
	P100_PAD(1,  SDIO_CMD,  SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
	P100_PAD(2,  SDIO_DAT0, SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
	P100_PAD(3,  SDIO_DAT1, SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
	P100_PAD(4,  SDIO_DAT2, SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
	P100_PAD(5,  SDIO_DAT3, SDIO, GPIO4, ____, ____, ____, ____, ____, ____, 0),
};

static const struct p100_pad_group p100_group0 = {	/* aon */
	.name = "p100-group0",
	.pins = p100_group0_pins,
	.npins = ARRAY_SIZE(p100_group0_pins),
	.offset_mux = 0x400,
	.mask_mux = 0xf,
	.offset_cfg = 0x0,
	.mask_cfg = 0xffff,
	.type = P100_PADCTRL_AON,
};

static const struct p100_pad_group p100_group1 = {	/* peri1 */
	.name = "p100-group1",
	.pins = p100_group1_pins,
	.npins = ARRAY_SIZE(p100_group1_pins),
	.offset_mux = 0x400,
	.mask_mux = 0xf,
	.offset_cfg = 0x0,
	.mask_cfg = 0xffff,
	.type = P100_PADCTRL_0,
};

static const struct p100_pad_group p100_group2 = {	/* peri2 */
	.name = "p100-group2",
	.pins = p100_group2_pins,
	.npins = ARRAY_SIZE(p100_group2_pins),
	.offset_mux = 0x400,
	.mask_mux = 0xf,
	.offset_cfg = 0x0,
	.mask_cfg = 0xffff,
	.type = P100_PADCTRL_1,
};

static const struct p100_pad_group p100_group3 = {	/* peri3 gpio special; zhihe need mod. */
	.name = "p100-group3",
	.pins = p100_group3_pins,
	.npins = ARRAY_SIZE(p100_group3_pins),
	.offset_mux = 0x200,
	.mask_mux = 0x1,
	.offset_cfg = 0x0,
	.mask_cfg = 0x1,
	.type = P100_PADCTRL_2,
};

static int p100_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);

	return pp->desc.npins;
}

static const char *p100_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int gsel)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);

	return pp->desc.pins[gsel].name;
}

static int p100_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int gsel,
					 const unsigned int **pins,
					 unsigned int *npins)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pp->desc.pins[gsel].number;
	*npins = 1;
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void p100_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *padcfg = p100_padcfg(pp, pin);
	void __iomem *muxcfg = p100_muxcfg(pp, pin);
	u32 pad;
	u32 mux;

	scoped_guard(raw_spinlock_irqsave, &pp->lock) {
		pad = readl_relaxed(padcfg);
		mux = readl_relaxed(muxcfg);
	}

	seq_printf(s, "[PADCFG_%03u:0x%x=0x%07x MUXCFG_%03u:0x%x=0x%08x]",
		   1 + pin / 2, 0x000 + 4 * (pin / 2), pad,
		   1 + pin / 8, 0x400 + 4 * (pin / 8), mux);
}
#else
#define p100_pin_dbg_show NULL
#endif

static void p100_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				       struct pinctrl_map *map, unsigned int nmaps)
{
	unsigned long *seen = NULL;
	unsigned int i;

	for (i = 0; i < nmaps; i++) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_PIN &&
		    map[i].data.configs.configs != seen) {
			seen = map[i].data.configs.configs;
			kfree(seen);
		}
	}

	kfree(map);
}

static int p100_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np,
					 struct pinctrl_map **maps,
					 unsigned int *num_maps)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	struct device_node *child;
	struct pinctrl_map *map;
	unsigned long *configs;
	unsigned int nconfigs;
	unsigned int nmaps;
	int ret;

	nmaps = 0;
	for_each_available_child_of_node(np, child) {
		int npins = of_property_count_strings(child, "pins");

		if (npins <= 0) {
			of_node_put(child);
			dev_err(pp->pctl->dev, "no pins selected for %pOFn.%pOFn\n",
				np, child);
			return -EINVAL;
		}
		nmaps += npins;
		if (of_property_present(child, "function"))
			nmaps += npins;
	}

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	mutex_lock(&pp->mutex);
	for_each_available_child_of_node(np, child) {
		unsigned int rollback = nmaps;
		enum p100_muxtype muxtype;
		struct property *prop;
		const char *funcname;
		const char **pgnames;
		const char *pinname;
		int npins;

		ret = pinconf_generic_parse_dt_config(child, pctldev, &configs, &nconfigs);
		if (ret) {
			dev_err(pp->pctl->dev, "%pOFn.%pOFn: error parsing pin config\n",
				np, child);
			goto put_child;
		}

		if (!of_property_read_string(child, "function", &funcname)) {
			muxtype = p100_muxtype_get(funcname);
			if (!muxtype) {
				dev_err(pp->pctl->dev, "%pOFn.%pOFn: unknown function '%s'\n",
					np, child, funcname);
				ret = -EINVAL;
				goto free_configs;
			}

			funcname = devm_kasprintf(pp->pctl->dev, GFP_KERNEL, "%pOFn.%pOFn",
						  np, child);
			if (!funcname) {
				ret = -ENOMEM;
				goto free_configs;
			}

			npins = of_property_count_strings(child, "pins");
			pgnames = devm_kcalloc(pp->pctl->dev, npins, sizeof(*pgnames), GFP_KERNEL);
			if (!pgnames) {
				ret = -ENOMEM;
				goto free_configs;
			}
		} else {
			funcname = NULL;
		}

		npins = 0;
		of_property_for_each_string(child, "pins", prop, pinname) {
			unsigned int i;

			for (i = 0; i < pp->desc.npins; i++) {
				if (!strcmp(pinname, pp->desc.pins[i].name))
					break;
			}
			if (i == pp->desc.npins) {
				nmaps = rollback;
				dev_err(pp->pctl->dev, "%pOFn.%pOFn: unknown pin '%s'\n",
					np, child, pinname);
				goto free_configs;
			}

			if (nconfigs) {
				map[nmaps].type = PIN_MAP_TYPE_CONFIGS_PIN;
				map[nmaps].data.configs.group_or_pin = pp->desc.pins[i].name;
				map[nmaps].data.configs.configs = configs;
				map[nmaps].data.configs.num_configs = nconfigs;
				nmaps += 1;
			}
			if (funcname) {
				pgnames[npins++] = pp->desc.pins[i].name;
				map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
				map[nmaps].data.mux.function = funcname;
				map[nmaps].data.mux.group = pp->desc.pins[i].name;
				nmaps += 1;
			}
		}

		if (funcname) {
			ret = pinmux_generic_add_function(pctldev, funcname, pgnames,
							  npins, (void *)muxtype);
			if (ret < 0) {
				dev_err(pp->pctl->dev, "error adding function %s\n", funcname);
				goto put_child;
			}
		}
	}

	*maps = map;
	*num_maps = nmaps;
	mutex_unlock(&pp->mutex);
	return 0;

free_configs:
	kfree(configs);
put_child:
	of_node_put(child);
	p100_pinctrl_dt_free_map(pctldev, map, nmaps);
	mutex_unlock(&pp->mutex);
	return ret;
}

static const struct pinctrl_ops p100_pinctrl_ops = {
	.get_groups_count = p100_pinctrl_get_groups_count,
	.get_group_name = p100_pinctrl_get_group_name,
	.get_group_pins = p100_pinctrl_get_group_pins,
	.pin_dbg_show = p100_pin_dbg_show,
	.dt_node_to_map = p100_pinctrl_dt_node_to_map,
	.dt_free_map = p100_pinctrl_dt_free_map,
};

static const u8 p100_drive_strength_in_mA[16] = {
	1, 2, 3, 5, 7, 8, 10, 12, 13, 15, 16, 18, 20, 21, 23, 25,
};

static u16 p100_drive_strength_from_mA(u32 arg)
{
	u16 ds;

	for (ds = 0; ds < P100_PADCFG_DS; ds++) {
		if (arg <= p100_drive_strength_in_mA[ds])
			return ds;
	}
	return P100_PADCFG_DS;
}

static int p100_padcfg_rmw(struct p100_pinctrl *pp, unsigned int pin,
			     u32 mask, u32 value)
{
	void __iomem *padcfg = p100_padcfg(pp, pin);
	unsigned int shift = p100_padcfg_shift(pp, pin);
	u32 tmp;

	mask <<= shift;
	value <<= shift;

	scoped_guard(raw_spinlock_irqsave, &pp->lock) {
		tmp = readl_relaxed(padcfg);
		tmp = (tmp & ~mask) | value;
		writel_relaxed(tmp, padcfg);
	}
	return 0;
}

static int p100_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	bool enabled;
	int param;
	u32 value;
	u32 arg;

	if ((uintptr_t)desc->drv_data & P100_PAD_NO_PADCFG)
		return -ENOTSUPP;

	value = readl_relaxed(p100_padcfg(pp, pin));
	value = (value >> p100_padcfg_shift(pp, pin)) & GENMASK(9, 0);

	param = pinconf_to_config_param(*config);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(value & (P100_PADCFG_SPU | P100_PADCFG_PE));
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = (value & P100_PADCFG_BIAS) == P100_PADCFG_PE;
		arg = enabled ? P100_PULL_DOWN_OHM : 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (value & P100_PADCFG_SPU) {
			enabled = true;
			arg = P100_PULL_STRONG_OHM;
		} else if ((value & (P100_PADCFG_PE | P100_PADCFG_PS)) ==
				    (P100_PADCFG_PE | P100_PADCFG_PS)) {
			enabled = true;
			arg = P100_PULL_UP_OHM;
		} else {
			enabled = false;
			arg = 0;
		}
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		enabled = true;
		arg = p100_drive_strength_in_mA[value & P100_PADCFG_DS];
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		enabled = value & P100_PADCFG_IE;
		arg = enabled ? 1 : 0;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = value & P100_PADCFG_ST;
		arg = enabled ? 1 : 0;
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = value & P100_PADCFG_SL;
		arg = enabled ? 1 : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int p100_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int gsel, unsigned long *config)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = pp->desc.pins[gsel].number;

	return p100_pinconf_get(pctldev, pin, config);
}

static int p100_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int i;
	u16 mask, value;

	if ((uintptr_t)desc->drv_data & P100_PAD_NO_PADCFG)
		return -ENOTSUPP;

	mask = 0;
	value = 0;
	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			mask |= P100_PADCFG_BIAS;
			value &= ~P100_PADCFG_BIAS;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= P100_PADCFG_BIAS;
			value &= ~P100_PADCFG_BIAS;
			value |= P100_PADCFG_PE;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= P100_PADCFG_BIAS;
			value &= ~P100_PADCFG_BIAS;
			if (arg == P100_PULL_STRONG_OHM)
				value |= P100_PADCFG_SPU;
			else
				value |= P100_PADCFG_PE | P100_PADCFG_PS;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			mask |= P100_PADCFG_DS;
			value &= ~P100_PADCFG_DS;
			value |= p100_drive_strength_from_mA(arg);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			mask |= P100_PADCFG_IE;
			if (arg)
				value |= P100_PADCFG_IE;
			else
				value &= ~P100_PADCFG_IE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			mask |= P100_PADCFG_ST;
			if (arg)
				value |= P100_PADCFG_ST;
			else
				value &= ~P100_PADCFG_ST;
			break;
		case PIN_CONFIG_SLEW_RATE:
			mask |= P100_PADCFG_SL;
			if (arg)
				value |= P100_PADCFG_SL;
			else
				value &= ~P100_PADCFG_SL;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return p100_padcfg_rmw(pp, pin, mask, value);
}

static int p100_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = pp->desc.pins[gsel].number;

	return p100_pinconf_set(pctldev, pin, configs, num_configs);
}

#ifdef CONFIG_DEBUG_FS
static void p100_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s, unsigned int pin)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	u32 value = readl_relaxed(p100_padcfg(pp, pin));

	value = (value >> p100_padcfg_shift(pp, pin)) & GENMASK(9, 0);

	seq_printf(s, " [0x%03x]", value);
}
#else
#define p100_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops p100_pinconf_ops = {
	.pin_config_get = p100_pinconf_get,
	.pin_config_group_get = p100_pinconf_group_get,
	.pin_config_set = p100_pinconf_set,
	.pin_config_group_set = p100_pinconf_group_set,
	.pin_config_dbg_show = p100_pinconf_dbg_show,
	.is_generic = true,
};

static int p100_pinmux_set(struct p100_pinctrl *pp, unsigned int pin,
			     unsigned long muxdata, enum p100_muxtype muxtype)
{
	void __iomem *muxcfg = p100_muxcfg(pp, pin);
	unsigned int shift = p100_muxcfg_shift(pp, pin);
	u32 mask, value, tmp;

	for (value = 0; muxdata; muxdata >>= 7, value++) {
		if ((muxdata & GENMASK(6, 0)) == muxtype)
			break;
	}
	if (!muxdata) {
		dev_err(pp->pctl->dev, "invalid mux %s for pin %s\n",
			p100_muxtype_string[muxtype], pin_get_name(pp->pctl, pin));
		return -EINVAL;
	}

	mask = pp->mask_mux << shift;
	value = value << shift;

	scoped_guard(raw_spinlock_irqsave, &pp->lock) {
		tmp = readl_relaxed(muxcfg);
		tmp = (tmp & ~mask) | value;
		writel_relaxed(tmp, muxcfg);
	}

	return 0;
}

static int p100_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int fsel, unsigned int gsel)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func = pinmux_generic_get_function(pctldev, fsel);

	return p100_pinmux_set(pp, pp->desc.pins[gsel].number,
				 (uintptr_t)pp->desc.pins[gsel].drv_data & P100_PAD_MUXDATA,
				 (uintptr_t)func->data);
}

static int p100_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int muxtype, value;
	const struct pin_desc *desc = pin_desc_get(pctldev, offset);
	unsigned long muxdata = (uintptr_t)desc->drv_data & P100_PAD_MUXDATA;

	for (value = 0; muxdata; muxdata >>= 7, value++) {
		if (value <= 7) { // ALT0~ALT7
			switch (muxdata & GENMASK(6, 0)) {
			case P100_MUX_AOGPIO0:
				muxtype = P100_MUX_AOGPIO0;
				break;
			case P100_MUX_AOGPIO1:
				muxtype = P100_MUX_AOGPIO1;
				break;
			case P100_MUX_GPIO0:
				muxtype = P100_MUX_GPIO0;
				break;
			case P100_MUX_GPIO1:
				muxtype = P100_MUX_GPIO1;
				break;
			case P100_MUX_GPIO2:
				muxtype = P100_MUX_GPIO2;
				break;
			case P100_MUX_GPIO3:
				muxtype = P100_MUX_GPIO3;
				break;
			case P100_MUX_GPIO4:
				muxtype = P100_MUX_GPIO4;
				break;
			default:
				continue;
			}
		} else {
			dev_err(pp->pctl->dev, "invalid mux %#lx for pin %s\n", (uintptr_t)desc->drv_data, desc->name);
			return -EINVAL;
		}
		break;
	}

	return p100_pinmux_set(pp, offset,
				 (uintptr_t)desc->drv_data & P100_PAD_MUXDATA, muxtype);
}

static int p100_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int offset, bool input)
{
	struct p100_pinctrl *pp = pinctrl_dev_get_drvdata(pctldev);

	return p100_padcfg_rmw(pp, offset, P100_PADCFG_IE,
				 input ? P100_PADCFG_IE : 0);
}

static const struct pinmux_ops p100_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = p100_pinmux_set_mux,
	.gpio_request_enable = p100_gpio_request_enable,
	.gpio_set_direction = p100_gpio_set_direction,
	.strict = true,
};

static int p100_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct p100_pad_group *group = device_get_match_data(dev);
	struct p100_pinctrl *pp;
	int ret;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	pp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pp->base))
		return PTR_ERR(pp->base);

	pp->desc.name = group->name;
	pp->desc.pins = group->pins;
	pp->desc.npins = group->npins;
	pp->offset_mux = group->offset_mux;
	pp->mask_mux = group->mask_mux;
	pp->offset_cfg = group->offset_cfg;
	pp->mask_cfg = group->mask_cfg;
	pp->type = group->type;
	pp->desc.pctlops = &p100_pinctrl_ops;
	pp->desc.pmxops = &p100_pinmux_ops;
	pp->desc.confops = &p100_pinconf_ops;
	pp->desc.owner = THIS_MODULE;
	mutex_init(&pp->mutex);
	raw_spin_lock_init(&pp->lock);

	if (pp->type > P100_PADCTRL_AON) {
		pp->clk = devm_clk_get_enabled(dev, "pclk");
		if (IS_ERR(pp->clk))
			return dev_err_probe(dev, PTR_ERR(pp->clk), "error getting clock\n");
	} else {
		pp->clk = devm_clk_get_enabled(dev, NULL);
		if (IS_ERR(pp->clk))
			return dev_err_probe(dev, PTR_ERR(pp->clk), "error getting clock\n");
	}

	platform_set_drvdata(pdev, pp);
	ret = devm_pinctrl_register_and_init(dev, &pp->desc, pp, &pp->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");

	return pinctrl_enable(pp->pctl);
}

#ifdef CONFIG_PM_SLEEP
static int p100_pinctrl_backup_regs(struct p100_pinctrl *pp, unsigned int cfg_reg_nums,
					unsigned int mux_reg_nums)
{
	int i;

	for (i = 0; i < cfg_reg_nums; i++)
		pp->cfg_bak[i] = readl(P100_PM_PAD_CFG(i));
	for (i = 0; i < mux_reg_nums; i++)
		pp->mux_bak[i] = readl(P100_PM_PAD_MUX(i));

	return 0;
}

static int p100_pinctrl_restore_regs(struct p100_pinctrl *pp, unsigned int cfg_reg_nums,
				unsigned int mux_reg_nums)
{
	int i;

	for (i = 0; i < cfg_reg_nums; i++)
		writel(pp->cfg_bak[i], P100_PM_PAD_CFG(i));
	for (i = 0; i < mux_reg_nums; i++)
		writel(pp->mux_bak[i], P100_PM_PAD_MUX(i));

	return 0;
}

static int p100_pinctrl_suspend(struct device *dev)
{
	dev_info(dev, "p100 pinctrl suspend\n");
	struct p100_pinctrl *pp = dev_get_drvdata(dev);
	int ret = 0;

	switch(pp->type) {
		case P100_PADCTRL_AON:
			ret = p100_pinctrl_backup_regs(pp, P100_AON_CFG_REG_NUMS, P100_AON_MUX_REG_NUMS);
			break;
		case P100_PADCTRL_0:
			ret = p100_pinctrl_backup_regs(pp, P100_PADCTRL0_CFG_REG_NUMS, P100_PADCTRL0_MUX_REG_NUMS);
			clk_disable_unprepare(pp->clk);
			break;
		case P100_PADCTRL_1:
			ret = p100_pinctrl_backup_regs(pp, P100_PADCTRL1_CFG_REG_NUMS, P100_PADCTRL1_MUX_REG_NUMS);
			clk_disable_unprepare(pp->clk);
			break;
		case P100_PADCTRL_2:
			ret = p100_pinctrl_backup_regs(pp, P100_PADCTRL2_CFG_REG_NUMS, P100_PADCTRL2_MUX_REG_NUMS);
			clk_disable_unprepare(pp->clk);
			pp->mux_bak[P100_AUDIO_IO_SEL_IDX] = readl(pp->base);
			break;
		default:
			break;
	}

	return ret;
}

static int p100_pinctrl_resume(struct device *dev)
{
	dev_info(dev, "p100 pinctrl resume\n");
	struct p100_pinctrl *pp = dev_get_drvdata(dev);
	int ret = 0;

	switch(pp->type) {
		case P100_PADCTRL_AON:
			ret = p100_pinctrl_restore_regs(pp, P100_AON_CFG_REG_NUMS, P100_AON_MUX_REG_NUMS);
			break;
		case P100_PADCTRL_0:
			ret = clk_prepare_enable(pp->clk);
			if (ret) {
				dev_err(dev, "could not enable padctrl clk\n");
				return -EINVAL;
			}
			ret = p100_pinctrl_restore_regs(pp, P100_PADCTRL0_CFG_REG_NUMS, P100_PADCTRL0_MUX_REG_NUMS);
			break;
		case P100_PADCTRL_1:
			ret = clk_prepare_enable(pp->clk);
			if (ret) {
				dev_err(dev, "could not enable padctrl clk\n");
				return -EINVAL;
			}
			ret = p100_pinctrl_restore_regs(pp, P100_PADCTRL1_CFG_REG_NUMS, P100_PADCTRL1_MUX_REG_NUMS);
			break;
		case P100_PADCTRL_2:
			ret = clk_prepare_enable(pp->clk);
			if (ret) {
				dev_err(dev, "could not enable padctrl clk\n");
				return -EINVAL;
			}
			ret = p100_pinctrl_restore_regs(pp, P100_PADCTRL2_CFG_REG_NUMS, P100_PADCTRL2_MUX_REG_NUMS);
			break;
		default:
			break;
	}

	return ret;
}
#endif	//CONFIG_PM_SLEEP

static const struct dev_pm_ops p100_pinctrl_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(p100_pinctrl_suspend, p100_pinctrl_resume)
};

static const struct of_device_id p100_pinctrl_of_match[] = {
	{ .compatible = "zhihe,p100-group0-pinctrl", .data = &p100_group0 },	/* aon_padctrl */
	{ .compatible = "zhihe,p100-group1-pinctrl", .data = &p100_group1 },	/* peri1_padctrl */
	{ .compatible = "zhihe,p100-group2-pinctrl", .data = &p100_group2 },	/* peri2_padctrl */
	{ .compatible = "zhihe,p100-group3-pinctrl", .data = &p100_group3 },	/* peri3_padctrl */
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, p100_pinctrl_of_match);

static struct platform_driver p100_pinctrl_driver = {
	.probe = p100_pinctrl_probe,
	.driver = {
		.name = "pinctrl-p100",
		.of_match_table = p100_pinctrl_of_match,
		.pm = &p100_pinctrl_dev_pm_ops,
	},
};
module_platform_driver(p100_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the ZhiHe P100 SoC");
MODULE_AUTHOR("Emil Renner Berthing <emil.renner.berthing@canonical.com>");
MODULE_LICENSE("GPL");
