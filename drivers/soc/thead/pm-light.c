// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */
#include <asm/cpuidle.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/types.h>
#include <asm/sbi.h>
#include <asm/suspend.h>
#include <linux/firmware/thead/ipc.h>
#include <linux/platform_device.h>

#undef pr_fmt
#define pr_fmt(fmt) "light-system-suspend" ": " fmt

struct rpc_msg_cpu_info{
	u16 cpu_id;
	u16 status;
	u32 reserved[5];
} __packed __aligned(1);

struct rpc_msg_cpu_info_ack{
	struct light_aon_rpc_ack_common ack_hdr;
	u16 cpu_id;
	u16 status;
	u32 reserved[5];
} __packed __aligned(1);

struct light_aon_msg_pm_ctrl {
	struct light_aon_rpc_msg_hdr hdr;
	union rpc_func_t {
	struct rpc_msg_cpu_info	cpu_info;
	} __packed __aligned(1) rpc;
} __packed __aligned(1);

struct light_aon_pm_ctrl {
	struct device			*dev;
	struct light_aon_ipc		*ipc_handle;
	struct light_aon_msg_pm_ctrl msg;
	bool   suspend_flag;
};

static struct light_aon_pm_ctrl *aon_pm_ctrl;

static int light_require_state_pm_ctrl(struct light_aon_msg_pm_ctrl *msg, enum light_aon_misc_func func, void* ack_msg, bool ack)
{
	pr_debug("notify aon subsys...\n");
	struct light_aon_ipc *ipc = aon_pm_ctrl->ipc_handle;
	struct light_aon_rpc_msg_hdr *hdr = &msg->hdr;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_LPM;
	hdr->func = (uint8_t)func;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	return light_aon_call_rpc(ipc, msg, ack_msg, ack);
}

static int sbi_suspend_finisher(unsigned long suspend_type,
				unsigned long resume_addr,
				unsigned long opaque)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_SUSPEND,
			suspend_type, resume_addr, opaque, 0, 0, 0);

	return (ret.error) ? sbi_err_map_linux_errno(ret.error) : 0;
}

static int light_suspend_enter(suspend_state_t state)
{
	struct light_aon_msg_pm_ctrl msg = {0};
	unsigned long suspend_type;

	if (!IS_ENABLED(CONFIG_PM))
		return 0;
	pr_debug("[%s,%d]enter platform system suspend... state:%d\n", __func__, __LINE__, state);

	if (state == PM_SUSPEND_MEM)
		suspend_type = SBI_HSM_SUSP_NON_RET_BIT;
	else
		return -EINVAL;

	cpu_suspend(suspend_type, sbi_suspend_finisher);
	pr_debug("[%s,%d]wakeup from system suspend\n",__func__, __LINE__);
	return 0;
}

static int light_suspend_prepare(void)
{
	int ret;
	aon_pm_ctrl->suspend_flag = true;
	struct light_aon_msg_pm_ctrl msg = {0};
	ret = light_require_state_pm_ctrl(&msg, LIGHT_AON_LPM_FUNC_REQUIRE_STR, NULL, false);
	if (ret) {
		pr_err("[%s,%d]failed to initiate Suspend to Ram process to AON subsystem\n",__func__, __LINE__);
		return ret;
	}
	return 0;
}

static void light_resume_finish(void)
{
	int ret;
	aon_pm_ctrl->suspend_flag = false;
	struct light_aon_msg_pm_ctrl msg = {0};
	struct light_aon_rpc_ack_common ack_msg = {0};
	ret = light_require_state_pm_ctrl(&msg, LIGHT_AON_LPM_FUNC_RESUME_STR, &ack_msg, true);
	if (ret) {
		pr_err("[%s,%d]failed to clear lowpower state\n",__func__, __LINE__);
	}
}

static int thead_cpuhp_offline(unsigned int cpu)
{
	int ret;
	if(!aon_pm_ctrl->suspend_flag)
	{
		struct light_aon_msg_pm_ctrl msg = {0};
		struct light_aon_rpc_ack_common ack_msg = {0};

		RPC_SET_BE16(&msg.rpc.cpu_info.cpu_id, 0, (u16)cpu);
		RPC_SET_BE16(&msg.rpc.cpu_info.cpu_id, 2, 0);
		ret = light_require_state_pm_ctrl(&msg, LIGHT_AON_LPM_FUNC_CPUHP, &ack_msg, true);
		if (ret) {
			pr_info("failed to notify aon subsys with cpuhp...%08x\n", ret);
			return ret;
		}
	}
	return 0;
}

