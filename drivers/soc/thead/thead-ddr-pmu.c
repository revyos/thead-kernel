// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(C) 2021 Alibaba Communications Inc.
 * Author: David Li <liyong.li@alibaba-inc.com>
 */

//#define DEBUG

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/debugfs.h>

#define PMU_CTRL		0x0
#define PMU_DURA_THRESHOLD	0x4
#define PMU_MST_ID		0x8
#define PMU_MON_PERIOD		0xC
#define PMU_FLT_CTRL		0x10
#define PMU_FLT_LEN		0x14
#define PMU_FLT_E_ADDR0		0x18
#define PMU_FLT_E_ADDR1		0x1C
#define PMU_FLT_E_ADDR2		0x20
#define PMU_FLT_E_ADDR3		0x24
#define PMU_FLT_CMD		0x28
#define PMU_FLT_S_ADDR0		0x2C
#define PMU_FLT_S_ADDR1		0x30
#define PMU_FLT_S_ADDR2		0x34
#define PMU_FLT_S_ADDR3		0x38
#define PMU_TARGET_WDATA	0x3C
#define PMU_READ		0x40
#define PMU_STS_REG_BASE	0x40
#define PMU_RD_STS0		0x40
#define PMU_RD_STS1		0x44
#define PMU_RD_STS2		0x48
#define PMU_RD_STS3		0x4C
#define PMU_WR_STS0		0x50
#define PMU_WR_STS1		0x54
#define PMU_WR_STS2		0x58
#define PMU_WR_STS3		0x5C
#define PMU_INT_REG		0x80
#define PMU_ERR_RESP_ID		0x84
#define PMU_VERSION0		0x88
#define PMU_VERSION1		0x8C
#define PMU_OSTD_STS		0x90
#define PMU_TARGET_ADDR		0x98
#define PMU_RD_STS4		0xA0
#define PMU_WR_STS4		0xA4

#define PMU_RESET		(1 << 0)
#define PMU_EN			(1 << 1)
#define PMU_SRC_SEL_MASK	(0x1f << 3)
#define SRC_ADDR_RANGE_HIT	(1 << 3)
#define SRC_PERIOD_EXPIRED	(1 << 4)
#define SRC_TARGET_WDATA	(1 << 5)
#define SRC_ERROR_RESP		(1 << 6)
#define SRC_CNT_OVERFLOW	(1 << 7)
#define PMU_TRIG_MODE_MASK	(1 << 8)

#define PMU_DURA_THRESHOLD_W_SHIFT 0
#define PMU_DURA_THRESHOLD_W_MASK  (0xffff << PMU_DURA_THRESHOLD_W_SHIFT)
#define PMU_DURA_THRESHOLD_R_SHIFT 16
#define PMU_DURA_THRESHOLD_R_MASK  (0xffff << PMU_DURA_THRESHOLD_R_SHIFT)

#define PERIOD_MODE		(0 << 8)
#define SINGLE_MODE		(1 << 8)

#define AXID_SHIFT		16
#define AXID_EN			(1 << 0)
#define AXID_MASK_SHIFT		0
#define AXID_MASK		(0xffff << AXID_MASK_SHIFT)

// VRD0 and VWR0 share one configuration
// VRD1 and VWR1 share one configuration
#define ALIGN_FILTER_CNT0_EN	(1 << 0)
#define ALIGN_FILTER_CNT1_EN	(1 << 1)
#define ADDR_FILTER_CNT0_EN	(1 << 4)
#define ADDR_FILTER_CNT1_EN	(1 << 5)
#define SIZE_FILTER_CNT0_EN	(1 << 8)
#define SIZE_FILTER_CNT1_EN	(1 << 9)
#define LEN_FILTER_CNT0_EN	(1 << 12)
#define LEN_FILTER_CNT1_EN	(1 << 13)

#define SIZE_FLT_CNT0_SHIFT	16
#define SIZE_FLT_CNT0_MASK	(7 << SIZE_FLT_CNT0_SHIFT)
#define SIZE_FLT_CNT1_SHIFT	19
#define SIZE_FLT_CNT1_MASK	(7 << SIZE_FLT_CNT1_SHIFT)

#define LEN_FLT_CNT0_SHIFT	0
#define LEN_FLT_CNT0_MASK	(0xff << LEN_FLT_CNT0_SHIFT)
#define LEN_FLT_CNT1_SHIFT	8
#define LEN_FLT_CNT1_MASK	(0xff << LEN_FLT_CNT1_SHIFT)

#define ALIGN_FLT_CNT0_SHIFT	16
#define ALIGN_FLT_CNT0_MASK	(0xf << ALIGN_FLT_CNT0_SHIFT)
#define ALIGN_FLT_CNT1_SHIFT	20
#define ALIGN_FLT_CNT1_MASK	(0xf << ALIGN_FLT_CNT1_SHIFT)

#define PMU_IRQ_SRC_SHIFT	5
#define PMU_IRQ_SRC_MASK	(0x3ff << PMU_IRQ_SRC_SHIFT)
#define WDATA_SEL_SHIFT		1
#define WDATA_SEL_MASK		(0xf << WDATA_SEL_SHIFT)
#define PMU_CLEAR_INT		(1 << 0)

#define IRQ_TARGET_ADDR_SHIFT	16
#define IRQ_TARGET_ADDR_MASK	(0x3 << IRQ_TARGET_ADDR_SHIFT)

// IRQ status define
#define IRQ_SRC_TIME_EXPIRED		(1 << 0)
#define IRQ_SRC_TARGET_DATA_OCCUR 	(1 << 1)
#define IRQ_SRC_WRITE_ERROR_RESP_OCCUR 	(1 << 2)
#define IRQ_SRC_WRITE_DURATION_FIFOFULL	(1 << 3)
#define IRQ_SRC_WRITE_DURATION_CNT_FULL	(1 << 4)
#define IRQ_SRC_WRITE_TRANS_NOT_FINISH 	(1 << 5)
#define IRQ_SRC_READ_DURATION_FIFOFULL	(1 << 6)
#define IRQ_SRC_READ_DURATION_CNT_FULL	(1 << 7)
#define IRQ_SRC_READ_TRANS_NOT_FINISH 	(1 << 8)
#define IRQ_SRC_READ_ERROR_RESP_OCCUR 	(1 << 9)
#define IRQ_SRC_COMBINE_SHIFT		10
#define IRQ_SRC_TARGET_ADDR_R_OCCUR 	(1 << IRQ_SRC_COMBINE_SHIFT)
#define IRQ_SRC_TARGET_ADDR_W_OCCUR 	(1 << (IRQ_SRC_COMBINE_SHIFT+1))

#define WRITE_ERR_RESP_ID_SHIFT	0
#define WRITE_ERR_RESP_ID_MASK	(0xffff << WRITE_ERR_RESP_ID_SHIFT)
#define READ_ERR_RESP_ID_SHIFT	16
#define READ_ERR_RESP_ID_MASK	(0xffff << READ_ERR_RESP_ID_SHIFT)

#define RD_MAX_OSTD_SHIFT	8
#define RD_MAX_OSTD_MASK	(0xff << RD_MAX_OSTD_SHIFT)
#define WR_MAX_OSTD_SHIFT	24
#define WR_MAX_OSTD_MASK	(0xff << WR_MAX_OSTD_SHIFT)

// perf events define
// event_num = (event_offset_reg - PMU_STS_REG_BASE) / 4
#define DDR_EVENT_READ_DURATION_CNT	0
#define DDR_EVENT_READ_TRANS_CNT	1
#define DDR_EVENT_READ_BYTES		2
#define DDR_EVENT_READ_DURATION_OVER_THRESH	3
#define DDR_EVENT_WRITE_DURATION_CNT	4
#define DDR_EVENT_WRITE_TRANS_CNT	5
#define DDR_EVENT_WRITE_BYTES		6
#define DDR_EVENT_WRITE_DURATION_OVER_THRESH	7
#define DDR_EVENT_VRD0_TRANS_CNT	8
#define DDR_EVENT_VRD1_TRANS_CNT	9
#define DDR_EVENT_DUMMY1		0xa
#define DDR_EVENT_DUMMY2		0xb
#define DDR_EVENT_VWR0_TRANS_CNT	0xc
#define DDR_EVENT_VWR1_TRANS_CNT	0xd
#define DDR_EVENT_DUMMY3		0xe
#define DDR_EVENT_DUMMY4		0xf
#define DDR_EVENT_DUMMY5		0x10
#define DDR_EVENT_DUMMY6		0x11
#define DDR_EVENT_DUMMY7		0x12
#define DDR_EVENT_DUMMY8		0x13
#define DDR_EVENT_RD_MAX_OSTD		0x14
#define DDR_EVENT_WR_MAX_OSTD		0x15
#define DDR_EVENT_DUMMY9		0x16
#define DDR_EVENT_DUMMY10		0x17
#define DDR_EVENT_RD_DLY_CNT		0x18
#define DDR_EVENT_WR_DLY_CNT		0x19



