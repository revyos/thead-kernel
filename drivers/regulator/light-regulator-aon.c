/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/firmware/thead/ipc.h>

#define MBOX_MAX_MSG_LEN	28

#define CONFIG_AON_REG_DEBUG	1

struct rpc_msg_regu_vol_set {
	u16 regu_id;                    ///< virtual regu id
	u16 is_dual_rail;               ///< whether this regu has dual rails
	u32 dc1;                        ///< voltage uinit in uv for single rail or first rail of dual rail
	u32 dc2;                        ///< voltage uinit in uv for second rail of dual rail,ignore it if "is_dual_rail" is false
	u16 reserved[6];
} __packed __aligned(1);

struct rpc_msg_regu_vol_get {
	u16 regu_id;                    ///< virtual regu id
	u16 is_dual_rail;               ///< whether this regu has dual rails
	u32 dc1;                        ///< voltage uinit in uv for single rail or first rail of dual rail
	u32 dc2;                        ///< voltage uinit in uv for second rail of dual rail,ignore it if "is_dual_rail" is false
	u16 reserved[6];

} __packed __aligned(1);

struct rpc_msg_regu_vol_get_ack {
	struct light_aon_rpc_ack_common ack_hdr; ///< ack_hdr
	u16 regu_id;                    ///< virtual regu id
	u16 is_dual_rail;               ///< whether this regu has dual rails
	u32 dc1;                        ///< voltage uinit in uv for single rail or first rail of dual rail
	u32 dc2;                        ///< voltage uinit in uv for second rail of dual rail,ignore it if "is_dual_rail" is false
	u16 reserved[6];

} __packed __aligned(1);

struct rpc_msg_regu_pwr_set {
	u16 regu_id;                    ///< virtual regu id
	u16 status;                     ///< 0: power off; 1: powr on
	u32 reserved[5];
} __packed __aligned(1);

struct rpc_msg_regu_pwr_get {
	u16 regu_id;                    ///< virtual regu id
	u32 reserved[5];
} __packed __aligned(1);

struct rpc_msg_regu_pwr_get_ack {
	struct light_aon_rpc_ack_common ack_hdr; ///< ack_hdr
	u16 regu_id;                    ///< virtual regu id
	u16 status;                     ///< 0: power off; 1: powr on
	u32 reserved[5];
} __packed __aligned(1);

struct light_aon_msg_regulator_ctrl {
	struct light_aon_rpc_msg_hdr hdr;
	union rpc_func_t {
	struct rpc_msg_regu_vol_set    regu_vol_set;
	struct rpc_msg_regu_vol_get    regu_vol_get;
	struct rpc_msg_regu_pwr_set    regu_pwr_set;
	struct rpc_msg_regu_pwr_get    regu_pwr_get;
	} __packed __aligned(1) rpc;
} __packed __aligned(1);

enum pm_resource {
	SOC_DVDD18_AON,      /*da9063:  ldo-3 */
	SOC_AVDD33_USB3,     /*da9063:  ldo-9 */
	SOC_DVDD08_AON,      /*da9063:  ldo-2 */
	SOC_APCPU_DVDD_DVDDM,/*da9063:  vbcore1 & vbcore2*/
	SOC_DVDD08_DDR,      /*da9063:  buckperi */
	SOC_VDD_DDR_1V8,     /*da9063:  ldo-4  */
	SOC_VDD_DDR_1V1,     /*da9063:  buckmem & buckio  */
	SOC_VDD_DDR_0V6,     /*da9063:  buckpro */
	SOC_DVDD18_AP,       /*da9063:  ldo-11 */
	SOC_DVDD08_AP,       /*da9121:  da9121_ex */
	SOC_AVDD08_MIPI_HDMI,/*da9063:  ldo-1  */
	SOC_AVDD18_MIPI_HDMI,/*da9063:  ldo-5  */
	SOC_DVDD33_EMMC,     /*da9063:  ldo-10 */
	SOC_DVDD18_EMMC,     /*slg51000:ldo-3 */
	SOC_DOVDD18_SCAN,    /*da9063:  ldo-6 */
	SOC_VEXT_2V8,        /*da9063:  ldo-7 */
	SOC_DVDD12_SCAN,     /*da9063:  ld0-8 */
	SOC_AVDD28_SCAN_EN,  /*da9063: gpio4 */
	SOC_AVDD28_RGB,      /*slg51000:ldo-1 */
	SOC_DOVDD18_RGB,     /*slg51000:ldo-4 */
	SOC_DVDD12_RGB,      /*slg51000:ldo-5 */
	SOC_AVDD25_IR,       /*slg51000:ldo-2 */
	SOC_DOVDD18_IR,      /*slg51000:ldo-7 */
	SOC_DVDD12_IR,       /*slg51000:ldo-6 */
	SOC_ADC_VREF,
	SOC_LCD0_EN,
	SOC_VEXT_1V8,


