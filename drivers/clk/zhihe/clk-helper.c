/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/device.h>

#include "clk-helper.h"

#define P100_PLL_CFG0		0x0
#define P100_PLL_CFG1		0x04
#define P100_PLL_CFG2		0x8
#define P100_POSTDIV2_SHIFT	24
#define P100_POSTDIV2_MASK	GENMASK(26, 24)
#define P100_POSTDIV1_SHIFT	20
#define P100_POSTDIV1_MASK	GENMASK(22, 20)
#define P100_FBDIV_SHIFT	8
#define P100_FBDIV_MASK	GENMASK(19, 8)
#define P100_REFDIV_SHIFT	0
#define P100_REFDIV_MASK	GENMASK(5, 0)
#define P100_BYPASS_MASK	BIT(30)
#define P100_RST_MASK		BIT(29)
#define P100_DSMPD_MASK	BIT(24)
#define P100_DACPD_MASK	BIT(25)
#define P100_FRAC_MASK		GENMASK(23, 0)
#define P100_FRAC_SHIFT	0
#define P100_FRAC_DIV		BIT(24)

#define LOCK_TIMEOUT_US		10000

#define div_mask(d)	((1 << (d->width)) - 1)

DEFINE_SPINLOCK(zhihe_p100_clk_lock);

#define to_clk_p100pll(_hw) container_of(_hw, struct clk_p100pll, hw)

void zhihe_unregister_clocks(struct clk *clks[], unsigned int count)
{
        unsigned int i;

        for (i = 0; i < count; i++)
                clk_unregister(clks[i]);
}

static int clk_p100_pll_wait_lock(struct clk_p100pll *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base + pll->pll_sts_off, val,
				  val & pll->pll_lock_bit, 0,
				  LOCK_TIMEOUT_US);
}

static int clk_p100_pll_prepare(struct clk_hw *hw)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	void __iomem *cfg1_off;
	u32 val;
	int ret;

	cfg1_off = pll->base + pll->cfg0_reg_off + P100_PLL_CFG1;
	val = readl_relaxed(cfg1_off);
	if (!(val & pll->pll_rst_bit))
		return 0;

	/* Enable RST */
	val |= pll->pll_rst_bit;
	writel_relaxed(val, cfg1_off);

	udelay(3);

	/* Disable RST */
	val &= ~pll->pll_rst_bit;
	writel_relaxed(val, cfg1_off);

	ret = clk_p100_pll_wait_lock(pll);
	if (ret)
		return ret;

	return 0;
}

static int clk_p100_pll_fake_prepare(struct clk_hw *hw)
{
	return 0;
}

static int clk_p100_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);

	return (val & pll->pll_rst_bit) ? 0 : 1;
}

static void clk_p100_pll_unprepare(struct clk_hw *hw)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);
	val |= pll->pll_rst_bit;
	writel_relaxed(val, pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);
}

static unsigned long clk_p100_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	u32 refdiv, fbdiv, postdiv1, postdiv2, frac;
	u32 pll_cfg0, pll_cfg1;
	u64 fvco = 0;

	pll_cfg0 = readl_relaxed(pll->base + pll->cfg0_reg_off);
	pll_cfg1 = readl_relaxed(pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);
	refdiv = (pll_cfg0 & P100_REFDIV_MASK) >> P100_REFDIV_SHIFT;
	fbdiv = (pll_cfg0 & P100_FBDIV_MASK) >> P100_FBDIV_SHIFT;
	postdiv1 = (pll_cfg0 & P100_POSTDIV1_MASK) >> P100_POSTDIV1_SHIFT;
	postdiv2 = (pll_cfg0 & P100_POSTDIV2_MASK) >> P100_POSTDIV2_SHIFT;
	frac = (pll_cfg1 & P100_FRAC_MASK) >> P100_FRAC_SHIFT;

	/* rate calculation:
	 * INT mode: FOUTVCO = FREE * FBDIV / REFDIV
	 * FRAC mode:FOUTVCO = (FREE * FBDIV + FREE * FRAC/BIT(24)) / REFDIV
	 */
	if (pll->pll_mode == PLL_MODE_FRAC)
		fvco = (parent_rate * frac) / P100_FRAC_DIV;

	fvco += (parent_rate * fbdiv);
	do_div(fvco, refdiv);

	if (pll->out_type == P100_PLL_DIV)
		do_div(fvco, postdiv1 * postdiv2);

	return fvco;
}