#define DDR_EVENT_MASK			0x1f

#define DDR_EVENT_AXID_MASK		0x20
#define DDR_EVENT_AXID_READ_DURATION_CNT (DDR_EVENT_AXID_MASK \
					| DDR_EVENT_READ_DURATION_CNT)
#define DDR_EVENT_AXID_READ_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_READ_TRANS_CNT)
#define DDR_EVENT_AXID_READ_BYTES	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_READ_BYTES)
#define DDR_EVENT_AXID_READ_DURATION_OVER_THRESH (DDR_EVENT_AXID_MASK \
					| DDR_EVENT_READ_DURATION_OVER_THRESH)
#define DDR_EVENT_AXID_WRITE_DURATION_CNT (DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WRITE_DURATION_CNT)
#define DDR_EVENT_AXID_WRITE_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WRITE_TRANS_CNT)
#define DDR_EVENT_AXID_WRITE_BYTES	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WRITE_BYTES)
#define DDR_EVENT_AXID_WRITE_DURATION_OVER_THRESH (DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WRITE_DURATION_OVER_THRESH)
#define DDR_EVENT_AXID_VRD0_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_VRD0_TRANS_CNT)
#define DDR_EVENT_AXID_VRD1_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_VRD1_TRANS_CNT)
#define DDR_EVENT_AXID_VWR0_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_VWR0_TRANS_CNT)
#define DDR_EVENT_AXID_VWR1_TRANS_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_VWR1_TRANS_CNT)
#define DDR_EVENT_AXID_RD_MAX_OSTD	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_RD_MAX_OSTD)
#define DDR_EVENT_AXID_WR_MAX_OSTD	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WR_MAX_OSTD)
#define DDR_EVENT_AXID_RD_DLY_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_RD_DLY_CNT)
#define DDR_EVENT_AXID_WR_DLY_CNT	(DDR_EVENT_AXID_MASK \
					| DDR_EVENT_WR_DLY_CNT)

#define DDR_EVENT_MISC_MASK		0x40
#define DDR_EVENT_PMU_EXEC_TIME		(DDR_EVENT_MISC_MASK \
					| 0x0)
#define DDR_EVENT_QUARY_TOTAL_BW	(DDR_EVENT_MISC_MASK \
					| 0x1)

#define DDR_EVENT_CAPTURE_MASK		0x60
#define DDR_EVENT_CAPTURE_R_DATA	(DDR_EVENT_CAPTURE_MASK \
					| 0x0)
#define DDR_EVENT_CAPTURE_W_DATA	(DDR_EVENT_CAPTURE_MASK \
					| 0x1)
#define DDR_EVENT_CAPTURE_ADDR		(DDR_EVENT_CAPTURE_MASK \
					| 0x2)
#define DDR_EVENT_CAPTURE_ERROR_RESP_W	(DDR_EVENT_CAPTURE_MASK \
					| 0x3)
#define DDR_EVENT_CAPTURE_ERROR_RESP_R	(DDR_EVENT_CAPTURE_MASK \
					| 0x4)

#define DDR_EVENT_FILTER_MASK		(DDR_EVENT_CAPTURE_MASK \
					| DDR_EVENT_MISC_MASK \
					| DDR_EVENT_AXID_MASK \
					)
#define NUM_TIME_EXPIRED_EVENTS		(DDR_EVENT_CAPTURE_MASK-1)
#define NUM_EVENTS		0x65
#define NUM_INST		5
#define INST_ALL		-1
#define INST_MISC		-2
#define INST_NULL		-3

// # address hit compare mode: 0: write; 1: read
#define CM_WRITE		0
#define CM_READ			1

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME	"light_ddr"
#define DDR_CPUHP_CB_NAME	DDR_PERF_DEV_NAME "_perf_pmu"

static DEFINE_IDA(ddr_ida);

#define APB_CLK (250*1000*1000) // 250MHz
#define PMU_PERIOD_CNT 10 // 10ms in default

#define TRIGGER_MODE PERIOD_MODE
//#define TRIGGER_MODE SINGLE_MODE

#define ADDRMSB 34

#define FMT_HEX		0
#define FMT_DECIMAL	1

#define CLK_1M (1024*1024)
static long DDR_MT_3733 = (long)3733*CLK_1M;
static long DDR_MT_3200 = (long)3200*CLK_1M;

#define DDR_BITWIDTH_32 (32)
#define DDR_BITWIDTH_64 (64)
#define DDR_BITWIDTH DDR_BITWIDTH_64

static const struct of_device_id light_ddr_pmu_dt_ids[] = {
	{ .compatible = "thead,light-ddr-pmu", },
	{}
};
MODULE_DEVICE_TABLE(of, light_ddr_pmu_dt_ids);

struct iomem_base {
	void __iomem *base;
	bool enable;
};

struct ddr_pmu {
	struct pmu pmu;
	struct iomem_base pmu_base[NUM_INST];
	unsigned int cpu;
	struct	hlist_node node;
	struct	device *dev;
	struct perf_event *events[NUM_INST][NUM_EVENTS];
	int active_events;
	int hwc_active_events[NUM_INST];
	int period;
	int freq_khz;
	struct timespec64 t_begin;
	int irq;
	int id;
	u64 hwc_version[NUM_INST];
	int inst_id;
	int axi_id;
	int axi_mask;
};

// # for debugfs configuration
struct ddr_pmu_trace {
	unsigned int trace_enable; // 0: disable; 1: enable
	unsigned int trace_period_ms; // should no less than pmu->period
	unsigned int trace_count; // trace count
	unsigned int trace_mode; // 0: uart log; 1: to ddr reserved memory
	unsigned int trace_data_fmt; // 0: hex; 1: decimal
};
static struct ddr_pmu_trace pmu_trace = {0};

struct trace_point {
	unsigned int reg_offset;
	unsigned int value;
};