	SOC_REGU_MAX
};

struct apcpu_vol_set {
	u32 vdd;               ///< cpu core voltage
	u32 vddm;              ///< cpu core-mem voltage
};

struct aon_regu_desc {
	struct regulator_desc *regu_desc;            ///< discription of regulator
	u32                    regu_num;             ///< element number of regulators,which point to regu-dsc-array
};

struct aon_regu_info {
	struct device              *dev;
	const struct apcpu_vol_set *cpu_vol;         ///< signed-off voltage of cpu
	u32                        vddm;             ///< cpu-mem voltage
	uint8_t                    cpu_dual_rail_flag; ///< cpu dual rail flag
	uint8_t                    vddm_dual_rail_flag; ///< cpu-mem dual rail flag
	struct aon_regu_desc       *regu_desc;       ///< regu-desc set
	struct light_aon_ipc       *ipc_handle;      ///< handle of mail-box
};

static struct aon_regu_info light_aon_pmic_info;

#define APCPU_VOL_DEF(_vdd, _vddm) \
	{					\
		.vdd = _vdd,		\
		.vddm = _vddm,		\
	}

static const struct apcpu_vol_set apcpu_volts[] = {
	/*300Mhz*/
	APCPU_VOL_DEF(600000U, 750000U),
	APCPU_VOL_DEF(600000U, 800000U),
	APCPU_VOL_DEF(650000U, 800000U),
	APCPU_VOL_DEF(720000U, 770000U),
	/*800Mhz*/
	APCPU_VOL_DEF(700000U,800000U),
	APCPU_VOL_DEF(720000U,820000U),
	/*1500Mhz*/
	APCPU_VOL_DEF(800000U,800000U),
	APCPU_VOL_DEF(820000U,820000U),
	/*1850Mhz*/
	APCPU_VOL_DEF(1000000U,1000000U),
};

/* dc2 is valid when is_dual_rail is true
 *
 * dual-rail regulator means that a virtual regulator involes two hw-regulators
 */
static int aon_set_regulator(struct light_aon_ipc *ipc, u16 regu_id,
			    u32 dc1, u32 dc2, u8 dc1_is_dual_rail, u8 dc2_is_dual_rail)
{
	uint16_t is_dual_rail = 0;
	struct light_aon_msg_regulator_ctrl msg = {0};
	struct light_aon_rpc_ack_common  ack_msg = {0};
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_PM;
	hdr->func = (uint8_t)LIGHT_AON_PM_FUNC_SET_RESOURCE_REGULATOR;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	if(dc1_is_dual_rail) {
        is_dual_rail |= 1 << 0;
	}

	if(dc2_is_dual_rail) {
        is_dual_rail |= 1 << 1;
	}

	RPC_SET_BE16(&msg.rpc.regu_vol_set.regu_id, 0, regu_id);
	RPC_SET_BE16(&msg.rpc.regu_vol_set.regu_id, 2, is_dual_rail);
	RPC_SET_BE32(&msg.rpc.regu_vol_set.regu_id, 4, dc1);
	RPC_SET_BE32(&msg.rpc.regu_vol_set.regu_id, 8, dc2);

	return light_aon_call_rpc(ipc, &msg, &ack_msg, true);
}