/* Zhihe P100 Pll recalc rate bypass for HAPS and EMU*/
static unsigned long clk_p100_pll_recalc_rate_fake_pll(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	const struct p100_pll_rate_table *rate_table = pll->rate_table;

	/* return minimum supported value */
	if (pll->out_type == P100_PLL_DIV)
		return rate_table[0].rate;

	return rate_table[0].vco_rate;
}

static const struct p100_pll_rate_table *p100_get_pll_div_settings(
		struct clk_p100pll *pll, unsigned long rate)
{
	const struct p100_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++)
		if (rate == rate_table[i].rate)
			return &rate_table[i];

	return NULL;
}

static const struct p100_pll_rate_table *p100_get_pll_vco_settings(
		struct clk_p100pll *pll, unsigned long rate)
{
	const struct p100_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++)
		if (rate == rate_table[i].vco_rate)
			return &rate_table[i];

	return NULL;
}

static inline bool clk_p100_pll_change(struct clk_p100pll *pll,
					const struct p100_pll_rate_table *rate)
{
	u32 refdiv_old, fbdiv_old, postdiv1_old, postdiv2_old, frac_old;
	u32 cfg0, cfg1;
	bool pll_changed;

	cfg0 = readl_relaxed(pll->base + pll->cfg0_reg_off);
	cfg1 = readl_relaxed(pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);

	refdiv_old = (cfg0 & P100_REFDIV_MASK) >> P100_REFDIV_SHIFT;
	fbdiv_old = (cfg0 & P100_FBDIV_MASK) >> P100_FBDIV_SHIFT;
	postdiv1_old = (cfg0 & P100_POSTDIV1_MASK) >> P100_POSTDIV1_SHIFT;
	postdiv2_old = (cfg0 & P100_POSTDIV2_MASK) >> P100_POSTDIV2_SHIFT;
	frac_old = (cfg1 & P100_FRAC_MASK) >> P100_FRAC_SHIFT;

	pll_changed = rate->refdiv != refdiv_old || rate->fbdiv != fbdiv_old ||
		      rate->postdiv1 != postdiv1_old || rate->postdiv2 != postdiv2_old;
	if (pll->pll_mode == PLL_MODE_FRAC)
		pll_changed |= (rate->frac != frac_old);

	return pll_changed;
}

static int clk_p100_pll_set_rate(struct clk_hw *hw, unsigned long drate,
				 unsigned long prate)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	const struct p100_pll_rate_table *rate;
	void __iomem *cfg1_off;
	u32 tmp, div_val;
	int ret;

	if (pll->out_type == P100_PLL_VCO) {
		rate = p100_get_pll_vco_settings(pll, drate);
		if (!rate) {
			pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
				drate, clk_hw_get_name(hw));
			return -EINVAL;
		}
	} else {
		rate = p100_get_pll_div_settings(pll, drate);
		if (!rate) {
			pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
				drate, clk_hw_get_name(hw));
			return -EINVAL;
		}
	}

	if (!clk_p100_pll_change(pll, rate))
		return 0;

	/* Enable RST */
	cfg1_off = pll->base + pll->cfg0_reg_off + P100_PLL_CFG1;
	tmp = readl_relaxed(cfg1_off);
	tmp |= pll->pll_rst_bit;
	writel_relaxed(tmp, cfg1_off);

	div_val = (rate->refdiv << P100_REFDIV_SHIFT) |
		  (rate->fbdiv << P100_FBDIV_SHIFT) |
		  (rate->postdiv1 << P100_POSTDIV1_SHIFT) |
		  (rate->postdiv2 << P100_POSTDIV2_SHIFT);
	writel_relaxed(div_val, pll->base + pll->cfg0_reg_off);

	if (pll->pll_mode == PLL_MODE_FRAC) {
		tmp &= ~(P100_FRAC_MASK << P100_FRAC_SHIFT);
		tmp |= rate->frac;
		writel_relaxed(tmp, cfg1_off);
	}

	udelay(3);

	/* Disable RST */
	tmp &= ~pll->pll_rst_bit;
	writel_relaxed(tmp, cfg1_off);

	/* Wait Lock, ~20us cost */
	ret = clk_p100_pll_wait_lock(pll);
	if (ret)
		return ret;

	/* HW requires 30us for pll stable */
	udelay(30);

	return 0;
}