static ssize_t
ddr_pmu_event_show(struct device *dev, struct device_attribute *attr,
		   char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define LIGHT_DDR_PMU_EVENT_ATTR(_name, _id)				\
	(&((struct perf_pmu_events_attr[]) {				\
		{ .attr = __ATTR(_name, 0444, ddr_pmu_event_show, NULL),\
		  .id = _id, }						\
	})[0].attr.attr)

static struct attribute *ddr_perf_events_attrs[] = {
	LIGHT_DDR_PMU_EVENT_ATTR(rd_duration_cnt, DDR_EVENT_READ_DURATION_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(rd_trans_cnt, DDR_EVENT_READ_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(rd_byte_cnt, DDR_EVENT_READ_BYTES),
	LIGHT_DDR_PMU_EVENT_ATTR(rd_duration_cnt_over_threshold,
			DDR_EVENT_READ_DURATION_OVER_THRESH),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_duration_cnt, DDR_EVENT_WRITE_DURATION_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_trans_cnt, DDR_EVENT_WRITE_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_byte_cnt, DDR_EVENT_WRITE_BYTES),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_duration_cnt_over_threshold,
			DDR_EVENT_WRITE_DURATION_OVER_THRESH),
	LIGHT_DDR_PMU_EVENT_ATTR(vrd0_trans_cnt, DDR_EVENT_VRD0_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(vrd1_trans_cnt, DDR_EVENT_VRD1_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(vwr0_trans_cnt, DDR_EVENT_VWR0_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(vwr1_trans_cnt, DDR_EVENT_VWR1_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(rd_max_ostd, DDR_EVENT_RD_MAX_OSTD),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_max_ostd, DDR_EVENT_WR_MAX_OSTD),
	LIGHT_DDR_PMU_EVENT_ATTR(rd_dly_cnt, DDR_EVENT_RD_DLY_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(wr_dly_cnt, DDR_EVENT_WR_DLY_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_duration_cnt,
			DDR_EVENT_AXID_READ_DURATION_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_trans_cnt,
			DDR_EVENT_AXID_READ_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_bytes, DDR_EVENT_AXID_READ_BYTES),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_duration_cnt_over_threshold,
			DDR_EVENT_AXID_READ_DURATION_OVER_THRESH),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_duration_cnt,
			DDR_EVENT_AXID_WRITE_DURATION_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_trans_cnt,
			DDR_EVENT_AXID_WRITE_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_bytes, DDR_EVENT_AXID_WRITE_BYTES),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_duration_cnt_over_threshold,
			DDR_EVENT_AXID_WRITE_DURATION_OVER_THRESH),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_vrd0_trans_cnt,
			DDR_EVENT_AXID_VRD0_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_vrd1_trans_cnt,
			DDR_EVENT_AXID_VRD1_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_vwr0_trans_cnt,
			DDR_EVENT_AXID_VWR0_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_vwr1_trans_cnt,
			DDR_EVENT_AXID_VWR1_TRANS_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_max_ostd,
			DDR_EVENT_AXID_RD_MAX_OSTD),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_max_ostd,
			DDR_EVENT_AXID_WR_MAX_OSTD),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_rd_dly_cnt,
			DDR_EVENT_AXID_RD_DLY_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(axid_wr_dly_cnt,
			DDR_EVENT_AXID_WR_DLY_CNT),
	LIGHT_DDR_PMU_EVENT_ATTR(pmu_exec_time,
			DDR_EVENT_PMU_EXEC_TIME),
	LIGHT_DDR_PMU_EVENT_ATTR(total_bw,
			DDR_EVENT_QUARY_TOTAL_BW),
	LIGHT_DDR_PMU_EVENT_ATTR(capture_rdata,
			DDR_EVENT_CAPTURE_R_DATA),
	LIGHT_DDR_PMU_EVENT_ATTR(capture_wdata,
			DDR_EVENT_CAPTURE_W_DATA),
	LIGHT_DDR_PMU_EVENT_ATTR(capture_addr,
			DDR_EVENT_CAPTURE_ADDR),
	LIGHT_DDR_PMU_EVENT_ATTR(capture_error_resp_w,
			DDR_EVENT_CAPTURE_ERROR_RESP_W),
	LIGHT_DDR_PMU_EVENT_ATTR(capture_error_resp_r,
			DDR_EVENT_CAPTURE_ERROR_RESP_R),
	NULL,
};

static struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

#define ATTR_EVENT_MASK 0xffUL
#define ATTR_INST_SHIFT 8
#define ATTR_INST_MASK	(0xffUL << ATTR_INST_SHIFT)
#define ATTR_CHN_EN_SHIFT 8
#define ATTR_CHN_EN_MASK	(0x1fUL << ATTR_CHN_EN_SHIFT)
#define ATTR_AXID_SHIFT 0
#define ATTR_AXID_MASK (0xffffffffUL << ATTR_AXID_SHIFT)
#define ATTR_DURA_THRESHOLD_R_SHIFT 48
#define ATTR_DURA_THRESHOLD_R_MASK (0xffffUL << ATTR_DURA_THRESHOLD_R_SHIFT)
#define ATTR_DURA_THRESHOLD_W_SHIFT 32
#define ATTR_DURA_THRESHOLD_W_MASK (0xffffUL << ATTR_DURA_THRESHOLD_W_SHIFT)
#define ATTR_FILTER_START_ADDR_SHIFT 32
#define ATTR_FILTER_START_ADDR_MASK (0xffffffffUL \
					<< ATTR_FILTER_START_ADDR_SHIFT)
#define ATTR_FILTER_END_ADDR_SHIFT 0
#define ATTR_FILTER_END_ADDR_MASK (0xffffffffUL << ATTR_FILTER_END_ADDR_SHIFT)
#define ATTR_FILTER_SIZE_SHIFT 16
#define ATTR_FILTER_SIZE_MASK (0xffUL << ATTR_FILTER_SIZE_SHIFT)
#define ATTR_FILTER_LEN_SHIFT 24
#define ATTR_FILTER_LEN_MASK (0xffUL << ATTR_FILTER_LEN_SHIFT)
#define ATTR_FILTER_ALIGN_SHIFT 32
#define ATTR_FILTER_ALIGN_MASK (0xffUL << ATTR_FILTER_ALIGN_SHIFT)

#define ATTR_CAP_DATA_SHIFT 0
#define ATTR_CAP_DATA_MASK (0xffffffffUL << ATTR_CAP_DATA_SHIFT)
#define ATTR_CAP_START_ADDR_SHIFT 0
#define ATTR_CAP_START_ADDR_MASK (0x3ffffffffUL << ATTR_CAP_START_ADDR_SHIFT)
#define ATTR_CAP_END_ADDR_SHIFT 0
#define ATTR_CAP_END_ADDR_MASK (0x3ffffffffUL << ATTR_CAP_END_ADDR_SHIFT)

PMU_FORMAT_ATTR(event, "config:0-7");
PMU_FORMAT_ATTR(inst_id, "config:8-15");
PMU_FORMAT_ATTR(chn_en, "config:8-12");
PMU_FORMAT_ATTR(axi_id, "config1:16-31");
PMU_FORMAT_ATTR(axi_mask, "config1:0-15");
PMU_FORMAT_ATTR(dura_threshold_r, "config1:48-63");
PMU_FORMAT_ATTR(dura_threshold_w, "config1:32-47");
PMU_FORMAT_ATTR(flt_start_addr, "config2:32-63"); // [33:0] >> 4
PMU_FORMAT_ATTR(flt_end_addr, "config2:0-31"); // [33:0] >> 4
PMU_FORMAT_ATTR(flt_size, "config:16-23");
PMU_FORMAT_ATTR(flt_len, "config:24-31");
PMU_FORMAT_ATTR(flt_align, "config:32-39");
PMU_FORMAT_ATTR(cap_chn_en, "config:8-12");
PMU_FORMAT_ATTR(cap_data, "config1:0-31");
PMU_FORMAT_ATTR(cap_start_addr, "config1:0-31"); // [33:0] >> 4
PMU_FORMAT_ATTR(cap_end_addr, "config2:0-31"); // [33:0] >> 4

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_axi_id.attr,
	&format_attr_axi_mask.attr,
	&format_attr_inst_id.attr,
	&format_attr_dura_threshold_r.attr,
	&format_attr_dura_threshold_w.attr,
	&format_attr_flt_start_addr.attr,
	&format_attr_flt_end_addr.attr,
	&format_attr_flt_size.attr,
	&format_attr_flt_len.attr,
	&format_attr_flt_align.attr,
	&format_attr_cap_chn_en.attr,
	&format_attr_cap_data.attr,
	&format_attr_cap_start_addr.attr,
	&format_attr_cap_end_addr.attr,
	NULL,
};

static struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	NULL,
};


static long __get_ddr_freq(void)
{
	return (long)DDR_MT_3733;
}

static long __get_ddr_bitwidth(void)
{
	return (long)DDR_BITWIDTH;
}


static bool ddr_perf_is_axid_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_AXID_MASK);
}

static bool ddr_perf_is_misc_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_MISC_MASK);
}

static bool ddr_perf_is_capture_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_CAPTURE_MASK);
}

static bool ddr_perf_contain_threshold(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_MASK;

	if (val == DDR_EVENT_WRITE_DURATION_OVER_THRESH)
		return 0;
	else if (val == DDR_EVENT_READ_DURATION_OVER_THRESH)
		return 1;

	return -1;
}

static int ddr_perf_contains_filtered(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_MASK;

	if ((val == DDR_EVENT_VRD0_TRANS_CNT)
		|| (val == DDR_EVENT_VWR0_TRANS_CNT))
		return 0;
	else if ((val == DDR_EVENT_VRD1_TRANS_CNT)
		|| (val == DDR_EVENT_VWR1_TRANS_CNT))
		return 1;

	return -1;
}

