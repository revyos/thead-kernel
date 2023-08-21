#ifndef _AIC8800DC_COMPAT_H_
#define _AIC8800DC_COMPAT_H_

#include "aicsdio.h"
typedef u32 (*array2_tbl_t)[2];

typedef uint8_t u8_l;
typedef int8_t s8_l;
typedef bool bool_l;
typedef uint16_t u16_l;
typedef int16_t s16_l;
typedef uint32_t u32_l;
typedef int32_t s32_l;
typedef uint64_t u64_l;

extern u8 chip_sub_id;
extern u8 chip_mcu_id;

void aicwf_patch_config_8800dc(struct          aic_sdio_dev *rwnx_hw);
void system_config_8800dc(struct aic_sdio_dev *rwnx_hw);


#endif