static int thead_cpuhp_online(unsigned int cpu)
{
	int ret;
	if(!aon_pm_ctrl->suspend_flag)
	{
		struct light_aon_msg_pm_ctrl msg = {0};
        struct light_aon_rpc_ack_common ack_msg = {0};
		RPC_SET_BE16(&msg.rpc.cpu_info.cpu_id, 0, (u16)cpu);
		RPC_SET_BE16(&msg.rpc.cpu_info.cpu_id, 2,  1);
		ret = light_require_state_pm_ctrl(&msg, LIGHT_AON_LPM_FUNC_CPUHP, &ack_msg, true);
		if (ret) {
			pr_info("[%s,%d]failed to bring up aon subsys with cpuhp...%08x\n", __func__, __LINE__, ret);
			return ret;
		}
	}
	return 0;
}

static const struct of_device_id aon_ctrl_ids[] = {
	{ .compatible = "thead,light-aon-suspend-ctrl" },
	{}
};

static const struct platform_suspend_ops light_suspend_ops = {
	.enter = light_suspend_enter,
	.valid = suspend_valid_only_mem,
	.prepare_late = light_suspend_prepare,
	.end    = light_resume_finish,
};

#define C906_RESET_REG                  0xfffff4403c

static void boot_audio(void) {
	uint64_t *v_addr = ioremap(C906_RESET_REG, 4);
	if(!v_addr) {
		printk("io remap failed\r\n");
		return;
	}
	writel(0x37, (volatile void *)v_addr);
	writel(0x3f, (volatile void *)v_addr);
	iounmap(C906_RESET_REG);
}

//this is called after dpm_suspend_end,before snapshot
static int light_hibernation_pre_snapshot(void)
{
	return 0;
}
//called before dpm_resume_start after slave cores up
static void light_hibernation_platform_finish(void)
{
	boot_audio();
	return;
}

static const struct platform_hibernation_ops light_hibernation_allmode_ops = {
	.pre_snapshot = light_hibernation_pre_snapshot,
	.finish = light_hibernation_platform_finish,
};

int  get_audio_text_mem(struct device *dev, phys_addr_t* mem, size_t* mem_size)
{
    struct resource r;
	ssize_t fw_size;
	void *mem_va;
	struct device_node *node;
	int ret;

	*mem = 0;
	*mem_size = 0;

	node = of_parse_phandle(dev->of_node, "audio-text-memory-region", 0);
	if (!node) {
		dev_err(dev, "no audio-text-memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret) {
	    dev_err(dev, "audio-text-memory-region get resource faild\n");
		return -EINVAL;
	}

	*mem = r.start;
	*mem_size = resource_size(&r);
    return 0;
}

static int light_pm_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	int ret;
	struct light_aon_pm_ctrl	*pm_ctrl;

	pm_ctrl = devm_kzalloc(&pdev->dev, sizeof(*aon_pm_ctrl), GFP_KERNEL);
	if (!pm_ctrl)
		return -ENOMEM;
	aon_pm_ctrl = pm_ctrl;

	ret = light_aon_get_handle(&(aon_pm_ctrl->ipc_handle));
	if (ret == -EPROBE_DEFER)
		return ret;

	suspend_set_ops(&light_suspend_ops);

	/*only save BSS and data section for audio*/
	phys_addr_t audio_text_mem;
	size_t audio_text_mem_size;
	ret = get_audio_text_mem(dev, &audio_text_mem, &audio_text_mem_size);
	if(ret) {
       return ret;
	} else {
		printk("Get audio text phy mem:0x%011x, size:%d\n", audio_text_mem, audio_text_mem_size);
	}
	hibernate_register_nosave_region(__phys_to_pfn(audio_text_mem), __phys_to_pfn(audio_text_mem + audio_text_mem_size));
	hibernation_set_allmode_ops(&light_hibernation_allmode_ops);

	ret = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN, "soc/thead:online",
			thead_cpuhp_online,
			thead_cpuhp_offline);
	if(ret < 0) {
		pr_err("[%s,%d]failed to register hotplug callbacks with err %08x.\n", __func__, __LINE__, ret);
		return ret;
	}

	aon_pm_ctrl->dev = &pdev->dev;
	aon_pm_ctrl->suspend_flag = false;
	platform_set_drvdata(pdev, aon_pm_ctrl);

	dev_info(&pdev->dev, "Light power management control sys successfully registered\n");
	return 0;
}

static struct platform_driver light_pm_driver = {
	.driver = {
		.name	= "light-pm",
		.of_match_table = aon_ctrl_ids,
	},
	.probe = light_pm_probe,
};

module_platform_driver(light_pm_driver);