// enable instance for operating later
static void ddr_pmu_enable_inst(struct ddr_pmu *pmu, int inst)
{
	int i;
	//dev_dbg(pmu->dev, "%s inst=%d\n", __func__, inst);

	for (i = 0; i < NUM_INST; i++) {
		if ((inst == i) || (inst == INST_ALL))
			pmu->pmu_base[i].enable = true;
		else
			pmu->pmu_base[i].enable = false;
	}

}

static void ddr_perf_free_counter(struct ddr_pmu *pmu, int event_id, int inst)
{
	int i;
	dev_dbg(pmu->dev, "%s event_id=%d, inst=%d\n",
		__func__, event_id, inst);

	for (i = 0; i < NUM_INST; i++) {
		if ((inst == i) || (inst == INST_ALL) || (inst == INST_MISC)) {
			if (pmu->events[i][event_id] != NULL) {
				pmu->events[i][event_id] = NULL;
				pmu->hwc_active_events[i]--;
				pmu->active_events--;
			}
		}
	}
}

static bool ddr_perf_is_empty(struct ddr_pmu *pmu, int inst)
{
	int i,j;

	for (i = 0; i < NUM_INST; i++) {
		if ((inst == i) || (inst == INST_ALL) || (inst == INST_MISC)) {
			for (j = 0; j < NUM_EVENTS; j++) {
				if (pmu->events[i][j] != NULL)
					return false;
			}
		}
	}

	return true;
}

static u64 ddr_perf_read_counter(struct ddr_pmu *pmu, int event_id, int inst)
{
	u64 ret = 0;
	int i;

	ddr_pmu_enable_inst(pmu, inst);

	dev_dbg(pmu->dev, "ddr-pmu read counter reg offset<0x%x>\n",
		PMU_READ + (event_id & DDR_EVENT_MASK) * 4);
	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		ret += readl_relaxed(pmu->pmu_base[i].base + PMU_READ
			+ (event_id & DDR_EVENT_MASK) * 4);
	}

	return ret;
}
static u64 ddr_perf_read_max_counter(struct ddr_pmu *pmu, int event_id, int inst)
{
	u64 ret = 0, val = 0;
	int i;

	ddr_pmu_enable_inst(pmu, inst);

	dev_dbg(pmu->dev, "ddr-pmu read max counter reg offset<0x%x>\n",
		PMU_READ + (event_id & DDR_EVENT_MASK) * 4);
	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_READ
			+ (event_id & DDR_EVENT_MASK) * 4);
		if (val > ret)
			ret = val;
	}

	return ret;
}

static int ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	unsigned long cfg1 = event->attr.config1;
	unsigned long cfg2 = event->attr.config2;

	dev_dbg(pmu->dev, "%s, cfg=0x%lx, cfg1=0x%lx, cfg2=0x%lx\n",
				__func__, cfg, cfg1, cfg2);

	if (event->attr.type != event->pmu->type) {
		dev_dbg(pmu->dev, "unsupport attr type 0x%x != 0x%x\n",
					event->attr.type,
					event->pmu->type);
		return -ENOENT;
	}

	if (event->cpu < 0) {
		dev_warn(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	event->cpu = pmu->cpu;
	hwc->config = event->attr.config & ATTR_EVENT_MASK;
	hwc->idx = -1;

	return 0;
}


static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	u64 delta, val;
	struct timespec64 t_end;
	int inst = hwc->idx;

	dev_dbg(pmu->dev, "%s\n", __func__);
	if (ddr_perf_is_misc_masked(event)) {
		if ((cfg & ATTR_EVENT_MASK) == DDR_EVENT_PMU_EXEC_TIME) {
			ktime_get_real_ts64(&t_end);
			delta = (t_end.tv_sec - pmu->t_begin.tv_sec)*1000*1000 +
				 (t_end.tv_nsec - pmu->t_begin.tv_nsec)/1000;
			local64_set(&event->count, delta);
		} else if ((cfg & ATTR_EVENT_MASK) == DDR_EVENT_QUARY_TOTAL_BW) {
			delta = __get_ddr_freq()*__get_ddr_bitwidth()/8;
			local64_set(&event->count, delta);
		}
		dev_dbg(pmu->dev, "misc_event->count<0x%lx>\n",
				local64_read(&event->count));
		return;
	} else if (ddr_perf_is_capture_masked(event)) {
		local64_add(1, &event->count); // increase 1
		dev_dbg(pmu->dev, "cap_event->count<0x%lx>\n",
				local64_read(&event->count));
		return;
	}

	if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_RD_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD, inst);
		val = (delta & RD_MAX_OSTD_MASK) >> RD_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_WR_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD, inst); // same register with DDR_EVENT_RD_MAX_OSTD
		val = (delta & WR_MAX_OSTD_MASK) >> WR_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else {
		delta = ddr_perf_read_counter(pmu, cfg & ATTR_EVENT_MASK, inst);
		local64_add(delta, &event->count);
		local64_set(&hwc->prev_count, delta);
	}
	dev_dbg(pmu->dev, "event->count<0x%lx>,	hwc->prev_count<0x%lx>\n",
				local64_read(&event->count),
				local64_read(&hwc->prev_count));
}

static void ddr_perf_event_update_by_inst(struct perf_event *event, int inst)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	u64 delta, val;

	dev_dbg(pmu->dev, "%s\n", __func__);

	if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_RD_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD, inst);
		val = (delta & RD_MAX_OSTD_MASK) >> RD_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_WR_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD, inst); // same register with DDR_EVENT_RD_MAX_OSTD
		val = (delta & WR_MAX_OSTD_MASK) >> WR_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else {

		delta = ddr_perf_read_counter(pmu, cfg & ATTR_EVENT_MASK, inst);
		local64_add(delta, &event->count);
		local64_set(&hwc->prev_count, delta);
	}
	dev_dbg(pmu->dev, "event->count<0x%lx>,	hwc->prev_count<0x%lx>\n",
				local64_read(&event->count),
				local64_read(&hwc->prev_count));
}

static void ddr_pmu_inst_reset(struct ddr_pmu *pmu, int inst)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s inst=%d\n", __func__, inst);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_RESET;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);

		val |= PMU_RESET;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}

}

static int ddr_pmu_query_irq_sts(struct ddr_pmu *pmu, int inst)
{
	int val, val2;
	int i;

	//dev_dbg(pmu->dev, "%s inst=%d\n", __func__, inst);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
		val = (val & PMU_IRQ_SRC_MASK) >> PMU_IRQ_SRC_SHIFT;
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		val2 = (val2 & IRQ_TARGET_ADDR_MASK) >> IRQ_TARGET_ADDR_SHIFT;
		return val | (val2 << IRQ_SRC_COMBINE_SHIFT);
	}
	return 0;
}

static void ddr_pmu_clear_irq(struct ddr_pmu *pmu, int inst)
{
	int val,val2;
	int i;

	dev_dbg(pmu->dev, "%s inst=%d\n", __func__, inst);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		val2 = (val2 & IRQ_TARGET_ADDR_MASK) >> IRQ_TARGET_ADDR_SHIFT;
		dev_dbg(pmu->dev, "ddr-pmu irq state<0x%x>\n",
				(val & PMU_IRQ_SRC_MASK) >> PMU_IRQ_SRC_SHIFT
				| (val2 << IRQ_SRC_COMBINE_SHIFT));

		val |= PMU_CLEAR_INT;
		writel(val, pmu->pmu_base[i].base + PMU_INT_REG);
	}

}

