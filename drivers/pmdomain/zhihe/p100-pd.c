/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <dt-bindings/iopmp/zh-iopmp.h>
#include <asm/zh-iopmp.h>

#include "p100-pd.h"

static const struct p100_pd_range p100_pd_ranges[] = {
	{"power_gpu", P100_PD_GPU},
	{"power_npu_wrapper", P100_PD_NPU_WRAPPER},
	{"power_npu_ip", P100_PD_NPU_IP},
	{"power_d2d", P100_PD_D2D},
	{"power_pcie0", P100_PD_PCIE0},
	{"power_pcie1", P100_PD_PCIE1},
	{"power_sata", P100_PD_SATA},
	{"power_usb", P100_PD_USB},
	{"power_vi_wrapper", P100_PD_VI_WRAP},
	{"power_vi_isp", P100_PD_VI_ISP},
	{"power_vo", P100_PD_VO},
	{"power_vp_wrapper", P100_PD_VP_WRAP},
	{"power_venc", P100_PD_VENC},
	{"power_vdec", P100_PD_VDEC},
	{"power_top", P100_PD_TOP},
	{"power_peri0", P100_PD_PERI0},
	{"power_peri1", P100_PD_PERI1},
	{"power_peri2", P100_PD_PERI2},
	{"power_peri3", P100_PD_PERI3},
	{"power_tee", P100_PD_TEE},
};

static struct dentry *pd_debugfs_root;
static struct dentry *pd_pde;

static inline struct p100_pm_domain *to_p100_pd(struct generic_pm_domain *domain)
{
	return container_of(domain, struct p100_pm_domain, pd);
}

static void bpc_config(struct device *dev, const char *str, void __iomem *base_addr, u32 bpc_ctrl)
{
    if ((bpc_ctrl & (1 << 0)) != 0) {
        dev_dbg(dev, "Enter %s bpc_config sw model...\n", str);
        writel(0x1, base_addr + 0x000); // 0x1 bypass
        writel(0x18, base_addr + 0x13c); // bpc 9000 ocgen &rset
    } else {
        dev_dbg(dev, "Enter %s bpc_config hw model...\n", str);
        writel(0x0, base_addr + 0x000);
    }
    writel(0x10101, base_addr + 0x004); // pwr venc bpc 3000| fence
}

static void pcu_intr(struct device *dev, const char *str, void __iomem *base_addr)
{
    u32 data;
    udelay(1);
    data = readl(base_addr + 0x2c); // read pcu intr
    while (data == 0) {
        udelay(1);
        data = readl(base_addr + 0x2c); // read pcu intr
    }
    if (((data & (1 << 0)) != 0) || ((data & (1 << 3)) != 0)) {
        dev_dbg(dev, "%s pcu_intr accept\n", str);
    }
    if (((data & (1 << 1)) != 0) || ((data & (1 << 4)) != 0)) {
        dev_err(dev, "%s pcu_intr deny\n", str);
    }
    if (((data & (1 << 2)) != 0) || ((data & (1 << 5)) != 0)) {
        dev_err(dev, "%s pcu_intr timeout\n", str);
    }
    writel(data, base_addr + 0x28); // clr cpu intr
}

static void pcu_config(struct device *dev, const char *str,
		       void __iomem * base_addr, u32 pcu_ctrl, u32 state)
{
    dev_dbg(dev, "Enter %s pcu_config intr enable...\n", str);
    writel(0x3f, base_addr + 0x24); // interrupt enable
    if ((pcu_ctrl & (1 << 0)) != 0) {
        dev_dbg(dev, "Enter %s pcu_config: pcu reg trigger...\n", str);
        writel((state & 0x1f), base_addr + 0x0c); // lpstate = power on
        writel(0x1, base_addr + 0x08); // lqreq
        pcu_intr(dev, str, base_addr); // wait for accept
    } else {
        dev_dbg(dev, "Enter %s pcu_config: wait r2p trigger...\n", str);
    }
}