/* dc2 is valid when is_dual_rail is true
 *
 * dual-rail regulator means that a virtual regulator involes two hw-regulators
 */
static int aon_get_regulator(struct light_aon_ipc *ipc, u16 regu_id,
			    u32 *dc1, u32 *dc2, u16 is_dual_rail)
{
	struct light_aon_msg_regulator_ctrl msg = {0};
	struct rpc_msg_regu_vol_get_ack     ack_msg = {0};
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;
	int ret;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_PM;
	hdr->func = (uint8_t)LIGHT_AON_PM_FUNC_GET_RESOURCE_REGULATOR;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	RPC_SET_BE16(&msg.rpc.regu_vol_get.regu_id, 0, regu_id);
    RPC_SET_BE16(&msg.rpc.regu_vol_get.regu_id, 2, is_dual_rail);


	ret = light_aon_call_rpc(ipc, &msg, &ack_msg, true);
	if (ret)
		return ret;
    /*fix me:set local */
	ack_msg.regu_id = regu_id;
	ack_msg.is_dual_rail = is_dual_rail;

	RPC_GET_BE32(&ack_msg.regu_id, 4, &ack_msg.dc1);
	RPC_GET_BE32(&ack_msg.regu_id, 8, &ack_msg.dc2);

	if (dc1 != NULL)
		*dc1 = ack_msg.dc1;

	if (dc2 != NULL)
		*dc2 = ack_msg.dc2;

	return 0;
}

static int aon_regu_power_ctrl(struct light_aon_ipc *ipc,u32 regu_id,u16 pwr_on)
{
	struct light_aon_msg_regulator_ctrl msg = {0};
	struct light_aon_rpc_ack_common  ack_msg = {0};
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_PM;
	hdr->func = (uint8_t)LIGHT_AON_PM_FUNC_PWR_SET;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	RPC_SET_BE16(&msg.rpc.regu_pwr_set.regu_id, 0, regu_id);
	RPC_SET_BE16(&msg.rpc.regu_pwr_set.regu_id, 2, pwr_on);

	return light_aon_call_rpc(ipc, &msg, &ack_msg, true);
}
static int aon_regu_dummy_enable(struct regulator_dev *reg)
{
	return 0;
}
static int aon_regu_dummy_disable(struct regulator_dev *reg)
{
	return 0;
}
static int aon_regu_dummy_is_enabled(struct regulator_dev *reg)
{
	return 0;
}
static int aon_regu_enable(struct regulator_dev *reg)
{
	u16 regu_id =(u16) rdev_get_id(reg);
	return aon_regu_power_ctrl(light_aon_pmic_info.ipc_handle, regu_id, 1);
}

static int aon_regu_disable(struct regulator_dev *reg)
{
	u16 regu_id =(u16) rdev_get_id(reg);
	return aon_regu_power_ctrl(light_aon_pmic_info.ipc_handle, regu_id, 0);
}

static int aon_regu_is_enabled(struct regulator_dev *reg)
{
	struct light_aon_msg_regulator_ctrl msg = {0};
	struct rpc_msg_regu_pwr_get_ack  ack_msg = {0};
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;
	int ret;
	u16 regu_id =(u16) rdev_get_id(reg);

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_PM;
	hdr->func = (uint8_t)LIGHT_AON_PM_FUNC_PWR_GET;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	RPC_SET_BE16(&msg.rpc.regu_pwr_get.regu_id, 0, regu_id);

	ret = light_aon_call_rpc(light_aon_pmic_info.ipc_handle, &msg, &ack_msg, true);
	if (ret < 0) {
		return ret;
	}
	RPC_GET_BE16(&ack_msg.regu_id, 2, &ack_msg.status);
	return (int) ack_msg.status;
}