static void
ddr_pmu_interrupt_enable(struct ddr_pmu *pmu, int inst, int mode, bool enable)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s %d\n", __func__, enable);
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_SRC_SEL_MASK;
		if (enable) {
			switch (mode) {
			case SRC_ADDR_RANGE_HIT:
				val |= SRC_ADDR_RANGE_HIT;
				break;
			case SRC_PERIOD_EXPIRED:
				val |= SRC_PERIOD_EXPIRED;
				break;
			case SRC_TARGET_WDATA:
				val |= SRC_TARGET_WDATA;
				break;
			case SRC_ERROR_RESP:
				val |= SRC_ERROR_RESP;
				break;
			case SRC_CNT_OVERFLOW:
				val |= SRC_CNT_OVERFLOW;
				break;
			default:
				val |= SRC_PERIOD_EXPIRED;
			}
		}
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_set_trigge_mode(struct ddr_pmu *pmu, int inst, int mode)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s\n", __func__);
	dev_dbg(pmu->dev, "%s inst=%d, mode=%d\n", __func__, inst, mode);

	ddr_pmu_enable_inst(pmu, inst);
	/*
	 * config trigge mode to period,
	 * and interrupt source to period expired
	 */
	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_TRIG_MODE_MASK;
		if (mode == SINGLE_MODE)
			val |= SINGLE_MODE;
		else
			val |= PERIOD_MODE;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_config_axid(struct ddr_pmu *pmu, int inst, int axid)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s inst=%d, axid=0x%x\n", __func__, inst, axid);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		if (axid == -1)
			val = 0;
		else
			val = axid;
		writel(val, pmu->pmu_base[i].base + PMU_MST_ID);
	}
}

static void
ddr_pmu_config_threshold(struct ddr_pmu *pmu, int inst, int threshold,
				bool is_read)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;

		val = readl_relaxed(pmu->pmu_base[i].base + PMU_DURA_THRESHOLD);
		if (is_read == true) {
			val &= ~PMU_DURA_THRESHOLD_R_MASK;
			val |= threshold << PMU_DURA_THRESHOLD_R_SHIFT;
		} else {
			val &= ~PMU_DURA_THRESHOLD_W_MASK;
			val |= threshold << PMU_DURA_THRESHOLD_W_SHIFT;
		}
		writel(val, pmu->pmu_base[i].base + PMU_DURA_THRESHOLD);
	}
}

static void
ddr_pmu_config_filter_addr(struct ddr_pmu *pmu, int inst, int cnt,
			long start, long end)
{
	int val;
	int i;
	void __iomem *base;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			// config addr in counter0
			// [ADDRMSB-1: 0]
			val = start & 0xffffffff; // start[31:0]
			writel(val, base + PMU_FLT_S_ADDR0);

			val = readl_relaxed(base + PMU_FLT_S_ADDR1);
			val &= ~((1 << (ADDRMSB-32)) - 1);
			// start[33:32]
			val |= (start >> 32) & ((1 << (ADDRMSB-32)) - 1);
			writel(val, base + PMU_FLT_S_ADDR1);

			val = end & 0xffffffff; // end[31:0]
			writel(val, base + PMU_FLT_E_ADDR0);

			val = readl_relaxed(base + PMU_FLT_E_ADDR1);
			val &= ~((1 << (ADDRMSB-32)) - 1);
			// end[33:32]
			val = (end >> 32) & ((1 << (ADDRMSB-32)) - 1);
			writel(val, base + PMU_FLT_E_ADDR1);
		} else if (cnt == 1) {
			// config addr in counter1
			// [ADDRMSB*2-1: ADDMSB]
			val = readl_relaxed(base + PMU_FLT_S_ADDR1);
			val &= ~((1<<(64-ADDRMSB)) - 1);
			// start[29:0]
			val |= start & ((1<<(64-ADDRMSB)) - 1);
			writel(val, base + PMU_FLT_S_ADDR1);

			val = start >> (64-ADDRMSB); // start[33:30]
			writel(val, base + PMU_FLT_S_ADDR2);

			val = readl_relaxed(base + PMU_FLT_E_ADDR1);
			val &= ~((1<<(64-ADDRMSB)) - 1);
			val |= end & ((1<<(64-ADDRMSB)) - 1); // end[29:0]
			writel(val, base + PMU_FLT_E_ADDR1);

			val = end >> (64-ADDRMSB); // end[33:30]
			writel(val, base + PMU_FLT_E_ADDR2);
		}
	}
}
static void
ddr_pmu_enable_filter_addr(struct ddr_pmu *pmu, int inst, int cnt)
{
	int val;
	int i;
	void __iomem *base;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			// enable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ADDR_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);

		} else if (cnt == 1) {
			// enable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ADDR_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(ADDR_FILTER_CNT0_EN | ADDR_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void
ddr_pmu_filter_size(struct ddr_pmu *pmu, int inst, int cnt, int size)
{
	int val;
	int i;
	void __iomem *base;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~SIZE_FLT_CNT0_MASK;
			val |= (size << SIZE_FLT_CNT0_SHIFT)
				& SIZE_FLT_CNT0_MASK;

			// enable size filter
			val |= SIZE_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);

		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~SIZE_FLT_CNT1_MASK;
			val |= (size << SIZE_FLT_CNT1_SHIFT)
				& SIZE_FLT_CNT1_MASK;

			// enable size filter
			val |= SIZE_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable size filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(SIZE_FILTER_CNT0_EN | SIZE_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_filter_len(struct ddr_pmu *pmu, int inst, int cnt, int len)
{
	int val;
	int i;
	void __iomem *base;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			val = readl_relaxed(base + PMU_FLT_LEN);
			val &= ~LEN_FLT_CNT0_MASK;
			val |= (len << LEN_FLT_CNT0_SHIFT) & LEN_FLT_CNT0_MASK;
			writel(val, base + PMU_FLT_LEN);

			// enable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= LEN_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);

		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_FLT_LEN);
			val &= ~LEN_FLT_CNT1_MASK;
			val |= (len << LEN_FLT_CNT1_SHIFT) & LEN_FLT_CNT1_MASK;
			writel(val, base + PMU_FLT_LEN);

			// enable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= LEN_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(LEN_FILTER_CNT0_EN | LEN_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void
ddr_pmu_filter_align(struct ddr_pmu *pmu, int inst, int cnt, int align)
{
	int val;
	int i;
	int cfg;
	void __iomem *base;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		switch (align) {
		case 16:
			cfg = 1 << 0;
			break;
		case 32:
			cfg = 1 << 1;
			break;
		case 64:
			cfg = 1 << 2;
			break;
		case 128:
			cfg = 1 << 3;
			break;
		default:
			cfg = 0xf;
		}

		if (cnt == 0) {
			val = readl_relaxed(base + PMU_CTRL);
			val &= ~ALIGN_FLT_CNT0_MASK;
			val |= (cfg << ALIGN_FLT_CNT0_SHIFT) &
				ALIGN_FLT_CNT0_MASK;
			writel(val, base + PMU_CTRL);

			// enable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ALIGN_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);

		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_CTRL);
			val &= ~ALIGN_FLT_CNT1_MASK;
			val |= (cfg << ALIGN_FLT_CNT1_SHIFT) &
				ALIGN_FLT_CNT1_MASK;
			writel(val, base + PMU_CTRL);

			// enable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ALIGN_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(ALIGN_FILTER_CNT0_EN |
				ALIGN_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_counter_period(struct ddr_pmu *pmu, int inst, int period)
{
	long val;
	int i;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = (long)pmu->freq_khz * period;
		if (val > UINT_MAX) {
			dev_warn(pmu->dev, "%s counter period is overflow\n",
						__func__);
			val = UINT_MAX;
		}
		writel((unsigned int)val, pmu->pmu_base[i].base + PMU_MON_PERIOD);
	}
}

static void ddr_pmu_set_target_data(struct ddr_pmu *pmu, int inst, int data)
{
	int i;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel(data, pmu->pmu_base[i].base + PMU_TARGET_WDATA);
	}
}
static int ddr_pmu_get_target_data(struct ddr_pmu *pmu, int inst)
{
	int i;
	int data;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		data = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_WDATA);
		return data;
	}

	return -1;
}
static int ddr_pmu_get_read_err_resp_id(struct ddr_pmu *pmu, int inst)
{
	int i;
	int data;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		data = readl_relaxed(pmu->pmu_base[i].base + PMU_ERR_RESP_ID);
		data = (data & READ_ERR_RESP_ID_MASK) >> READ_ERR_RESP_ID_SHIFT;
		return data;
	}

	return -1;
}
static int ddr_pmu_get_write_err_resp_id(struct ddr_pmu *pmu, int inst)
{
	int i;
	int data;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		data = readl_relaxed(pmu->pmu_base[i].base + PMU_ERR_RESP_ID);
		data = (data & WRITE_ERR_RESP_ID_MASK) >> WRITE_ERR_RESP_ID_SHIFT;
		return data;
	}

	return -1;
}