static long clk_p100_pllvco_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	const struct p100_pll_rate_table *rate_table = pll->rate_table;
	unsigned long best = 0, now = 0;
	unsigned int i, best_i = 0;

	for (i = 0; i < pll->rate_count; i++) {
		now = rate_table[i].vco_rate;

		if (rate == now) {
			return rate_table[i].vco_rate;
		} else if (abs(now - rate) < abs(best - rate)) {
			best = now;
			best_i = i;
		}
	}

	/* return minimum supported value */
	return rate_table[best_i].vco_rate;
}

static long clk_p100_plldiv_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_p100pll *pll = to_clk_p100pll(hw);
	const struct p100_pll_rate_table *rate_table = pll->rate_table;
	unsigned long best = 0, now = 0;
	unsigned int i, best_i = 0;

	for (i = 0; i < pll->rate_count; i++) {
		now = rate_table[i].rate;

		if (rate == now) {
			return rate_table[i].rate;
		} else if (abs(now - rate) < abs(best - rate)) {
			best = now;
			best_i = i;
		}
	}

	/* return minimum supported value */
	return rate_table[best_i].rate;
}

static struct clk_ops clk_p100_pll_def_ops = {
	.recalc_rate	= clk_p100_pll_recalc_rate,
};

static struct clk_ops clk_p100_pllvco_ops = {
	.prepare	= clk_p100_pll_prepare,
	.unprepare	= clk_p100_pll_unprepare,
	.is_prepared	= clk_p100_pll_is_prepared,
	.recalc_rate	= clk_p100_pll_recalc_rate,
	.round_rate	= clk_p100_pllvco_round_rate,
	.set_rate	= clk_p100_pll_set_rate,
};

static struct clk_ops clk_p100_plldiv_ops = {
	.prepare	= clk_p100_pll_prepare,
	.unprepare	= clk_p100_pll_unprepare,
	.is_prepared	= clk_p100_pll_is_prepared,
	.recalc_rate	= clk_p100_pll_recalc_rate,
	.round_rate	= clk_p100_plldiv_round_rate,
	.set_rate	= clk_p100_pll_set_rate,
};

int zhihe_clk_set_round_rate(struct device *dev, struct clk *clk, unsigned int freq)
{
	unsigned long r;
	int ret;

	r = clk_round_rate(clk, freq);
	if (r != freq)
		dev_warn(dev, "%s rounded rate:%ld not equal to desired rate:%d\n", __clk_get_name(clk),
			r, freq);

	ret = clk_set_rate(clk, r);
	if (ret) {
		dev_err(dev, "failed to set clks for %s\n", __clk_get_name(clk));
	}

	return ret;
}

void zhihe_p100_clk_fake_pll_fixed_ops(void)
{
	clk_p100_pll_def_ops.recalc_rate = clk_p100_pll_recalc_rate_fake_pll;
	clk_p100_pllvco_ops.recalc_rate = clk_p100_pll_recalc_rate_fake_pll;
	clk_p100_pllvco_ops.prepare = clk_p100_pll_fake_prepare;
	clk_p100_plldiv_ops.recalc_rate = clk_p100_pll_recalc_rate_fake_pll;
	clk_p100_plldiv_ops.prepare = clk_p100_pll_fake_prepare;

	return;
}

