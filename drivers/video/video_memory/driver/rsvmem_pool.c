/*
 * Copyright (C) 2022 Alibaba Group. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/mman.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/genalloc.h>
#include "rsvmem_pool.h"

/* 12 bits (4096 bytes) */
#define GEN_POOL_ALLOC_ORDER 12

static rsvmem_pool_info_t rsvmem_pool_regions[MAX_RSVMEM_REGION_COUNT];

int rsvmem_pool_create(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *rsvmem_node;
	struct resource res;
	int pool_id = 0;
	int ret;

	if (!dev || !dev->of_node)
		return -EINVAL;

	rsvmem_node = of_parse_phandle(np, "memory-region", 0);
	if (!rsvmem_node) {
		dev_notice(dev, "No memory region node\n");
		return -ENODEV;
	}

	while (pool_id < MAX_RSVMEM_REGION_COUNT &&
	       of_address_to_resource(rsvmem_node, pool_id, &res) == 0) {
		struct gen_pool *pool = gen_pool_create(GEN_POOL_ALLOC_ORDER, -1);
		if (pool == NULL) {
			dev_err(dev, "Failed to create reserved memory pool region[%d]\n", pool_id);
			return -ENOMEM;
		}

		ret = gen_pool_add(pool, res.start, resource_size(&res), -1);
		if (ret) {
			dev_err(dev, "%s: gen_pool_add failed\n", __func__);
			gen_pool_destroy(pool);
			return ret;
		}

		rsvmem_pool_regions[pool_id].pool = pool;
		rsvmem_pool_regions[pool_id].base = res.start;
		rsvmem_pool_regions[pool_id].size = resource_size(&res);

		dev_err(dev, "%s: rsvmem_pool_region[%d] = {pool=%px, base=0x%llx, size=0x%llx}\n",
		        __func__, pool_id, rsvmem_pool_regions[pool_id].pool,
		        rsvmem_pool_regions[pool_id].base, rsvmem_pool_regions[pool_id].size);

		pool_id ++;
	}

	return 0;
}

void rsvmem_pool_destroy(void)
{
	int i;

	for (i = 0; i < MAX_RSVMEM_REGION_COUNT; i++) {
		if (rsvmem_pool_regions[i].pool != NULL) {
			gen_pool_destroy(rsvmem_pool_regions[i].pool);
			memset(&rsvmem_pool_regions[i], 0, sizeof(rsvmem_pool_info_t));
		}
	}
}

unsigned long rsvmem_pool_alloc(int region_id, size_t size)
{
	struct gen_pool *pool;
	unsigned long addr;

	if (region_id < 0 || region_id >= MAX_RSVMEM_REGION_COUNT) {
		pr_err("%s: region_id(%d) is invalid\n", __func__, region_id);
		return 0;
	}

	pool = rsvmem_pool_regions[region_id].pool;
	if (pool == NULL) {
		pr_err("%s: pool region[%d] is invalid\n", __func__, region_id);
		return 0;
	}

	addr = gen_pool_alloc(pool, size);
	pr_debug("%s: Allocated %zu bytes from pool region[%d]: 0x%08lx\n", __func__, size, region_id, addr);

	return addr;
}

void rsvmem_pool_free(int region_id, size_t size, unsigned long addr)
{
	struct gen_pool *pool;

	if (region_id < 0 || region_id >= MAX_RSVMEM_REGION_COUNT) {
		pr_err("%s: region_id(%d) is invalid\n", __func__, region_id);
		return;
	}

	pool = rsvmem_pool_regions[region_id].pool;
	if (pool == NULL) {
		pr_err("%s: rsvmem pool region[%d] is invalid\n", __func__, region_id);
		return;
	}

	gen_pool_free(pool, addr, size);
}

