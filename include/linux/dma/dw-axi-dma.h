/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Synopsys DesignWare AXI DMA Controller driver.
 */

#ifndef __LINUX_DW_AXI_DMA_H
#define __LINUX_DW_AXI_DMA_H

#include <linux/types.h>

/**
 * struct axi_dma_peripheral_config - AXI DMA peripheral configuration
 *
 * @src_addr_incr: If true, the source address increments during DMA transfers.
 *                 If false, the source address remains fixed.
 *
 * @dst_addr_incr: If true, the destination address increments during DMA transfers.
 *                 If false, the destination address remains fixed.
 *
 * @src_fixed_burst: When source address is not incremented (src_addr_incr == false),
 *                   this field specifies the burst length for which the source
 *                   address remains fixed during a DMA transfer. A value of 0 may be
 *                   used to indicate that this setting is not applicable or undefined.
 *
 * @dst_fixed_burst: When destination address is not incremented (dst_addr_incr == false),
 *                   this field specifies the burst length for which the destination
 *                   address remains fixed during a DMA transfer. A value of 0 may be
 *                   used to indicate that this setting is not applicable or undefined.
 *
 * These fields allow configuring whether the DMA engine should use linear addressing
 * (incrementing) or static addressing for source and destination buffers, and define
 * the burst length when fixed addressing mode is used.
 */
struct axi_dma_peripheral_config {
	bool src_addr_incr;
	bool dst_addr_incr;
	u32  src_fixed_burst;
	u32  dst_fixed_burst;
};

#endif /* __LINUX_DW_AXI_DMA_H */