static void vpss_r2p_intr(struct p100_pd_soc *soc, u32 r2p_ctrl)
{
	struct device *dev = soc->dev;
	u32 data;
	void* __iomem base = soc->base[VP_R2P];

	if ((r2p_ctrl & (1 << 0)) == 0) {
		udelay(1);
		data = readl(base + 0x1c); // rd r2p intr
		while (data == 0) {
			udelay(1);
			data = readl(base + 0x1c); // rd r2p intr
		}
		if ((data & (1 << 0)) != 0) {
			dev_dbg(dev, "vpss_r2p_intr accept\n");
		}
		if ((data & (1 << 1)) != 0) {
			dev_err(dev, "vpss_r2p_intr deny\n");
		}
		if ((data & (1 << 2)) != 0) {
			dev_err(dev, "vpss_r2p_intr timeout\n");
		}
		writel(data, base + 0x14); // clr pwr pcu intr
	} else {
		dev_dbg(dev, "vpss_r2p intr bypass: pcu reg trigger...\n");
	}
}

static void vpss_r2p_low_power_config(struct p100_pd_soc *soc, u32 r2p_ctrl, u32 pctrl_en)
{
	struct device *dev = soc->dev;
	int order;
	//srand((unsigned int)time(NULL));
	//order = rand() % 2;
	order = 0;
	dev_dbg(dev, "Enter vpss_r2p_config: intr enable...\n");
	writel(0x7, soc->base[VP_R2P] + 0x10); // r2p intr en
	if ((r2p_ctrl & (1 << 0)) == 0) {
		writel((0x00001f << 4) | ((pctrl_en & 0x7) << 1) | (order & 0x1), soc->base[VP_R2P] + 0x08); // r2p
		writel(0x01, soc->base[VP_R2P] + 0x04);
		if ((pctrl_en & 0x7) == 0x0 || (pctrl_en & 0x7) == 0x2 || (pctrl_en & 0x7) == 0x4 || (pctrl_en & 0x7) == 0x6) {
			dev_dbg(dev, "Enter vpss_r2p_config: r2p need set: trigger pwr pcu ctrl_en | pctrl_en='h%x, order='h%x\n", pctrl_en, order);
		} else if ((pctrl_en & 0x7) == 0x1) {
			dev_dbg(dev, "Enter vpss_r2p_config: pwr pcu ctrl_en, venc&vdec ctrl_dis | pctrl_en='h%x, order='h%x\n", pctrl_en, order);
			pcu_intr(dev, "vp_wrap_pctrl", soc->base[VP_WRAP_PCU]);
		} else if ((pctrl_en & 0x7) == 0x3) {
			dev_dbg(dev, "Enter vpss_r2p_config: pwr&venc pcu ctrl_en,vdec ctrl_dis | pctrl_en='h%x, order='h%x\n", pctrl_en, order);
			pcu_intr(dev, "vp_venc_pctrl", soc->base[VP_VENC_PCU]);
			pcu_intr(dev, "vp_wrap_pctrl", soc->base[VP_WRAP_PCU]);
		} else if ((pctrl_en & 0x7) == 0x5) {
			dev_dbg(dev, "Enter vpss_r2p_config: pwr&vdec pcu ctrl_en,venc ctrl_dis | pctrl_en='h%x, order='h%x\n", pctrl_en, order);
			pcu_intr(dev, "vp_vdec_pctrl", soc->base[VP_VDEC_PCU]);
			pcu_intr(dev, "vp_wrap_pctrl", soc->base[VP_WRAP_PCU]);
		} else if ((pctrl_en & 0x7) == 0x7) {
			dev_dbg(dev, "Enter vpss_r2p_config: pwr&venc&vdec pcu ctrl_en ||pctrl_en='h%x, order='h%x\n", pctrl_en, order);
			pcu_intr(dev, "vp_vdec_pctrl", soc->base[VP_VDEC_PCU]);
			pcu_intr(dev, "vp_venc_pctrl", soc->base[VP_VENC_PCU]);
			pcu_intr(dev, "vp_wrap_pctrl", soc->base[VP_WRAP_PCU]);
		}
		dev_dbg(dev, "Enter vpss_r2p_config: r2p trigger pcu...\n");
	} else {
		dev_dbg(dev, "vpss_r2p_config bypass: pcu reg trigger...\n");
	}
	vpss_r2p_intr(soc->base[VP_R2P], r2p_ctrl);
}

