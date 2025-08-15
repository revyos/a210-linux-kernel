// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V IOMMU as a platform device
 *
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Author: Nick Kossifidis <mick@ics.forth.gr>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/bitfield.h>

#include "iommu-bits.h"
#include "iommu.h"

#define P100_IOMMU_CAP 0x28d0008210

static int riscv_iommu_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct riscv_iommu_device *iommu = NULL;
	struct resource *res = NULL;
	u32 fctl = 0;
	int irq = 0;
	int ret = 0;

	iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	iommu->dev = dev;
	dev_set_drvdata(dev, iommu);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "could not find resource for register region\n");
		return -EINVAL;
	}

	iommu->reg = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(iommu->reg)) {
		ret = dev_err_probe(dev, PTR_ERR(iommu->reg),
				    "could not map register region\n");
		goto fail;
	}

	iommu->reg_phys = res->start;

	ret = -ENODEV;

	/* Sanity check: Did we get the whole register space ? */
	if ((res->end - res->start + 1) < RISCV_IOMMU_REG_SIZE) {
		dev_err(dev, "device region smaller than register file (0x%llx)\n",
			res->end - res->start);
		goto fail;
	}

	iommu->cap = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CAP);
	if (iommu->cap != P100_IOMMU_CAP) {
		dev_err_probe(dev, -ENODEV,
			"IOMMU Capacity Reg=0x%llx Error.Check clock power reset!\n", iommu->cap);
		goto fail;
	}

	/* For now we only support WSIs until we have AIA support */
	ret = FIELD_GET(RISCV_IOMMU_CAP_IGS, iommu->cap);
	if (ret == RISCV_IOMMU_CAP_IGS_MSI) {
		dev_err(dev, "IOMMU only supports MSIs\n");
		goto fail;
	}

	/* Parse IRQ assignment */
	irq = platform_get_irq(pdev, 0);
	if (irq > 0)
		iommu->irq = irq;
	else {
		dev_err(dev, "no IRQ provided for iommu\n");
		goto fail;
	}

	/* Make sure fctl.WSI is set */
	fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);
	fctl |= RISCV_IOMMU_FCTL_WSI;
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL, fctl);

	/* Parse Queue lengts */
	ret = of_property_read_u32(pdev->dev.of_node, "cmdq_len", &iommu->cmdq_len);
	if (!ret)
		dev_info(dev, "command queue length set to %i\n", iommu->cmdq_len);

	ret = of_property_read_u32(pdev->dev.of_node, "fltq_len", &iommu->fltq_len);
	if (!ret)
		dev_info(dev, "fault/event queue length set to %i\n", iommu->fltq_len);

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	ret = riscv_iommu_init(iommu);

	//clear tlb
	if (!ret) {
		struct riscv_iommu_domain iommu_domain;
		iommu_domain.iommu = iommu;
		iommu_domain.pscid = 0;
		iommu_domain.domain.ops = iommu->iommu.ops->default_domain_ops;
		iommu_flush_iotlb_all(&iommu_domain.domain);
	}

 fail:
	/* Note: devres_release_all() will release iommu and iommu->reg */
	return ret;
};

static void riscv_iommu_platform_remove(struct platform_device *pdev)
{
	riscv_iommu_remove(dev_get_drvdata(&pdev->dev));
}

static void riscv_iommu_platform_shutdown(struct platform_device *pdev)
{
	return;
}

static const struct of_device_id riscv_iommu_of_match[] = {
	{.compatible = "riscv,iommu",},
	{},
};

MODULE_DEVICE_TABLE(of, riscv_iommu_of_match);

static struct platform_driver riscv_iommu_platform_driver = {
	.driver = {
		   .name = "riscv,iommu",
		   .of_match_table = riscv_iommu_of_match,
		   .suppress_bind_attrs = true,
		   },
	.probe = riscv_iommu_platform_probe,
	.remove_new = riscv_iommu_platform_remove,
	.shutdown = riscv_iommu_platform_shutdown,
};

module_driver(riscv_iommu_platform_driver, platform_driver_register,
	      platform_driver_unregister);
