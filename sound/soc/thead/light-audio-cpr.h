#ifndef _LIGHT_AUDIO_CPR_H
#define _LIGHT_AUDIO_CPR_H

#define CPR_PERI_DIV_SEL_REG  0x004 /*audio sys i2s clock div Register*/
#define CPR_PERI_CLK_SEL_REG  0x008 /*audio sys i2s clock selection Register*/
#define CPR_PERI_CTRL_REG  0x00C /*Peripheral control signal configuration register*/


/* AUDIO SYS DIV SEL REG, offset: 0x4 */
#define CPR_AUDIO_DIV0_SEL_POS                     (4U)
#define CPR_AUDIO_DIV0_SEL_MSK                     (0x1FU << CPR_AUDIO_DIV0_SEL_POS)
#define CPR_AUDIO_DIV0_SEL(X)                      (X << CPR_AUDIO_DIV0_SEL_POS)
#define CPR_AUDIO_DIV1_SEL_POS                     (12U)
#define CPR_AUDIO_DIV1_SEL_MSK                     (0x1FU << CPR_AUDIO_DIV1_SEL_POS)
#define CPR_AUDIO_DIV1_SEL(X)                      (X << CPR_AUDIO_DIV1_SEL_POS)

/* AUDIO SYS CLK SEL REG, offset: 0x8 */
#define CPR_I2S0_SRC_SEL_POS                   (0U)
#define CPR_I2S0_SRC_SEL_MSK                   (0x3U << CPR_I2S0_SRC_SEL_POS)
#define CPR_I2S0_SRC_SEL(X)                    (X << CPR_I2S0_SRC_SEL_POS)
#define CPR_I2S0_SRC_SEL_24M                   (0x1U << AUDIOSYS_I2S0_SRC_SEL_POS)
#define CPR_I2S0_SRC_SEL_AUDIO_DIVCLK1         (0x2U << AUDIOSYS_I2S0_SRC_SEL_POS)

#define CPR_I2S1_SRC_SEL_POS                   (4U)
#define CPR_I2S1_SRC_SEL_MSK                   (0x3U << CPR_I2S1_SRC_SEL_POS)
#define CPR_I2S1_SRC_SEL(X)                    (X << CPR_I2S1_SRC_SEL_POS)
#define CPR_I2S1_SRC_SEL_24M                   (0x1U << AUDIOSYS_I2S1_SRC_SEL_POS)
#define CPR_I2S1_SRC_SEL_AUDIO_DIVCLK1         (0x2U << AUDIOSYS_I2S1_SRC_SEL_POS)

#define CPR_I2S2_SRC_SEL_POS                   (8U)
#define CPR_I2S2_SRC_SEL_MSK                   (0x3U << CPR_I2S2_SRC_SEL_POS)
#define CPR_I2S2_SRC_SEL(X)                    (X << CPR_I2S2_SRC_SEL_POS)
#define CPR_I2S2_SRC_SEL_24M                   (0x1U << AUDIOSYS_I2S2_SRC_SEL_POS)
#define CPR_I2S2_SRC_SEL_AUDIO_DIVCLK1         (0x2U << AUDIOSYS_I2S2_SRC_SEL_POS)

/* PERI_CTRL_REG, Offset: 0xC */
#define CPR_VAD_I2SIN_SYNC_POS            (12U)
#define CPR_VAD_I2SIN_SYNC_MSK            (0x1U << CPR_VAD_I2SIN_SYNC_POS)
#define CPR_VAD_I2SIN_SYNC_EN             (CPR_VAD_I2SIN_SYNC_MSK)
#define CPR_I2S_SYNC_POS                  (13U)
#define CPR_I2S_SYNC_MSK                  (0x1U << CPR_I2S_SYNC_POS)
#define CPR_I2S_SYNC_EN                   (CPR_I2S_SYNC_MSK)
#define CPR_SPDIF_SYNC_POS                (14U)
#define CPR_SPDIF_SYNC_MSK                (0x1U << CPR_SPDIF_SYNC_POS)
#define CPR_SPDIF_SYNC_EN                 (CPR_SPDIF_SYNC_MSK)


#endif /* _LIGHT_AUDIO_CPR_H */