static int aon_regu_set_voltage(struct regulator_dev *reg,
				int minuV, int uV, unsigned *selector)
{
	u16 regu_id =(u16) rdev_get_id(reg);
	u32 voltage = minuV; /* uV */
	int err;

	pr_debug("[%s,%d]minuV = %d, uV = %d\n", __func__, __LINE__, minuV, uV);

	err = aon_set_regulator(light_aon_pmic_info.ipc_handle, regu_id,
				       voltage, 0, 0, 0);
	if (err) {
		pr_err("failed to set Voltages to %d!\n", minuV);
		return -EINVAL;
	}

	return 0;
}

static int aon_regu_get_voltage(struct regulator_dev *reg)
{
	u16 regu_id = (u16) rdev_get_id(reg);
	int voltage, ret;

	ret = aon_get_regulator(light_aon_pmic_info.ipc_handle, regu_id,
				&voltage, NULL, 0);
	if (ret) {
		pr_err("failed to get voltage\n");
		return -EINVAL;
	}

	pr_debug("[%s,%d]voltage = %d\n", __func__, __LINE__, voltage);

	return voltage;
}

static const struct apcpu_vol_set *apcpu_get_matched_signed_off_voltage(u32 vdd, u32 vddm)
{
	int vol_count = ARRAY_SIZE(apcpu_volts);
	int i;

	for (i = 0; i < vol_count; i++)
		if ((vdd == apcpu_volts[i].vdd) &&
		    (vddm == apcpu_volts[i].vddm))
			return &apcpu_volts[i];

#ifdef CONFIG_AON_REG_DEBUG
	return &apcpu_volts[2];
#else
	return NULL;
#endif
}

static int apcpu_set_vdd_vddm_voltage(struct regulator_dev *reg,
			      int minuV, int uV, unsigned *selector)
{
	struct aon_regu_info *info = rdev_get_drvdata(reg);
	const struct apcpu_vol_set *cpu_vol;
	u32 vol = minuV; /* uV */
	u32 dc1, dc2;
	int err;

	cpu_vol = apcpu_get_matched_signed_off_voltage(vol, light_aon_pmic_info.vddm);
	if (!cpu_vol) {
		dev_err(info->dev, "failed to find bcore1/bcore2 matching table\n");
#ifndef CONFIG_AON_REG_DEBUG
		return -EINVAL;
#endif
	}

	dc1 = cpu_vol->vdd;
	dc2 = cpu_vol->vddm;
	info->cpu_vol = cpu_vol;
	info->vddm = cpu_vol->vddm;

	err = aon_set_regulator(light_aon_pmic_info.ipc_handle, (u16)SOC_APCPU_DVDD_DVDDM,
				       dc1, dc2, info->cpu_dual_rail_flag, info->vddm_dual_rail_flag);
	if (err) {
		dev_err(info->dev, "failed to set Voltages to %d!\n", uV);
		return -EINVAL;
	}

	return 0;
}

static int apcpu_set_vddm_voltage(struct regulator_dev *reg,
			      int minuV, int uV, unsigned *selector)
{
	struct aon_regu_info *info = rdev_get_drvdata(reg);
	int bcore_table_count = ARRAY_SIZE(apcpu_volts);
	u32 vol = minuV; /* uV */
	int i;

	for (i = 0; i < bcore_table_count; i++)
		if (vol == apcpu_volts[i].vddm)
			break;

	if (i >= bcore_table_count) {
		dev_err(info->dev, "The vol is not existed in matching table\n");
#ifndef CONFIG_AON_REG_DEBUG
		return -EINVAL;
#endif
	}

	/* update the vddm */
	info->vddm = vol;
	return 0;
}

static int apcpu_get_voltage(struct regulator_dev *reg, bool is_vdd)
{
	struct aon_regu_info *info = rdev_get_drvdata(reg);
	const struct apcpu_vol_set *cpu_vol;
	u32 dc1, dc2;
	int err;

	err = aon_get_regulator(light_aon_pmic_info.ipc_handle, SOC_APCPU_DVDD_DVDDM,
				       &dc1, &dc2, 1);
	if (err) {
		dev_err(info->dev, "failed to get Voltages!\n");
		return -EINVAL;
	}
	cpu_vol = apcpu_get_matched_signed_off_voltage(dc1, dc2);
	if (!cpu_vol) {
		dev_err(info->dev, "Voltage [%d:%d] is not existing in matching table\n", dc1, dc2);
		return -EINVAL;
	}

	info->cpu_vol = cpu_vol;

	return is_vdd ? cpu_vol->vdd : cpu_vol->vddm;
}

