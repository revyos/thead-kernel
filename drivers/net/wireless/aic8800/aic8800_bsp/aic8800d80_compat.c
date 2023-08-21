#include "aic8800d80_compat.h"
#include "aic_bsp_driver.h"

extern struct aicbsp_info_t aicbsp_info;
extern int adap_test;

u32 aicbsp_syscfg_tbl_8800d80[][2] = {
/*
	{0x40500014, 0x00000101}, // 1)
	{0x40500018, 0x00000109}, // 2)
	{0x40500004, 0x00000010}, // 3) the order should not be changed

	// def CONFIG_PMIC_SETTING
	// U02 bootrom only
	{0x40040000, 0x00001AC8}, // 1) fix panic
	{0x40040084, 0x00011580},
	{0x40040080, 0x00000001},
	{0x40100058, 0x00000000},

	{0x50000000, 0x03220204}, // 2) pmic interface init
	{0x50019150, 0x00000002}, // 3) for 26m xtal, set div1
	{0x50017008, 0x00000000}, // 4) stop wdg
*/
};

int aicbsp_system_config_8800d80(struct aic_sdio_dev *sdiodev)
{
	int syscfg_num = sizeof(aicbsp_syscfg_tbl_8800d80) / sizeof(u32) / 2;
	int ret, cnt;
	for (cnt = 0; cnt < syscfg_num; cnt++) {
		ret = rwnx_send_dbg_mem_write_req(sdiodev, aicbsp_syscfg_tbl_8800d80[cnt][0], aicbsp_syscfg_tbl_8800d80[cnt][1]);
		if (ret) {
			printk("%x write fail: %d\n", aicbsp_syscfg_tbl_8800d80[cnt][0], ret);
			return ret;
		}
	}
	return 0;
}


u32 adaptivity_patch_tbl_8800d80[][2] = {
/*
	{0x0004, 0x0000320A}, //linkloss_thd
    {0x0094, 0x00000000}, //ac_param_conf
	{0x00F8, 0x00010138}, //tx_adaptivity_en
*/
};

u32 patch_tbl_8800d80[][2] = {
/*
#if !defined(CONFIG_LINK_DET_5G)
    {0x0104, 0x00000000}, //link_det_5g
#endif
#if defined(CONFIG_MCU_MESSAGE)
    {0x004c, 0x0000004B}, //pkt_cnt_1724=0x4B
    {0x0050, 0x0011FC00}, //ipc_base_addr
#endif
*/
};

u32 syscfg_tbl_masked_8800d80[][3] = {
	{0x40506024, 0x000000FF, 0x000000DF}, // for clk gate lp_level
};

u32 rf_tbl_masked_8800d80[][3] = {
	{0x40344058, 0x00800000, 0x00000000},// pll trx
};

int aicwifi_sys_config_8800d80(struct aic_sdio_dev *sdiodev)
{
	int ret, cnt;
	int syscfg_num = sizeof(syscfg_tbl_masked_8800d80) / sizeof(u32) / 3;
	for (cnt = 0; cnt < syscfg_num; cnt++) {
		ret = rwnx_send_dbg_mem_mask_write_req(sdiodev,
			syscfg_tbl_masked_8800d80[cnt][0], syscfg_tbl_masked_8800d80[cnt][1], syscfg_tbl_masked_8800d80[cnt][2]);
		if (ret) {
			printk("%x mask write fail: %d\n", syscfg_tbl_masked_8800d80[cnt][0], ret);
			return ret;
		}
	}

	ret = rwnx_send_dbg_mem_mask_write_req(sdiodev,
				rf_tbl_masked_8800d80[0][0], rf_tbl_masked_8800d80[0][1], rf_tbl_masked_8800d80[0][2]);
	if (ret) {
		printk("rf config %x write fail: %d\n", rf_tbl_masked_8800d80[0][0], ret);
		return ret;
	}

	return 0;
}

int aicwifi_patch_config_8800d80(struct aic_sdio_dev *sdiodev)
{
	const u32 rd_patch_addr = RAM_FMAC_FW_ADDR + 0x0180;
	u32 config_base;
	uint32_t start_addr = 0x1e6000;
	u32 patch_addr = start_addr;
	u32 patch_num = sizeof(patch_tbl_8800d80)/4;
	struct dbg_mem_read_cfm rd_patch_addr_cfm;
	u32 patch_addr_reg = 0x1e5318;
	u32 patch_num_reg = 0x1e531c;
	int ret = 0;
	u16 cnt = 0;
	int tmp_cnt = 0;
	int adap_patch_num = 0;

	if (aicbsp_info.cpmode == AICBSP_CPMODE_TEST) {
		patch_addr_reg = 0x1e5304;
		patch_num_reg = 0x1e5308;
	}

	ret = rwnx_send_dbg_mem_read_req(sdiodev, rd_patch_addr, &rd_patch_addr_cfm);
	if (ret) {
		printk("patch rd fail\n");
		return ret;
	}

	config_base = rd_patch_addr_cfm.memdata;

	ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_addr_reg, patch_addr);
	if (ret) {
		printk("0x%x write fail\n", patch_addr_reg);
		return ret;
	}

	if(adap_test){
		printk("%s for adaptivity test \r\n", __func__);
		adap_patch_num = sizeof(adaptivity_patch_tbl_8800d80)/4;
		ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_num_reg, patch_num + adap_patch_num);
	}else{
		ret = rwnx_send_dbg_mem_write_req(sdiodev, patch_num_reg, patch_num);
	}
	if (ret) {
		printk("0x%x write fail\n", patch_num_reg);
		return ret;
	}

	for (cnt = 0; cnt < patch_num/2; cnt += 1) {
		ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr+8*cnt, patch_tbl_8800d80[cnt][0]+config_base);
		if (ret) {
			printk("%x write fail\n", start_addr+8*cnt);
			return ret;
		}

		ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr+8*cnt+4, patch_tbl_8800d80[cnt][1]);
		if (ret) {
			printk("%x write fail\n", start_addr+8*cnt+4);
			return ret;
		}
	}

	tmp_cnt = cnt;
	
	if(adap_test){
		for(cnt = 0; cnt < adap_patch_num/2; cnt+=1)
		{
			if((ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr+8*(cnt+tmp_cnt), adaptivity_patch_tbl_8800d80[cnt][0]+config_base))) {
				printk("%x write fail\n", start_addr+8*cnt);
			}
		
			if((ret = rwnx_send_dbg_mem_write_req(sdiodev, start_addr+8*(cnt+tmp_cnt)+4, adaptivity_patch_tbl_8800d80[cnt][1]))) {
				printk("%x write fail\n", start_addr+8*cnt+4);
			}
		}
	}

	return 0;
}