struct clk *zhihe_p100_pll(const char *name, const char *parent_name,
			    void __iomem *base,
			    const struct p100_clk_info_pll *pll_clk)
{
	struct clk_p100pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	u32 val;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = pll_clk->flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	switch (pll_clk->out_type) {
	case P100_PLL_VCO:
		if (pll_clk->rate_table)
			init.ops = &clk_p100_pllvco_ops;
		break;
	case P100_PLL_DIV:
		if (pll_clk->rate_table)
			init.ops = &clk_p100_plldiv_ops;
		break;
	default:
		pr_err("%s: Unknown pll out type for pll clk %s\n",
		       __func__, name);
	};

	if (!pll_clk->rate_table)
		init.ops = &clk_p100_pll_def_ops;

	pll->base = base;
	pll->hw.init = &init;
	pll->out_type = pll_clk->out_type;
	pll->clk_type = pll_clk->clk_type;
	pll->rate_table = pll_clk->rate_table;
	pll->rate_count = pll_clk->rate_count;
	pll->cfg0_reg_off = pll_clk->cfg0_reg_off;
	pll->pll_sts_off = pll_clk->pll_sts_off;
	pll->pll_lock_bit = pll_clk->pll_lock_bit;
	pll->pll_bypass_bit = pll_clk->pll_bypass_bit;
	pll->pll_rst_bit = pll_clk->pll_rst_bit;
	pll->pll_mode = pll_clk->pll_mode;

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);
	val &= ~pll->pll_bypass_bit;
	val |= P100_DACPD_MASK;
	val |= P100_DSMPD_MASK;
	if (pll->pll_mode == PLL_MODE_FRAC) {
		val &= ~P100_DSMPD_MASK;
		val &= ~P100_DACPD_MASK;
	}
	writel_relaxed(val, pll->base + pll->cfg0_reg_off + P100_PLL_CFG1);

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll %s %lu\n",
			__func__, name, PTR_ERR(clk));
		kfree(pll);
	}

	return clk;
}

static inline struct clk_p100div *to_clk_p100div(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct clk_p100div, divider);
}

static unsigned long clk_p100div_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_p100div *p100_div = to_clk_p100div(hw);

	return p100_div->ops->recalc_rate(&p100_div->divider.hw, parent_rate);
}

static long clk_p100div_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	struct clk_p100div *p100_div = to_clk_p100div(hw);

	return p100_div->ops->round_rate(&p100_div->divider.hw, rate, prate);
}

static int clk_p100div_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_p100div *p100_div = to_clk_p100div(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned int divider, value;
	unsigned long flags = 0;
	u32 val;

	/** 
	 * The clk-divider will calculate the node frequency by rounding up 
	 * based on the parent frequency and the target divider.
	 * This calculation is to restore accurate frequency divider. 
	*/
	divider = DIV64_U64_ROUND_CLOSEST(parent_rate, rate);

	/* DIV is zero based divider, but CDE is not */
	if (p100_div->div_type == MUX_TYPE_DIV)
		value = divider;
	else
		value = divider - 1;

	/* handle the div valid range */
	if (value > p100_div->max_div)
		value = p100_div->max_div;
	if (value < p100_div->min_div)
		value = p100_div->min_div;

	spin_lock_irqsave(div->lock, flags);

	val = readl(div->reg);

	if (p100_div->sync_en != NO_DIV_EN) {
		val &= ~BIT(p100_div->sync_en);
		writel(val, div->reg);
		udelay(1);
	}

	val &= ~(div_mask(div) << div->shift);
	val |= value << div->shift;
	writel(val, div->reg);

	if (p100_div->sync_en != NO_DIV_EN) {
		udelay(1);
		val |= BIT(p100_div->sync_en);
		writel(val, div->reg);
	}

	spin_unlock_irqrestore(div->lock, flags);

	return 0;
}

static const struct clk_ops clk_p100div_ops = {
	.recalc_rate = clk_p100div_recalc_rate,
	.round_rate = clk_p100div_round_rate,
	.set_rate = clk_p100div_set_rate,
};

static struct clk *zhihe_clk_p100_divider_internal(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width,
				       u8 sync, enum p100_div_type div_type,
				       u16 min, u16 max, bool closest)
{
	struct clk_p100div *p100_div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	p100_div = kzalloc(sizeof(*p100_div), GFP_KERNEL);
	if (!p100_div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_p100div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;

	p100_div->divider.reg = reg;
	p100_div->divider.shift = shift;
	p100_div->divider.width = width;
	p100_div->divider.lock = &zhihe_p100_clk_lock;
	p100_div->divider.hw.init = &init;
	p100_div->ops = &clk_divider_ops;
	p100_div->sync_en = sync;
	p100_div->div_type = div_type;
	if (p100_div->div_type == MUX_TYPE_DIV)
		p100_div->divider.flags = CLK_DIVIDER_ONE_BASED;

	if (closest)
		p100_div->divider.flags |= CLK_DIVIDER_ROUND_CLOSEST;

	p100_div->min_div = min > ((1 << width) - 1) ?
			     ((1 << width) - 1) : min;
	p100_div->max_div = max > ((1 << width) - 1) ?
			     ((1 << width) - 1) : max;

	hw = &p100_div->divider.hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(p100_div);
		return ERR_PTR(ret);
	}

	return hw->clk;
}