static int apcpu_get_vdd_voltage(struct regulator_dev *reg)
{
	return apcpu_get_voltage(reg, true);
}

static int apcpu_get_vddm_voltage(struct regulator_dev *reg)
{
	return apcpu_get_voltage(reg, false);
}

static const struct regulator_ops regu_common_ops = {
	.enable =        aon_regu_enable,
	.disable =       aon_regu_disable,
	.is_enabled =    aon_regu_is_enabled,
	.list_voltage =  regulator_list_voltage_linear,
	.set_voltage =   aon_regu_set_voltage,
	.get_voltage =   aon_regu_get_voltage,
};

static const struct regulator_ops regu_gpio_ops = {
	.enable =        aon_regu_enable,
	.disable =       aon_regu_disable,
	.is_enabled =    aon_regu_is_enabled,
	.list_voltage =  regulator_list_voltage_linear,
};

static const struct regulator_ops apcpu_dvdd_ops = {
	.enable =        aon_regu_dummy_enable,
	.disable =       aon_regu_dummy_disable,
	.is_enabled =    aon_regu_dummy_is_enabled,
	.list_voltage =  regulator_list_voltage_linear,
	.set_voltage =   apcpu_set_vdd_vddm_voltage,
	.get_voltage =   apcpu_get_vdd_voltage,
};

static const struct regulator_ops apcpu_dvddm_ops = {
	.enable =        aon_regu_dummy_enable,
	.disable =       aon_regu_dummy_disable,
	.is_enabled =    aon_regu_dummy_is_enabled,
	.list_voltage =  regulator_list_voltage_linear,
	.set_voltage =   apcpu_set_vddm_voltage,
	.get_voltage =   apcpu_get_vddm_voltage,
};

/* Macros for voltage DC/DC converters (BUCKs) for cpu */
#define REGU_DSC_DEF(regu_id, of_math_name) \
	.id = regu_id, \
	.name = of_match_ptr(__stringify(of_math_name)), \
	.of_match = of_match_ptr(__stringify(of_math_name)), \
	.type = REGULATOR_VOLTAGE, \
	.owner = THIS_MODULE

#define BUCK_APCPU_DVDD(regu_id,min_mV, step_mV, max_mV) \
	.id = regu_id, \
	.name = of_match_ptr("appcpu_dvdd"), \
	.of_match = of_match_ptr("appcpu_dvdd"), \
	.ops = &apcpu_dvdd_ops, \
	.min_uV = (min_mV), \
	.uV_step = (step_mV), \
	.n_voltages = ((max_mV) - (min_mV))/(step_mV) + 1, \
	.type = REGULATOR_VOLTAGE, \
	.owner = THIS_MODULE

#define BUCK_APCPU_DVDDM(regu_id, min_mV, step_mV, max_mV) \
	.id = regu_id, \
	.name = of_match_ptr("appcpu_dvddm"),\
	.of_match = of_match_ptr("appcpu_dvddm"), \
	.ops = &apcpu_dvddm_ops, \
	.min_uV = (min_mV) , \
	.uV_step = (step_mV), \
	.n_voltages = ((max_mV) - (min_mV))/(step_mV) + 1, \
	.type = REGULATOR_VOLTAGE, \
	.owner = THIS_MODULE

static struct regulator_desc light_dialog_regu_desc[] = {
	/*cpu vdd vddm regulators, used to adjust vol dynamicaly */
	{
		BUCK_APCPU_DVDD(SOC_APCPU_DVDD_DVDDM, 300000, 10000, 1570000),
	},
	{
		BUCK_APCPU_DVDDM(SOC_APCPU_DVDD_DVDDM, 300000, 10000, 1570000),
	},