static void ddr_pmu_set_compare_mode(struct ddr_pmu *pmu, int inst, int mode)
{
	int i;

	dev_dbg(pmu->dev, "%s inst=%d, mode=%d\n", __func__, inst, mode);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel(mode, pmu->pmu_base[i].base + PMU_TARGET_ADDR);
	}
}
static int ddr_pmu_get_compare_mode(struct ddr_pmu *pmu, int inst)
{
	int i;
	int mode;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		mode = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		return mode;
	}

	return -1;
}

static void ddr_pmu_config_wdata_select(struct ddr_pmu *pmu, int inst, int mask)
{
	int i;

	dev_dbg(pmu->dev, "%s inst=%d, mask=0x%x\n", __func__, inst, mask);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel((mask << WDATA_SEL_SHIFT) & WDATA_SEL_MASK, pmu->pmu_base[i].base + PMU_INT_REG);
	}
}

static void ddr_pmu_counter_enable(struct ddr_pmu *pmu, int inst, bool enable)
{
	int val;
	int i;

	dev_dbg(pmu->dev, "%s inst=%d, eb=%d\n", __func__, inst, enable);

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < NUM_INST; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		if (enable) {
			val |= PMU_RESET;
			val |= PMU_EN;
		} else {
			/* Disable monitor */
			val &= ~PMU_EN;
			val &= ~PMU_RESET;
		}
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_counter_init(struct ddr_pmu *pmu, int inst)
{
	dev_dbg(pmu->dev, "%s inst<%d>\n", __func__, inst);

	ddr_pmu_enable_inst(pmu, inst);
	ddr_pmu_inst_reset(pmu, inst);
	ddr_pmu_counter_enable(pmu, inst, false);
	ddr_pmu_config_axid(pmu, inst, -1);
	ddr_pmu_config_threshold(pmu, inst, 1, true);
	ddr_pmu_config_threshold(pmu, inst, 1, false);
	ddr_pmu_config_filter_addr(pmu, inst, -1, 0, 0);
	ddr_pmu_enable_filter_addr(pmu, inst, -1);
	ddr_pmu_filter_size(pmu, inst, -1, 0);
	ddr_pmu_filter_len(pmu, inst, -1, 0);
	ddr_pmu_filter_align(pmu, inst, -1, 0);

}

static u64 ddr_pmu_get_version(struct ddr_pmu *pmu, int inst)
{
	u64 ver = -1;

	ver = readl_relaxed(pmu->pmu_base[inst].base + PMU_VERSION1);
	ver = readl_relaxed(pmu->pmu_base[inst].base + PMU_VERSION0)
			| (ver << 32);
	return ver;
}

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	unsigned long cfg1 = event->attr.config1;
	unsigned long cfg2 = event->attr.config2;
	int inst, axid = -1;
	int chns = 0;
	int cm_mode = CM_WRITE;
	int i;
	int threshold;
	int thres_event, flt_cnt;
	long start_addr, end_addr;
	int data;
	int event_num = cfg & ATTR_EVENT_MASK;

	inst = hwc->idx;
	dev_dbg(pmu->dev, "%s inst<%d>\n", __func__, inst);

	if (ddr_perf_is_capture_masked(event)) {
		chns = hwc->idx;
		for (i = 0; i < NUM_INST; i++) {
			if (chns & (1 << i)) {
				ddr_pmu_enable_inst(pmu, i);
				ddr_pmu_set_trigge_mode(pmu, i, TRIGGER_MODE);
				ddr_pmu_counter_period(pmu, i, pmu->period);
				switch (event_num) {
				case DDR_EVENT_CAPTURE_R_DATA:
					cm_mode = CM_READ;
				case DDR_EVENT_CAPTURE_W_DATA:
					data = (cfg1 & ATTR_CAP_DATA_MASK)
						>> ATTR_CAP_DATA_SHIFT;
					ddr_pmu_set_target_data(pmu, i, data);
					ddr_pmu_config_wdata_select(pmu, i, 0xf);
					ddr_pmu_set_compare_mode(pmu, i, cm_mode);
					ddr_pmu_interrupt_enable(pmu, i, SRC_TARGET_WDATA, true);
					break;
				case DDR_EVENT_CAPTURE_ADDR:
					dev_info(pmu->dev, "%s cfg1<0x%lx>, cfg2<0x%lx>\n"
								, __func__, cfg1, cfg2);
					start_addr = (cfg1 & ATTR_CAP_START_ADDR_MASK)
							>> ATTR_CAP_START_ADDR_SHIFT << 4;
					end_addr = (cfg2 & ATTR_CAP_END_ADDR_MASK)
							>> ATTR_CAP_END_ADDR_SHIFT << 4;
					if ((start_addr >= 0) && (end_addr > 0)) {
						dev_info(pmu->dev, "%s start_addr<0x%lx>, end_addr<0x%lx>\n"
									, __func__, start_addr, end_addr);
						ddr_pmu_config_filter_addr(pmu, i, 0, 
								start_addr, end_addr);
						ddr_pmu_enable_filter_addr(pmu, i, 0);
					}
					ddr_pmu_interrupt_enable(pmu, i, SRC_ADDR_RANGE_HIT, true);
					break;
				case DDR_EVENT_CAPTURE_ERROR_RESP_R:
				case DDR_EVENT_CAPTURE_ERROR_RESP_W:
					ddr_pmu_interrupt_enable(pmu, i, SRC_ERROR_RESP, true);
					break;
				default:
					dev_warn(pmu->dev, "%s unsupport capture event<0x%lx>\n",
								__func__, cfg&ATTR_EVENT_MASK);
					break;
				}
				ddr_pmu_counter_enable(pmu, i, true);
				hwc->state = 0;
			}
		}
		return;
	}

	ddr_pmu_enable_inst(pmu, inst);

	local64_set(&event->count, 0);
	local64_set(&hwc->prev_count, 0);

	if (ddr_perf_is_misc_masked(event)) {
		if ((cfg & ATTR_EVENT_MASK) == DDR_EVENT_PMU_EXEC_TIME) {
			// record current time as pmu begin time
			// once new an event, it will update the t_begin value
			ktime_get_real_ts64(&pmu->t_begin);
		}
		hwc->state = 0;
		return;
	}

	if (ddr_perf_is_axid_masked(event)) {
		if (pmu->axi_id == -1)
			axid = event->attr.config1 & ATTR_AXID_MASK;
		else
			axid = (pmu->axi_id << 16) | (pmu->axi_mask & 0xffff);
		ddr_pmu_config_axid(pmu, inst, axid);
	}
	thres_event = ddr_perf_contain_threshold(event);
	if (thres_event == 0) {
		threshold = (event->attr.config1 & ATTR_DURA_THRESHOLD_W_MASK)
				>> ATTR_DURA_THRESHOLD_W_SHIFT;
		ddr_pmu_config_threshold(pmu, inst, threshold, false);
	} else if (thres_event == 1) {
		threshold = (event->attr.config1 & ATTR_DURA_THRESHOLD_R_MASK)
				>> ATTR_DURA_THRESHOLD_R_SHIFT;
		ddr_pmu_config_threshold(pmu, inst, threshold, true);
	}
	flt_cnt = ddr_perf_contains_filtered(event);
	if (flt_cnt >= 0) {
		int flt_size, flt_len, flt_align;

		start_addr = (event->attr.config2 & ATTR_FILTER_START_ADDR_MASK)
				>> ATTR_FILTER_START_ADDR_SHIFT << 4;
		end_addr = (event->attr.config2 & ATTR_FILTER_END_ADDR_MASK) >>
				ATTR_FILTER_END_ADDR_SHIFT << 4;
		flt_size = (cfg & ATTR_FILTER_SIZE_MASK) >>
				ATTR_FILTER_SIZE_SHIFT;
		flt_len = (cfg & ATTR_FILTER_LEN_MASK) >>
				ATTR_FILTER_LEN_SHIFT;
		flt_align = (cfg & ATTR_FILTER_ALIGN_MASK) >>
				ATTR_FILTER_ALIGN_SHIFT;
		if ((start_addr >= 0) && (end_addr > 0)) {
			ddr_pmu_config_filter_addr(pmu, inst, flt_cnt, start_addr,
							end_addr);
			ddr_pmu_enable_filter_addr(pmu, inst, flt_cnt);
		}
		if (flt_size > 0)
			ddr_pmu_filter_size(pmu, inst, flt_cnt, flt_size);
		if (flt_len > 0)
			ddr_pmu_filter_len(pmu, inst, flt_cnt, flt_len);
		if (flt_align > 0)
			ddr_pmu_filter_align(pmu, inst, flt_cnt, flt_align);
	}
	ddr_pmu_set_trigge_mode(pmu, inst, TRIGGER_MODE);
	ddr_pmu_counter_period(pmu, inst, pmu->period);
	ddr_pmu_interrupt_enable(pmu, inst, SRC_PERIOD_EXPIRED, true);
	ddr_pmu_counter_enable(pmu, inst, true);

	hwc->state = 0;
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	int inst = INST_NULL;
	int chns = 0;
	int i;

	dev_dbg(pmu->dev, "%s\n", __func__);

	if (hwc->idx == -1) { // first initial
		if (ddr_perf_is_axid_masked(event)) {
			if (pmu->inst_id == -1)
				inst = (cfg & ATTR_INST_MASK) >> ATTR_INST_SHIFT;
			else
				inst = pmu->inst_id;
		}
		else if (ddr_perf_is_misc_masked(event))
			inst = INST_MISC;
		else if (ddr_perf_is_capture_masked(event))
			chns = (cfg & ATTR_CHN_EN_MASK) >> ATTR_CHN_EN_SHIFT;
		else
			inst = INST_ALL;
		hwc->idx = inst;
		if (ddr_perf_is_capture_masked(event)) {
			hwc->idx = chns;
		}
	}
	for (i = 0; i < NUM_INST; i++) {
		if ((chns & (1 << i)) || (inst == i) || (inst == INST_ALL) || (inst == INST_MISC)) {
			pmu->events[i][cfg & ATTR_EVENT_MASK] = event;
			if (pmu->active_events == 0)
				pmu_trace.trace_count = 0;
			pmu->active_events++;
			if (pmu->hwc_active_events[i] == 0) {
				if (inst != INST_MISC)
					ddr_pmu_counter_init(pmu, i);
			}
			pmu->hwc_active_events[i]++;
			if (inst == i)
				break;
		}
	}
	dev_dbg(pmu->dev, "ddr-pmu inst<%d>\n", inst);
	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	int inst = hwc->idx;
	int chns = 0;
	int i;

	dev_dbg(pmu->dev, "%s inst<%d>\n", __func__, inst);

	if (ddr_perf_is_capture_masked(event)) {
		chns = hwc->idx;
	}
	for (i = 0; i < NUM_INST; i++) {
		if ((chns & (1 << i)) || (inst == i) || (inst == INST_ALL) || (inst == INST_MISC)) {
			if (inst != INST_MISC) {
				ddr_pmu_enable_inst(pmu, i);
				ddr_pmu_counter_enable(pmu, i, false);
				if (pmu->hwc_active_events[i] == 0) {
					// the last event of one bmu inst, stop it
					ddr_pmu_interrupt_enable(pmu, i, 0, false);
				}
				ddr_pmu_clear_irq(pmu, i);
			}
			if (pmu->hwc_active_events[i] == 0) {
				// the last event of one bmu inst, reinit the configuration
				ddr_pmu_counter_init(pmu, i);
			}
			ddr_perf_free_counter(pmu, cfg & ATTR_EVENT_MASK, i);
		}
	}


}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	dev_dbg(pmu->dev, "%s\n", __func__);

	ddr_perf_event_stop(event, PERF_EF_UPDATE);
	if (!ddr_perf_is_capture_masked(event))
		ddr_perf_event_update(event);
	hwc->state |= PERF_HES_STOPPED;
	hwc->idx = -1;
}

