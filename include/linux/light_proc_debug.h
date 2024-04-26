// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#ifndef __LIGHT_PROC_DEBUG_H_
#define __LIGHT_PROC_DEBUG_H_


void *light_create_panic_log_proc(phys_addr_t log_phy, void *dir, void *log_addr, size_t size);
void light_remove_panic_log_proc(void *arg);

#endif