	/*common regu ,no need to adjust vol dynamicaly */
	{
		REGU_DSC_DEF(SOC_DVDD18_AON,soc_dvdd18_aon),
	},
	{
		REGU_DSC_DEF(SOC_AVDD33_USB3,soc_avdd33_usb3),
	},
	{
		REGU_DSC_DEF(SOC_DVDD08_AON,soc_dvdd08_aon),
	},
	{
		REGU_DSC_DEF(SOC_DVDD08_DDR,soc_dvdd08_ddr),
	},
	{
		REGU_DSC_DEF(SOC_VDD_DDR_1V8,soc_vdd_ddr_1v8),
	},
	{
		REGU_DSC_DEF(SOC_VDD_DDR_1V1,soc_vdd_ddr_1v1),
	},
	{
		REGU_DSC_DEF(SOC_VDD_DDR_0V6,soc_vdd_ddr_0v6),
	},
	{
		REGU_DSC_DEF(SOC_DVDD18_AP,soc_dvdd18_ap),
	},
	{
		REGU_DSC_DEF(SOC_DVDD08_AP,soc_dvdd08_ap),
	},
	{
		REGU_DSC_DEF(SOC_AVDD08_MIPI_HDMI,soc_avdd08_mipi_hdmi),
	},
	{
		REGU_DSC_DEF(SOC_AVDD18_MIPI_HDMI,soc_avdd18_mipi_hdmi),
	},
	{
		REGU_DSC_DEF(SOC_DVDD33_EMMC,soc_dvdd33_emmc),
	},
	{
		REGU_DSC_DEF(SOC_DVDD18_EMMC,soc_dvdd18_emmc),
	},
	{
		REGU_DSC_DEF(SOC_DOVDD18_SCAN,soc_dovdd18_scan),
	},
	{
		REGU_DSC_DEF(SOC_VEXT_2V8,soc_vext_2v8),
	},
	{
		REGU_DSC_DEF(SOC_DVDD12_SCAN,soc_dvdd12_scan),
	},
	{
		REGU_DSC_DEF(SOC_AVDD28_SCAN_EN,soc_avdd28_scan_en),
	},
	{
		REGU_DSC_DEF(SOC_AVDD28_RGB,soc_avdd28_rgb),
	},
	{
		REGU_DSC_DEF(SOC_DOVDD18_RGB,soc_dovdd18_rgb),
	},
	{
		REGU_DSC_DEF(SOC_DVDD12_RGB,soc_dvdd12_rgb),
	},
	{
		REGU_DSC_DEF(SOC_AVDD25_IR,soc_avdd25_ir),
	},
	{
		REGU_DSC_DEF(SOC_DOVDD18_IR,soc_dovdd18_ir),
	},
	{
		REGU_DSC_DEF(SOC_DVDD12_IR,soc_dvdd12_ir),
	},
	{
		REGU_DSC_DEF(SOC_LCD0_EN,soc_lcd0_en),
	},
	{
		REGU_DSC_DEF(SOC_VEXT_1V8,soc_vext_1v8),
	},
};

static const struct aon_regu_desc light_dialog_regus = {
    .regu_desc = (struct regulator_desc*) &light_dialog_regu_desc,
    .regu_num  = ARRAY_SIZE(light_dialog_regu_desc),
};

int light_match_regulator_name(struct aon_regu_desc *regus_set, const char *string)
{
    int index;
    const char *item;

    for(index = 0; index < regus_set->regu_num; index++) {
	    item = regus_set->regu_desc[index].name;
	    if(!item)
		    break;
	    if(!strcmp(item, string))
		    return index;
    }

    return -EINVAL;
}