struct clk *zhihe_clk_p100_divider(const char *name, const char *parent,
						void __iomem *reg, u8 shift, u8 width,
						u8 sync, enum p100_div_type div_type,
						u16 min, u16 max)
{
	return zhihe_clk_p100_divider_internal(name, parent, reg, shift, width,
											sync, div_type, min, max, false);
}

struct clk *zhihe_clk_p100_divider_closest(const char *name, const char *parent,
						void __iomem *reg, u8 shift, u8 width,
						u8 sync, enum p100_div_type div_type,
						u16 min, u16 max)
{
	return zhihe_clk_p100_divider_internal(name, parent, reg, shift, width,
											sync, div_type, min, max, true);
}

static inline struct clk_p100gate *to_clk_p100gate(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);

	return container_of(gate, struct clk_p100gate, gate);
}

static int clk_p100_gate_share_is_enabled(struct clk_hw *hw)
{
	struct clk_p100gate *p100_gate = to_clk_p100gate(hw);

	return p100_gate->ops->is_enabled(hw);
}

static int clk_p100_gate_share_enable(struct clk_hw *hw)
{
	struct clk_p100gate *p100_gate = to_clk_p100gate(hw);

	if (p100_gate->share_count && (*p100_gate->share_count)++ > 0) {
		return 0;
	}

	return p100_gate->ops->enable(hw);
}

static void clk_p100_gate_share_disable(struct clk_hw *hw)
{
	struct clk_p100gate *p100_gate = to_clk_p100gate(hw);

	if (p100_gate->share_count) {
		if (WARN_ON(*p100_gate->share_count == 0))
			return;
		else if (--(*p100_gate->share_count) > 0) {
			return;
		}
	}

	p100_gate->ops->disable(hw);
}

static void clk_p100_gate_share_disable_unused(struct clk_hw *hw)
{
	struct clk_p100gate *p100_gate = to_clk_p100gate(hw);

	if (!p100_gate->share_count || *p100_gate->share_count == 0)
		return p100_gate->ops->disable(hw);
}

static const struct clk_ops clk_p100gate_share_ops = {
	.enable = clk_p100_gate_share_enable,
	.disable = clk_p100_gate_share_disable,
	.disable_unused = clk_p100_gate_share_disable_unused,
	.is_enabled = clk_p100_gate_share_is_enabled,
};

struct clk *zhihe_clk_p100_register_gate_shared(const char *name, const char *parent,
						 unsigned long flags, void __iomem *reg,
						 u8 shift, spinlock_t *lock,
						 unsigned int *share_count)
{
	struct clk_p100gate *p100_gate;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	p100_gate = kzalloc(sizeof(*p100_gate), GFP_KERNEL);
	if (!p100_gate)
		return ERR_PTR(-ENOMEM);

	p100_gate->gate.reg = reg;
	p100_gate->gate.bit_idx = shift;
	p100_gate->gate.flags = 0;
	p100_gate->gate.lock = lock;
	p100_gate->gate.hw.init = &init;
	p100_gate->ops = &clk_gate_ops;
	p100_gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_p100gate_share_ops;
	init.flags = flags;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;

	hw = &p100_gate->gate.hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(p100_gate);
		return ERR_PTR(ret);
	}

	return hw->clk;
}

/*
 * Check if name is a frequency name according to the '*_frequency' pattern
 * return 0 if false
 * return length of frequency name without the _frequency
 */
static int is_frequency_name(const char *name)
{
	int strs, i;

	strs = strlen(name);
	/* string need to be at minimum len(x_frequency) */
	if (strs < 11)
		return 0;
	for (i = strs - 9; i > 0; i--) {
		/* find first '_' and check if right part is frequency */
		if (name[i] !=  '_')
			continue;
		if (strcmp(name + i + 1, "frequency") != 0)
			return 0;
		return i;
	}
	return 0;
}

static struct clk *zhihe_clk_match_clk(struct clk **clks, char *name)
{
	int i;