static void ddr_perf_pmu_enable(struct pmu *pmu)
{
	dev_dbg(pmu->dev, "%s\n", __func__);
}

static void ddr_perf_pmu_disable(struct pmu *pmu)
{
	dev_dbg(pmu->dev, "%s\n", __func__);
}

static int ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base[],
			 struct device *dev)
{
	int i,j;

	*pmu = (struct ddr_pmu) {
		.pmu = (struct pmu) {
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.attr_groups = attr_groups,
			.event_init  = ddr_perf_event_init,
			.add	     = ddr_perf_event_add,
			.del	     = ddr_perf_event_del,
			.start	     = ddr_perf_event_start,
			.stop	     = ddr_perf_event_stop,
			.read	     = ddr_perf_event_update,
			.pmu_enable  = ddr_perf_pmu_enable,
			.pmu_disable = ddr_perf_pmu_disable,
		},
		.dev = dev,
	};

	for (i = 0; i < NUM_INST; i++) {
		pmu->pmu_base[i].base = base[i];
		pmu->pmu_base[i].enable = false;
		for (j = 0; j < NUM_EVENTS; j++) {
			pmu->events[i][j] = NULL;
		}
		pmu->hwc_active_events[i] = 0;
		pmu->hwc_version[i] = ddr_pmu_get_version(pmu, i);
	}
	pmu->active_events = 0;

	pmu->period = PMU_PERIOD_CNT;
	pmu->freq_khz = APB_CLK / 1000; // unit KHz
	pmu->id = ida_simple_get(&ddr_ida, 0, 0, GFP_KERNEL);
	pmu->inst_id = -1;
	pmu->axi_id = -1;
	pmu->axi_mask = -1;

	pmu_trace.trace_data_fmt = FMT_DECIMAL;

	return pmu->id;
}