static int p100_ip_pd_switch(struct device *dev, const char *name, void __iomem *pca_base,
			     void __iomem *bpc_base, void __iomem *pcu_base, power_mode mode)
{
	/* usb ss can't power off because uart4(peri2) is inside it's power domain. */
	if (strcmp(name, "power_usb") == 0 && mode == OFF)
		return 0;

	/* config pca if needed */
	if (mode == ON && pca_base != NULL)
		writel(0x0, pca_base + 0x20);

	bpc_config(dev, name, bpc_base, BPC_HW_MODEL);
	pcu_config(dev, name, pcu_base, PCU_REG_TRIGGER, mode);

	return 0;
}

static int p100_pd_power_switch(struct generic_pm_domain *domain, power_mode mode)
{
	struct p100_pm_domain *p100_pd = to_p100_pd(domain);
	struct p100_pd_soc *soc = p100_pd->soc;
	struct device *dev = soc->dev;
	int ret;

	switch(p100_pd->index) {
		case P100_PD_VP_WRAP:
			ret = p100_ip_pd_switch(dev, domain->name, soc->base[VP_PCA], soc->base[VP_WRAP_BPC],
						soc->base[VP_WRAP_PCU], mode);
			vpss_r2p_low_power_config(soc, PCU_REG_TRIGGER, 0x7);
			break;
		case P100_PD_VENC:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[VP_VENC_BPC],
						soc->base[VP_VENC_PCU], mode);
			break;
		case P100_PD_VDEC:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[VP_VDEC_BPC],
						soc->base[VP_VDEC_PCU], mode);
			break;
		case P100_PD_GPU:
			ret = p100_ip_pd_switch(dev, domain->name, soc->base[GPU_PCA], soc->base[GPU_BPC],
						soc->base[GPU_PCU], mode);
			break;
		case P100_PD_NPU_WRAPPER:
			ret = p100_ip_pd_switch(dev, domain->name, soc->base[NPU_PCA], soc->base[NPU_WRAP_BPC],
						soc->base[NPU_WRAP_PCU], mode);
			break;
		case P100_PD_NPU_IP:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[NPU_IP_BPC],
						soc->base[NPU_IP_PCU], mode);
			break;
		case P100_PD_PCIE0:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[PCIE0_BPC],
						soc->base[PCIE0_PCU], mode);
			break;
		case P100_PD_PCIE1:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[PCIE1_BPC],
						soc->base[PCIE1_PCU], mode);
			break;
		case P100_PD_SATA:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[SATA_BPC],
						soc->base[SATA_PCU], mode);
			break;
		case P100_PD_VI_WRAP:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[VI_WRAP_BPC],
							soc->base[VI_WRAP_PCU], mode);
			writel(0x7, soc->base[VI_R2P] + 0x10);
			break;
		case P100_PD_VI_ISP:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[VI_ISP_BPC],
							soc->base[VI_ISP_PCU], mode);
			break;
		case P100_PD_VO:
			ret = p100_ip_pd_switch(dev, domain->name, NULL, soc->base[VO_BPC],
							soc->base[VO_PCU], mode);
			break;
		default:
			break;
	}

	return ret;
}

static int p100_pd_power_off(struct generic_pm_domain *domain)
{
	struct p100_pm_domain *p100_pd = to_p100_pd(domain);
	int ret;

#ifdef CONFIG_ZH_IOPMP
	if(p100_pd->device_count > 0)
		iopmp_disable(p100_pd->device_ids, p100_pd->device_count);
#endif

	if (p100_pd->num_clks)
		clk_bulk_disable(p100_pd->num_clks, p100_pd->clks);

	ret = reset_control_assert(p100_pd->reset);
	if (ret)
		return ret;

	return p100_pd_power_switch(domain, OFF);
}

static int p100_pd_power_on(struct generic_pm_domain *domain)
{
	struct p100_pm_domain *p100_pd = to_p100_pd(domain);
	int ret;

	ret = p100_pd_power_switch(domain, ON);
	if (ret)
		return ret;

	if (p100_pd->num_clks) {
		ret = clk_bulk_enable(p100_pd->num_clks, p100_pd->clks);
		if (ret)
			return ret;
	}

	ret = reset_control_deassert(p100_pd->reset);
	if (ret)
		return ret;

#ifdef CONFIG_ZH_IOPMP
	if(p100_pd->device_count > 0)
		iopmp_enable(p100_pd->device_ids, p100_pd->device_count);
#endif

	return 0;
}