static int light_aon_regulator_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regulator_config config = { };
	int index;
	int ret;
	struct aon_regu_desc *regus_set = NULL;
	const char *regulator_type;
	const char *regulator_name;
	struct regulator_dev *rdev;
	struct regulator_desc *desc;
	int    get_dual_rail_flag = 0;

	if (!np)
		return -ENODEV;

	regus_set = (struct aon_regu_desc*)of_device_get_match_data(&pdev->dev);
	if (!regus_set) {
		return -ENODEV;
	}

	/*get ipc handle */
	ret = light_aon_get_handle(&(light_aon_pmic_info.ipc_handle));
	if (ret) {
		dev_err(&pdev->dev, "failed to get ipc_handle\n");
		return ret;
	}

	/*init private drv data */
	light_aon_pmic_info.dev = &pdev->dev;
	light_aon_pmic_info.regu_desc = regus_set;
	light_aon_pmic_info.cpu_vol = &apcpu_volts[2]; /* pmic default voltages */
	light_aon_pmic_info.vddm = light_aon_pmic_info.cpu_vol->vddm;

	/*register regulators*/
	config.dev = &pdev->dev;
	config.driver_data = &light_aon_pmic_info;

	of_property_read_string(np, "regulator-name", &regulator_name);
	if(!regulator_name){
	    dev_err(&pdev->dev, "failed to get the regulator-name\n");
	    return -EINVAL;
	}

	of_property_read_string(np, "regulator-type", &regulator_type);
	if(!regulator_type){
	    dev_err(&pdev->dev, "failed to get the regulator-type\n");
	    return -EINVAL;
	}

	/*dual rail only work for cpu/cpu_mem*/
	if(of_property_read_bool(np, "regulator-dual-rail")){
		printk("get regual dual rail---->\n");
        get_dual_rail_flag = 1;
	}

	index = light_match_regulator_name(regus_set, regulator_name);
	if(index < 0)
	{
	    dev_err(&pdev->dev, "no regulator matches %s\n", regulator_name);
	    return -EINVAL;
	}

	desc = &regus_set->regu_desc[index];
	if(!strcmp(regulator_type, "dvdd")) {
		desc->ops = &apcpu_dvdd_ops;
		if(get_dual_rail_flag) {
            light_aon_pmic_info.cpu_dual_rail_flag = 1;
			get_dual_rail_flag = 0;
		}
	}
	else if(!strcmp(regulator_type, "dvddm")) {
		desc->ops = &apcpu_dvddm_ops;
		if(get_dual_rail_flag) {
            light_aon_pmic_info.vddm_dual_rail_flag = 1;
			get_dual_rail_flag = 0;
		}
	}
	else if(!strcmp(regulator_type, "common")) {
		desc->ops = &regu_common_ops;
	}
	else if(!strcmp(regulator_type, "gpio")) {
		desc->ops = &regu_gpio_ops;
	}
	else
	    {
    		dev_err(&pdev->dev,
		    "%s, regulator type is not clarified\n", desc->name);
		return -EINVAL;
	    }

    	rdev = devm_regulator_register(&pdev->dev, desc, &config);
	if (IS_ERR(rdev)) {
	    dev_err(&pdev->dev,
			"Failed to initialize regulator%s\n", desc->name);
			return PTR_ERR(rdev);
		}

	platform_set_drvdata(pdev, &light_aon_pmic_info);
	return 0;
}

static const struct of_device_id light_pmic_dev_id[] = {
	{ .compatible = "thead,light-dialog-pmic", .data = &light_dialog_regus},
	{},
};
MODULE_DEVICE_TABLE(of, light_pmic_dev_id);

static struct platform_driver light_aon_regulator_driver = {
	.driver = {
		   .name = "light-aon-reg",
		   .owner = THIS_MODULE,
		   .of_match_table = light_pmic_dev_id,
	},
	.probe = light_aon_regulator_probe,
};

static int __init light_aon_regulator_init(void)
{
	return platform_driver_register(&light_aon_regulator_driver);
}
postcore_initcall(light_aon_regulator_init);

static void __exit light_aon_regulator_exit(void)
{
	platform_driver_unregister(&light_aon_regulator_driver);
}
module_exit(light_aon_regulator_exit);

MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_AUTHOR("linghui.zlh <linghui.zlh@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light Aon regulator virtual driver");
MODULE_LICENSE("GPL v2");