static irqreturn_t ddr_pmu_irq_handler(int irq, void *p)
{
	int i, j;
	struct ddr_pmu *pmu = (struct ddr_pmu *) p;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	long long ms;
	int status;
	char tracelog[1024];
	struct trace_point tp;

	dev_dbg(pmu->dev, "%s\n", __func__);
	if (pmu_trace.trace_enable == 1)
	{
		ms = ktime_to_ms(ktime_get_boottime());
		sprintf(tracelog, "{pmu_trace<%d> [ms]:%lld",
					pmu_trace.trace_count,
					ms);
	}
	for (i = 0; i < NUM_INST; i++) {
		status = ddr_pmu_query_irq_sts(pmu, i);
		if (status == 0)
			continue;

		if (status & IRQ_SRC_TIME_EXPIRED) {
			for (j = 0; j < NUM_TIME_EXPIRED_EVENTS; j++) {
				if (!pmu->events[i][j])
					continue;

				event = pmu->events[i][j];
				if (ddr_perf_is_misc_masked(event))
					continue;
				hwc = &event->hw;
				ddr_perf_event_update_by_inst(event, i);
				if (pmu_trace.trace_enable == 1)
				{
					char tmp[30];
					tp.reg_offset = (unsigned int)((event->attr.config & DDR_EVENT_MASK) * 4)
									+ PMU_STS_REG_BASE;
					tp.value = (unsigned int)local64_read(&hwc->prev_count);
					if (pmu_trace.trace_data_fmt == 1) // in decimal format
						sprintf(tmp, " [BMU%d_0x%x]:%d", i, tp.reg_offset, tp.value);
					else // in hex format
						sprintf(tmp, " [BMU%d_0x%x]:0x%x", i, tp.reg_offset, tp.value);
					strcat(tracelog, tmp);
				}
			}
		}
		if (status & IRQ_SRC_TARGET_DATA_OCCUR) {
			if (ddr_pmu_get_compare_mode(pmu, i) == 1) {// compare data for read
				event = pmu->events[i][DDR_EVENT_CAPTURE_R_DATA];
				if (event) {
					ddr_perf_event_update(event);
					dev_warn(pmu->dev, "%s bmu inst<%d>:TARGET_RDATA<0x%x> captured\n",
							__func__, i, ddr_pmu_get_target_data(pmu, i));
				}
				else
					dev_err(pmu->dev, "%s bmu inst<%d>: found null event CAP_RDATA\n",
							__func__, i);
			} else {
				event = pmu->events[i][DDR_EVENT_CAPTURE_W_DATA];
				if (event) {
					ddr_perf_event_update(event);
					dev_warn(pmu->dev, "%s bmu inst<%d>:TARGET_WDATA<0x%x> captured\n",
							__func__, i, ddr_pmu_get_target_data(pmu, i));
				}
				else
					dev_err(pmu->dev, "%s bmu inst<%d>: found null event CAP_WDATA\n",
							__func__, i);
			}
		}
		if (status & IRQ_SRC_TARGET_ADDR_R_OCCUR || status & IRQ_SRC_TARGET_ADDR_W_OCCUR) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ADDR];
			if (event) {
				ddr_perf_event_update(event);
				if (status & IRQ_SRC_TARGET_ADDR_R_OCCUR)
					dev_warn(pmu->dev, "%s bmu inst<%d>:TARGET_RADDR[0x%llx ~ 0x%llx] captured\n",
							__func__, i,
							event->attr.config1,
							event->attr.config2);
				else
					dev_warn(pmu->dev, "%s bmu inst<%d>:TARGET_WADDR[0x%llx ~ 0x%llx] captured\n",
							__func__, i,
							event->attr.config1,
							event->attr.config2);
			} else {
				dev_err(pmu->dev, "%s bmu inst<%d>: found null event CAP_ADDR\n",
							__func__, i);
			}

		}
		if (status & IRQ_SRC_READ_ERROR_RESP_OCCUR) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ERROR_RESP_R];
			if (event) {
				ddr_perf_event_update(event);
				dev_warn(pmu->dev, "%s bmu inst<%d>:ERROR_RESP_R id<0x%x> captured\n",
							__func__, i, ddr_pmu_get_read_err_resp_id(pmu, i));
			}
			else
				dev_err(pmu->dev, "%s bmu inst<%d>: found null event ERROR_RESP_R\n",
							__func__, i);
		}
		if (status & IRQ_SRC_WRITE_ERROR_RESP_OCCUR) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ERROR_RESP_W];
			if (event) {
				ddr_perf_event_update(event);
				dev_warn(pmu->dev, "%s bmu inst<%d>:ERROR_RESP_W id<0x%x> captured\n",
							__func__, i, ddr_pmu_get_write_err_resp_id(pmu, i));
			}
			else
				dev_err(pmu->dev, "%s bmu inst<%d>: found null event ERROR_RESP_W\n",
							__func__, i);
		}
		ddr_pmu_clear_irq(pmu, i);
	}

	if (pmu_trace.trace_enable == 1) {
		strcat(tracelog, " }:");
		if (pmu_trace.trace_mode == 0) { // via uart log
			dev_dbg(pmu->dev, "%s\n", tracelog);
		}
		pmu_trace.trace_count++;
	}
	return IRQ_HANDLED;
}

static struct dentry *ddr_pmu_dir;
static struct dentry *events_dir;
static struct dentry *ver_dir;

static int ddr_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	struct device_node *np;
	struct resource res;
	void __iomem *base[NUM_INST];
	uint32_t reg_size = 0;
	char node[30];
	char *name;
	int num;
	int ret;
	int irq;
	int i;

	dev_info(&pdev->dev, "<%s>\n", __func__);

	// to init with some individual pmu instances
	for (i = 0; i < NUM_INST; i++) {
		ret = of_address_to_resource(pdev->dev.of_node, i, &res);
		if (ret) {
			dev_err(&pdev->dev, "unsupported reg<%d> in dts\n", i);
			return ret;
		}
		dev_dbg(&pdev->dev, "%s: registers %#llx-%#llx\n", __func__,
			(unsigned long long)res.start,
			(unsigned long long)res.end);

		reg_size += res.end - res.start + 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		base[i] = devm_ioremap_nocache(&pdev->dev, res.start, reg_size);
#else
		base[i] = devm_ioremap(&pdev->dev, res.start, reg_size);
#endif
		if (IS_ERR(base[i]))
			return PTR_ERR(base[i]);
		dev_dbg(&pdev->dev, "%s: base_addr<%d> %#llx\n", __func__, i,
			(unsigned long long)base[i]);
	}

	np = pdev->dev.of_node;

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	num = ddr_perf_init(pmu, base, &pdev->dev);

	platform_set_drvdata(pdev, pmu);

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME "%d",
				num);
	if (!name)
		return -ENOMEM;

	/* Request irq */
	irq = of_irq_get(np, 0);
	dev_dbg(&pdev->dev, "irq: <%d>\n", irq);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq: %d", irq);
		ret = irq;
		goto ddr_perf_err;
	}

	ret = devm_request_irq(&pdev->dev, irq,
					ddr_pmu_irq_handler,
					IRQF_NO_THREAD,
					DDR_PERF_DEV_NAME,
					pmu);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request irq failed: %d", ret);
		goto ddr_perf_err;
	}

	ret = perf_pmu_register(&pmu->pmu, name, -1);
	if (ret)
		goto ddr_perf_err;

#ifdef CONFIG_DEBUG_FS
	ddr_pmu_dir = debugfs_create_dir("ddr-pmu", NULL);
	if (ddr_pmu_dir != NULL) {
		ver_dir = debugfs_create_dir("version", ddr_pmu_dir);
		if (ver_dir != NULL) {
			for (i = 0; i < NUM_INST; i++) {
				sprintf(node, "hwc%d_version", i);
				debugfs_create_x64(node, 0444, ver_dir, &pmu->hwc_version[i]);
			}
		}
		debugfs_create_u32("period_ms", 0666, ddr_pmu_dir, &pmu->period);
		debugfs_create_u32("freq_khz", 0666, ddr_pmu_dir, &pmu->freq_khz);
		debugfs_create_u32("active_events", 0444, ddr_pmu_dir, &pmu->active_events);
		events_dir = debugfs_create_dir("events", ddr_pmu_dir);
		if (events_dir != NULL) {
			for (i = 0; i < NUM_INST; i++) {
				sprintf(node, "hwc%d_active_events", i);
				debugfs_create_u32(node, 0444, events_dir, &pmu->hwc_active_events[i]);
			}
			debugfs_create_x32("inst_id", 0666, events_dir, &pmu->inst_id);
			debugfs_create_x32("axi_id", 0666, events_dir, &pmu->axi_id);
			debugfs_create_x32("axi_mask", 0666, events_dir, &pmu->axi_mask);
		}
		debugfs_create_u32("trace_enable", 0666, ddr_pmu_dir, &pmu_trace.trace_enable);
		//debugfs_create_u32("trace_period_ms", 0666, ddr_pmu_dir, &pmu_trace.trace_period_ms);
		debugfs_create_u32("trace_data_format", 0666, ddr_pmu_dir, &pmu_trace.trace_data_fmt);
		debugfs_create_u32("trace_count", 0664, ddr_pmu_dir, &pmu_trace.trace_count);
		debugfs_create_u32("trace_mode", 0666, ddr_pmu_dir, &pmu_trace.trace_mode);
	}
#endif

	return 0;

ddr_perf_err:
	ida_simple_remove(&ddr_ida, pmu->id);
	dev_warn(&pdev->dev, " DDR Perf PMU failed (%d), disabled\n", ret);
	return ret;
}

static int ddr_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	dev_info(pmu->dev, "%s\n", __func__);

#ifdef CONFIG_DEBUG_FS
	if (ddr_pmu_dir != NULL)
		debugfs_remove_recursive(ddr_pmu_dir);
#endif

	perf_pmu_unregister(&pmu->pmu);

	ida_simple_remove(&ddr_ida, pmu->id);

	return 0;
}

static struct platform_driver light_ddr_pmu_driver = {
	.driver         = {
		.name   = "light-ddr-pmu",
		.of_match_table = light_ddr_pmu_dt_ids,
	},
	.probe          = ddr_perf_probe,
	.remove         = ddr_perf_remove,
};

module_platform_driver(light_ddr_pmu_driver);
MODULE_LICENSE("GPL v2");