static char *p100_pd_get_user_string(const char __user *userbuf, size_t userlen)
{
	char *buffer;

	buffer = vmalloc(userlen + 1);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(buffer, userbuf, userlen) != 0) {
		vfree(buffer);
		return ERR_PTR(-EFAULT);
	}

	pr_debug("buffer before strip linefeed = %s\n", buffer);
	/* got the string, now strip linefeed. */
	if (buffer[userlen - 1] == '\n')
		buffer[userlen - 1] = '\0';
	else
		buffer[userlen] = '\0';

	pr_debug("buffer after strip linefeed = %s\n", buffer);

	return buffer;
}

static ssize_t p100_power_domain_write(struct file *file,
		const char __user *userbuf,
		size_t userlen, loff_t *ppos)
{
	char *buffer, *start, *end;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct p100_pd_soc *soc = m->private;
	struct device *dev = soc->dev;
	struct generic_pm_domain *domain;
	char pd_name[P100_PD_NAME_SIZE];
	char pd_state[P100_PD_STATE_NAME_SIZE];
	int idx, ret;

	buffer = p100_pd_get_user_string(userbuf, userlen);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	start = skip_spaces(buffer); // skip leading space if any
	end = start;
	while (!isspace(*end) && *end != '\0') // eg: find "gpu" before a space from string "gpu on"
		end++;

	*end = '\0';
	strcpy(pd_name, start);
	dev_dbg(dev, "power domain name: %s\n", pd_name);

	/* find the target power domain */
	for (idx = 0; idx < soc->num_domains; idx++) {
		domain = &soc->domains[idx]->pd;
		dev_dbg(dev, "generic pm domain name: %s, user input name: %s, ret = %d\n",
				domain->name, pd_name, strcmp(pd_name, domain->name));
		if (strcmp(pd_name, domain->name))
			continue;
		dev_dbg(dev, "target pm power domain-%s found, index: %d\n",
					domain->name, idx);
		break;
	}
	
	if (idx == soc->num_domains) {
		dev_err(dev, "no taget power domain-%s found, idx = %d, total pd numbers = %d\n",
				pd_name, idx, soc->num_domains);
		userlen = -EINVAL;
		goto out;
	}

	end = end + 1; // end is the new start
	start = skip_spaces(end); // skip leading space if any
	end = start;
	while (!isspace(*end) && *end != '\0')
		end++;

	*end = '\0';
	strcpy(pd_state, start);
	dev_dbg(dev, "power domain target state: %s\n", pd_state);

	if (!strcmp(pd_state, "on")) {
		ret = domain->power_on(domain);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else if (!strcmp(pd_state, "off")) {
		ret = domain->power_off(domain);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else {
		dev_err(dev, "invalid power domain target state, not 'on' or 'off'\n");
		userlen = -EINVAL;
		goto out;
	}

out:
	vfree(buffer);

	return userlen;
}

static int p100_power_domain_show(struct seq_file *m, void *v)
{
	struct p100_pd_soc *soc = m->private;
	u32 count = soc->num_domains;
	int idx;

	seq_puts(m, "[Power domain name list]: ");
	for (idx = 0; idx < count; idx++)
		seq_printf(m, "%s ", soc->domains[idx]->pd.name);
	seq_puts(m, "\n");
	seq_puts(m, "[Power on  domain usage]: echo power_name on  > domain\n");
	seq_puts(m, "[Power off domain usage]: echo power_name off > domain\n");

	return 0;
}

static int p100_power_domain_open(struct inode *inode, struct file *file)
{
	struct p100_pd_soc *soc = inode->i_private;

	return single_open(file, p100_power_domain_show, soc);
}

static const struct file_operations p100_power_domain_fops = {
	.owner = THIS_MODULE,
	.write = p100_power_domain_write,
	.read = seq_read,
	.open = p100_power_domain_open,
	.llseek = generic_file_llseek,
};

static void pd_debugfs_init(struct p100_pd_soc *soc)
{
	pd_debugfs_root = debugfs_create_dir("power_domain", NULL);
	if (IS_ERR_OR_NULL(pd_debugfs_root))
		return;

	pd_pde = debugfs_create_file("domain", 0600, pd_debugfs_root,
			soc, &p100_power_domain_fops);
}

static int p100_domain_lookup(struct device *dev, const char *name)
{
	struct p100_pd_soc *pd_soc = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < pd_soc->num_ranges; i++) {
		if (strcmp(pd_soc->pd_ranges[i].name, name) == 0)
			return pd_soc->pd_ranges[i].index;
	}
	
	return 0;
}

static int p100_add_one_domain(struct device *dev, struct device_node *np)
{
	struct p100_pd_soc *pd_soc = dev_get_drvdata(dev);
	struct p100_pm_domain *p100_pd;
	int id;
	int ret;

	p100_pd = devm_kzalloc(dev, sizeof(*p100_pd), GFP_KERNEL);
	if (!p100_pd)
		return -ENOMEM;

	id = p100_domain_lookup(dev, np->name);
	if (!id) {
		goto free_mem;
	}

	p100_pd->index = id;
	p100_pd->pd.name = np->name;
	p100_pd->pd.power_off = p100_pd_power_off;
	p100_pd->pd.power_on = p100_pd_power_on;
	p100_pd->soc = pd_soc;

	ret = pm_genpd_init(&p100_pd->pd, NULL, true);
	if (ret) {
		dev_err(dev, "failed to init power domain %s index %d",
			p100_pd->pd.name, p100_pd->index);
		devm_kfree(dev, p100_pd);
		return -ENODEV;
	}

	ret = of_genpd_add_provider_simple(np, &p100_pd->pd);
	if (ret) {
		dev_err(dev, "failed to add PM domain provider for %pOFn: %d\n",
			np, ret);
		goto remove_genpd;
	}

	p100_pd->reset = of_reset_control_array_get_optional_shared(np);
	if (IS_ERR(p100_pd->reset)) {
		ret = PTR_ERR(p100_pd->reset);
		dev_err(dev, "failed to get device resets for domain:%s\n", np->name);
		goto reset_fail;
	}

	p100_pd->num_clks = of_clk_get_parent_count(np);
	if (p100_pd->num_clks) {
		p100_pd->clks = devm_kcalloc(dev, p100_pd->num_clks,
					     sizeof(*p100_pd->clks), GFP_KERNEL);
		if (!p100_pd->clks) {
			ret = -ENOMEM;
			goto reset_fail;
		}

		for (int i = 0; i < p100_pd->num_clks; i++) {
			p100_pd->clks[i].clk = of_clk_get(np, i);
			if (IS_ERR(p100_pd->clks[i].clk)) {
				ret = PTR_ERR(p100_pd->clks[i].clk);
				dev_err(dev,
					"failed to get clk at index %d: err:%d for domain:%s\n",
					i, ret, np->name);
				goto clk_fail;
			}

		}

		ret = clk_bulk_prepare(p100_pd->num_clks, p100_pd->clks);
		if (ret) {
			clk_bulk_put(p100_pd->num_clks, p100_pd->clks);
			goto clk_fail;
		}
	}

	pd_soc->domains[pd_soc->num_domains++] = p100_pd;

	dev_dbg(dev, "added PM domain %s\n", p100_pd->pd.name);

#ifdef CONFIG_ZH_IOPMP
	/* get iopmps config node */
	int device_id_count = 0;
	int count = of_count_phandle_with_args(np, "iopmps", NULL);
	for (int i = 0; i < count; i++) {
		struct device_node *iopmp_node = of_parse_phandle(np, "iopmps", i);
		if (!iopmp_node) {
			dev_err(dev, "failed to get iopmps at index %d: for domain:%s\n", i, np->name);
			ret = -EINVAL;
			goto clk_fail;
		}
		else {
			u32 device_id;
			if (of_property_read_u32(iopmp_node, "device-id", &device_id) == 0) {
				p100_pd->device_ids[device_id_count] = device_id;
				device_id_count++;
				dev_dbg(dev, "domain %pOFn iopmp node %pOFn: device id: %d\n", np, iopmp_node, device_id);
			}
		}
	}
	p100_pd->device_count = device_id_count;
#endif

	return 0;

clk_fail:
	devm_kfree(dev, p100_pd->clks);
reset_fail:
	reset_control_put(p100_pd->reset);
remove_genpd:
	pm_genpd_remove(&p100_pd->pd);
free_mem:
	devm_kfree(dev, p100_pd);
	return ret;
}

static int p100_init_pm_domains(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct of_phandle_args child_args, parent_args;
	int ret;

	for_each_child_of_node(np, child) {
		if (!of_device_is_available(child))
			continue;
		
		ret = p100_add_one_domain(dev, child);
		if (ret) {
			dev_err(dev, "failed to handle node %pOFn: %d\n",
				child, ret);
			of_node_put(child);
			return ret;
		}

		if (of_parse_phandle_with_args(child, "power-domains",
					       "#power-domain-cells",
					       0, &parent_args))
			continue;

		child_args.np = child;
		child_args.args_count = 0;

		ret = of_genpd_add_subdomain(&parent_args, &child_args);
		of_node_put(parent_args.np);
		if (ret) {
			dev_err(dev, "failed to handle subdomain node %pOFn: %d\n",
				child, ret);
			of_node_put(child);
			return ret;
		}
	}

	of_node_put(np);

	return ret;
}


static int p100_pd_parse_regbase(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_pd_soc *priv = dev_get_drvdata(dev);
	int ret = 0;

	priv->base[PMIC_CTRL] = devm_platform_ioremap_resource_byname(pdev, "AON_PMIC_CTRL");
	if (WARN_ON(IS_ERR(priv->base[PMIC_CTRL]))) {
		ret = PTR_ERR(priv->base[PMIC_CTRL]);
		return ret;
	}

	priv->base[VP_PCA] = devm_platform_ioremap_resource_byname(pdev, "VP_PCA");
	if (WARN_ON(IS_ERR(priv->base[VP_PCA]))) {
		ret = PTR_ERR(priv->base[VP_PCA]);
		return ret;
	}

	priv->base[VP_WRAP_BPC] = devm_platform_ioremap_resource_byname(pdev, "VP_WRAP_BPC");
	if (WARN_ON(IS_ERR(priv->base[VP_WRAP_BPC]))) {
		ret = PTR_ERR(priv->base[VP_WRAP_BPC]);
		return ret;
	}

	priv->base[VP_WRAP_PCU] = devm_platform_ioremap_resource_byname(pdev, "VP_WRAP_PCU");
	if (WARN_ON(IS_ERR(priv->base[VP_WRAP_PCU]))) {
		ret = PTR_ERR(priv->base[VP_WRAP_PCU]);
		return ret;
	}

	priv->base[VP_R2P] = devm_platform_ioremap_resource_byname(pdev, "VP_R2P");
	if (WARN_ON(IS_ERR(priv->base[VP_R2P]))) {
		ret = PTR_ERR(priv->base[VP_R2P]);
		return ret;
	}

	priv->base[VP_VENC_BPC] = devm_platform_ioremap_resource_byname(pdev, "VP_VENC_BPC");
	if (WARN_ON(IS_ERR(priv->base[VP_VENC_BPC]))) {
		ret = PTR_ERR(priv->base[VP_VENC_BPC]);
		return ret;
	}

	priv->base[VP_VENC_PCU] = devm_platform_ioremap_resource_byname(pdev, "VP_VENC_PCU");
	if (WARN_ON(IS_ERR(priv->base[VP_VENC_PCU]))) {
		ret = PTR_ERR(priv->base[VP_VENC_PCU]);
		return ret;
	}

	priv->base[VP_VDEC_BPC] = devm_platform_ioremap_resource_byname(pdev, "VP_VDEC_BPC");
	if (WARN_ON(IS_ERR(priv->base[VP_VDEC_BPC]))) {
		ret = PTR_ERR(priv->base[VP_VDEC_BPC]);
		return ret;
	}

	priv->base[VP_VDEC_PCU] = devm_platform_ioremap_resource_byname(pdev, "VP_VDEC_PCU");
	if (WARN_ON(IS_ERR(priv->base[VP_VDEC_PCU]))) {
		ret = PTR_ERR(priv->base[VP_VDEC_PCU]);
		return ret;
	}

	priv->base[GPU_PCA] = devm_platform_ioremap_resource_byname(pdev, "GPU_PCA");
	if (WARN_ON(IS_ERR(priv->base[GPU_PCA]))) {
		ret = PTR_ERR(priv->base[GPU_PCA]);
		return ret;
	}

	priv->base[GPU_BPC] = devm_platform_ioremap_resource_byname(pdev, "GPU_BPC");
	if (WARN_ON(IS_ERR(priv->base[GPU_BPC]))) {
		ret = PTR_ERR(priv->base[GPU_BPC]);
		return ret;
	}

	priv->base[GPU_PCU] = devm_platform_ioremap_resource_byname(pdev, "GPU_PCU");
	if (WARN_ON(IS_ERR(priv->base[GPU_PCU]))) {
		ret = PTR_ERR(priv->base[GPU_PCU]);
		return ret;
	}

	priv->base[NPU_PCA] = devm_platform_ioremap_resource_byname(pdev, "NPU_PCA");
	if (WARN_ON(IS_ERR(priv->base[NPU_PCA]))) {
		ret = PTR_ERR(priv->base[NPU_PCA]);
		return ret;
	}

	priv->base[NPU_WRAP_BPC] = devm_platform_ioremap_resource_byname(pdev, "NPU_WRAP_BPC");
	if (WARN_ON(IS_ERR(priv->base[NPU_WRAP_BPC]))) {
		ret = PTR_ERR(priv->base[NPU_WRAP_BPC]);
		return ret;
	}

	priv->base[NPU_WRAP_PCU] = devm_platform_ioremap_resource_byname(pdev, "NPU_WRAP_PCU");
	if (WARN_ON(IS_ERR(priv->base[NPU_WRAP_PCU]))) {
		ret = PTR_ERR(priv->base[NPU_WRAP_PCU]);
		return ret;
	}

	priv->base[NPU_IP_BPC] = devm_platform_ioremap_resource_byname(pdev, "NPU_IP_BPC");
	if (WARN_ON(IS_ERR(priv->base[NPU_IP_BPC]))) {
		ret = PTR_ERR(priv->base[NPU_IP_BPC]);
		return ret;
	}

	priv->base[NPU_IP_PCU] = devm_platform_ioremap_resource_byname(pdev, "NPU_IP_PCU");
	if (WARN_ON(IS_ERR(priv->base[NPU_IP_PCU]))) {
		ret = PTR_ERR(priv->base[NPU_IP_PCU]);
		return ret;
	}

	priv->base[PCIE0_BPC] = devm_platform_ioremap_resource_byname(pdev, "PCIE0_BPC");
	if (WARN_ON(IS_ERR(priv->base[PCIE0_BPC]))) {
		ret = PTR_ERR(priv->base[PCIE0_BPC]);
		return ret;
	}

	priv->base[PCIE0_PCU] = devm_platform_ioremap_resource_byname(pdev, "PCIE0_PCU");
	if (WARN_ON(IS_ERR(priv->base[PCIE0_PCU]))) {
		ret = PTR_ERR(priv->base[PCIE0_PCU]);
		return ret;
	}

	priv->base[PCIE1_BPC] = devm_platform_ioremap_resource_byname(pdev, "PCIE1_BPC");
	if (WARN_ON(IS_ERR(priv->base[PCIE1_BPC]))) {
		ret = PTR_ERR(priv->base[PCIE1_BPC]);
		return ret;
	}

	priv->base[PCIE1_PCU] = devm_platform_ioremap_resource_byname(pdev, "PCIE1_PCU");
	if (WARN_ON(IS_ERR(priv->base[PCIE1_PCU]))) {
		ret = PTR_ERR(priv->base[PCIE1_PCU]);
		return ret;
	}

	priv->base[SATA_BPC] = devm_platform_ioremap_resource_byname(pdev, "SATA_BPC");
	if (WARN_ON(IS_ERR(priv->base[SATA_BPC]))) {
		ret = PTR_ERR(priv->base[SATA_BPC]);
		return ret;
	}

	priv->base[SATA_PCU] = devm_platform_ioremap_resource_byname(pdev, "SATA_PCU");
	if (WARN_ON(IS_ERR(priv->base[SATA_PCU]))) {
		ret = PTR_ERR(priv->base[SATA_PCU]);
		return ret;
	}

	priv->base[USB_BPC] = devm_platform_ioremap_resource_byname(pdev, "USB_BPC");
	if (WARN_ON(IS_ERR(priv->base[USB_BPC]))) {
		ret = PTR_ERR(priv->base[USB_BPC]);
		return ret;
	}

	priv->base[USB_PCU] = devm_platform_ioremap_resource_byname(pdev, "USB_PCU");
	if (WARN_ON(IS_ERR(priv->base[USB_PCU]))) {
		ret = PTR_ERR(priv->base[USB_PCU]);
		return ret;
	}

	priv->base[VI_R2P] = devm_platform_ioremap_resource_byname(pdev, "VI_R2P");
	if (WARN_ON(IS_ERR(priv->base[VI_R2P]))) {
		ret = PTR_ERR(priv->base[VI_R2P]);
		return ret;
	}

	priv->base[VI_WRAP_BPC] = devm_platform_ioremap_resource_byname(pdev, "VI_WRAP_BPC");
	if (WARN_ON(IS_ERR(priv->base[VI_WRAP_BPC]))) {
		ret = PTR_ERR(priv->base[VI_WRAP_BPC]);
		return ret;
	}

	priv->base[VI_WRAP_PCU] = devm_platform_ioremap_resource_byname(pdev, "VI_WRAP_PCU");
	if (WARN_ON(IS_ERR(priv->base[VI_WRAP_PCU]))) {
		ret = PTR_ERR(priv->base[VI_WRAP_PCU]);
		return ret;
	}

	priv->base[VI_ISP_BPC] = devm_platform_ioremap_resource_byname(pdev, "VI_ISP_BPC");
	if (WARN_ON(IS_ERR(priv->base[VI_ISP_BPC]))) {
		ret = PTR_ERR(priv->base[VI_ISP_BPC]);
		return ret;
	}

	priv->base[VI_ISP_PCU] = devm_platform_ioremap_resource_byname(pdev, "VI_ISP_PCU");
	if (WARN_ON(IS_ERR(priv->base[VI_ISP_PCU]))) {
		ret = PTR_ERR(priv->base[VI_ISP_PCU]);
		return ret;
	}

	priv->base[VO_BPC] = devm_platform_ioremap_resource_byname(pdev, "VO_BPC");
	if (WARN_ON(IS_ERR(priv->base[VO_BPC]))) {
		ret = PTR_ERR(priv->base[VO_BPC]);
		return ret;
	}

	priv->base[VO_PCU] = devm_platform_ioremap_resource_byname(pdev, "VO_PCU");
	if (WARN_ON(IS_ERR(priv->base[VO_PCU]))) {
		ret = PTR_ERR(priv->base[VO_PCU]);
		return ret;
	}

	return ret;
}

static void p100_init_pmic_ctrl(struct device *dev)
{
	struct p100_pd_soc *pd_soc = dev_get_drvdata(dev);

	writel(readl(pd_soc->base[PMIC_CTRL] + 0x50) | 24, pd_soc->base[PMIC_CTRL] + 0x50);
	writel(AON_AON_I2C0_BADDR + 0x04, pd_soc->base[PMIC_CTRL] + 0x5c);
	writel(AON_AON_I2C0_BADDR + 0x10, pd_soc->base[PMIC_CTRL] + 0x58);
}

static int p100_pd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_pd_soc *pd_soc;
	int ret;

	pd_soc = devm_kzalloc(dev, sizeof(*pd_soc), GFP_KERNEL);
	if (!pd_soc)
		return -ENOMEM;
	dev_set_drvdata(dev, pd_soc);

	pd_soc->pd_ranges = p100_pd_ranges;
	pd_soc->num_ranges = ARRAY_SIZE(p100_pd_ranges);
	pd_soc->dev = dev;

	ret = p100_pd_parse_regbase(pdev);
	if (ret)
		return ret;

	ret = p100_init_pm_domains(dev);
	if (ret)
		return ret;

	p100_init_pmic_ctrl(dev);

	pd_debugfs_init(pd_soc);

	pr_cont("Registered p100 power domain:");
	for (int i = 0; i < pd_soc->num_domains; i++) {
		if (pd_soc->domains[i]) {
			pr_cont(" %s", pd_soc->domains[i]->pd.name);
		}
	}
	pr_cont("\n");

	return ret;
}

static const struct of_device_id p100_pd_of_match[] = {
	{ .compatible = "zhihe,p100-power-domain"},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, p100_pd_of_match);

static struct platform_driver p100_pd_driver = {
	.probe = p100_pd_probe,
	.driver = {
		.name = "p100-power-domain",
		.of_match_table = of_match_ptr(p100_pd_of_match),
	},
};

builtin_platform_driver(p100_pd_driver);

MODULE_AUTHOR("dong.yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe P100 power domain driver");
MODULE_LICENSE("GPL v2");