	for (i = 0; i < CLK_END; i++) {
		if (clks[i] == ERR_PTR(-ENOENT))
			continue;
		if (strcmp(__clk_get_name(clks[i]), name) == 0)
			return clks[i];
	}

	return NULL;
}

int zhihe_clk_of_bulk_init(struct device *dev, struct clk **clks)
{
	struct device_node *np = dev->of_node;
	struct property *prop;
	char name[64];
	int i;
	unsigned int value;
	struct clk *clk;
	int ret;

	for_each_property_of_node(np, prop) {
		i = is_frequency_name(prop->name);
		if (i == 0)
			continue;
		memcpy(name, prop->name, i);
		name[i] = '\0';
		of_property_read_u32(np, prop->name, &value);
		clk = zhihe_clk_match_clk(clks, name);
		if (clk == NULL)
			return -EINVAL;
		ret = zhihe_clk_set_round_rate(dev, clk, value);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

int p100_parse_regbase(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_clk_subsys *priv = dev_get_drvdata(dev);
	int ret = 0;

	for (int i = 0; i < priv->num_regs; i++) {
		priv->regs[i].base = devm_platform_ioremap_resource_byname(pdev, priv->regs[i].name);
		if (WARN_ON(IS_ERR(priv->regs[i].base))) {
			ret = PTR_ERR(priv->regs[i].base);
			return ret;
		}
	}

	return ret;
}

void p100_register_clock(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_clk_subsys *priv = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	unsigned int freq;
	int ret;

	/* update pll freq if defined in dts */
	for (int i = 0; i < priv->num_plls; i++) {
		ret = of_property_read_u32(np, priv->plls[i].name, &freq);
		if (ret)
			continue;
		priv->plls[i].rate_table[0].vco_rate = freq;
		priv->plls[i].rate_table[0].rate = freq / priv->plls[i].rate_table[0].postdiv1;
	}

	for (int i = 0; i < priv->num_info; i++) {
		enum p100_clk_types type = priv->info[i].type;
		char *name = priv->info[i].name;
		char *parent = priv->info[i].parent;
		u32 id = priv->info[i].id;
		void __iomem *base = priv->regs[priv->info[i].reg].base +
				     priv->info[i].shift;
		u8 width = priv->info[i].width;
		u8 bit_idx = priv->info[i].bit_idx;

		switch (type) {
			case CLK_TYPE_FIXED:
				priv->clk_data->clks[id] = zhihe_clk_fixed(name, parent, priv->info[i].fixed.freq);
				break;
			case CLK_TYPE_FIXED_FACTOR:
				priv->clk_data->clks[id] = zhihe_p100_clk_fixed_factor(name, parent,
							       priv->info[i].fixed_factor.mult, priv->info[i].fixed_factor.div);
				break;
			case CLK_TYPE_PLL:
				priv->clk_data->clks[id] = zhihe_p100_pll(name, parent, base, priv->info[i].pll);
				break;
			case CLK_TYPE_DIVIDER:
				priv->clk_data->clks[id] = zhihe_clk_p100_divider(name, parent, base, bit_idx, width,
						 priv->info[i].divider.sync, priv->info[i].divider.div_type,
						 priv->info[i].divider.min, priv->info[i].divider.max);
				break;
			case CLK_TYPE_DIVIDER_CLOSEST:
				priv->clk_data->clks[id] = zhihe_clk_p100_divider_closest(name, parent, base, bit_idx, width,
						 priv->info[i].divider.sync, priv->info[i].divider.div_type,
						 priv->info[i].divider.min, priv->info[i].divider.max);
				break;
			case CLK_TYPE_GATE:
				priv->clk_data->clks[id] = zhihe_clk_p100_gate(name, parent, base, bit_idx);
				break;
			case CLK_TYPE_GATE_SHARED:
				priv->clk_data->clks[id] = zhihe_clk_p100_gate_shared(name, parent, base, bit_idx,
									    priv->info[i].gate_shared.share_count);
				break;
			case CLK_TYPE_MUX:
				priv->clk_data->clks[id] = zhihe_p100_clk_mux_flags(name, base, bit_idx, width,
						 priv->info[i].mux.parents, priv->info[i].mux.num_parents,
						 priv->info[i].mux.flags);
				break;
			default:
				dev_err(dev, "clk register fail with wrong type=%d", type);
				break;
		}
	}
}
