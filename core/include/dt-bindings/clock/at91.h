/* SPDX-License-Identifier: GPL-2.0-or-later or BSD-3-Clause  */
/*
 * Copyright (C) 2021 Microchip
 *
 * This header provides constants for AT91 pmc status.
 *
 * The constants defined in this header are being used in dts.
 */

#ifndef _DT_BINDINGS_CLK_AT91_H
#define _DT_BINDINGS_CLK_AT91_H

#define PMC_TYPE_CORE		0
#define PMC_TYPE_SYSTEM		1
#define PMC_TYPE_PERIPHERAL	2
#define PMC_TYPE_GCK		3
#define PMC_TYPE_PROGRAMMABLE	4

#define PMC_SLOW		0
#define PMC_MCK			1
#define PMC_UTMI		2
#define PMC_MAIN		3
#define PMC_MCK2		4
#define PMC_I2S0_MUX		5
#define PMC_I2S1_MUX		6
#define PMC_PLLACK		7
#define PMC_PLLBCK		8
#define PMC_AUDIOPLLCK		9
#define PMC_MCK_PRES		10
#define PMC_AUDIOPLL_FRACCK	11
#define PMC_USBCK		12
#define PMC_SAMA5D2_CORE_CLK_COUNT	13

/* SAMA7G5 */
#define PMC_CPUPLL		(PMC_MAIN + 1)
#define PMC_SYSPLL		(PMC_MAIN + 2)
#define PMC_DDRPLL		(PMC_MAIN + 3)
#define PMC_IMGPLL		(PMC_MAIN + 4)
#define PMC_BAUDPLL		(PMC_MAIN + 5)
#define PMC_AUDIOPMCPLL		(PMC_MAIN + 6)
// PMC_MCK_PRES
#define PMC_AUDIOIOPLL		(PMC_MAIN + 8)
#define PMC_ETHPLL		(PMC_MAIN + 9)
#define PMC_MCK1		(PMC_MAIN + 10)
// MCK2, MCK3, MCK4
#define PMC_SAMA7G5_CORE_CLK_COUNT	(PMC_MAIN + 14)

#define AT91_SCMI_CLK_CORE_MCK		0
#define AT91_SCMI_CLK_CORE_UTMI		1
#define AT91_SCMI_CLK_CORE_MAIN		2
#define AT91_SCMI_CLK_CORE_MCK2		3
#define AT91_SCMI_CLK_CORE_I2S0_MUX	4
#define AT91_SCMI_CLK_CORE_I2S1_MUX	5
#define AT91_SCMI_CLK_CORE_PLLACK	6
#define AT91_SCMI_CLK_CORE_PLLBCK	7
#define AT91_SCMI_CLK_CORE_AUDIOPLLCK	8
#define AT91_SCMI_CLK_CORE_MCK_PRES	9

#define AT91_SCMI_CLK_SYSTEM_DDRCK	10
#define AT91_SCMI_CLK_SYSTEM_LCDCK	11
#define AT91_SCMI_CLK_SYSTEM_UHPCK	12
#define AT91_SCMI_CLK_SYSTEM_UDPCK	13
#define AT91_SCMI_CLK_SYSTEM_PCK0	14
#define AT91_SCMI_CLK_SYSTEM_PCK1	15
#define AT91_SCMI_CLK_SYSTEM_PCK2	16
#define AT91_SCMI_CLK_SYSTEM_ISCCK	17

#define AT91_SCMI_CLK_PERIPH_MACB0_CLK		18
#define AT91_SCMI_CLK_PERIPH_TDES_CLK		19
#define AT91_SCMI_CLK_PERIPH_MATRIX1_CLK	20
#define AT91_SCMI_CLK_PERIPH_HSMC_CLK		21
#define AT91_SCMI_CLK_PERIPH_PIOA_CLK		22
#define AT91_SCMI_CLK_PERIPH_FLX0_CLK		23
#define AT91_SCMI_CLK_PERIPH_FLX1_CLK		24
#define AT91_SCMI_CLK_PERIPH_FLX2_CLK		25
#define AT91_SCMI_CLK_PERIPH_FLX3_CLK		26
#define AT91_SCMI_CLK_PERIPH_FLX4_CLK		27
#define AT91_SCMI_CLK_PERIPH_UART0_CLK		28
#define AT91_SCMI_CLK_PERIPH_UART1_CLK		29
#define AT91_SCMI_CLK_PERIPH_UART2_CLK		30
#define AT91_SCMI_CLK_PERIPH_UART3_CLK		31
#define AT91_SCMI_CLK_PERIPH_UART4_CLK		32
#define AT91_SCMI_CLK_PERIPH_TWI0_CLK		33
#define AT91_SCMI_CLK_PERIPH_TWI1_CLK		34
#define AT91_SCMI_CLK_PERIPH_SPI0_CLK		35
#define AT91_SCMI_CLK_PERIPH_SPI1_CLK		36
#define AT91_SCMI_CLK_PERIPH_TCB0_CLK		37
#define AT91_SCMI_CLK_PERIPH_TCB1_CLK		38
#define AT91_SCMI_CLK_PERIPH_PWM_CLK		39
#define AT91_SCMI_CLK_PERIPH_ADC_CLK		40
#define AT91_SCMI_CLK_PERIPH_UHPHS_CLK		41
#define AT91_SCMI_CLK_PERIPH_UDPHS_CLK		42
#define AT91_SCMI_CLK_PERIPH_SSC0_CLK		43
#define AT91_SCMI_CLK_PERIPH_SSC1_CLK		44
#define AT91_SCMI_CLK_PERIPH_TRNG_CLK		45
#define AT91_SCMI_CLK_PERIPH_PDMIC_CLK		46
#define AT91_SCMI_CLK_PERIPH_SECURAM_CLK	47
#define AT91_SCMI_CLK_PERIPH_I2S0_CLK		48
#define AT91_SCMI_CLK_PERIPH_I2S1_CLK		49
#define AT91_SCMI_CLK_PERIPH_CAN0_CLK		50
#define AT91_SCMI_CLK_PERIPH_CAN1_CLK		51
#define AT91_SCMI_CLK_PERIPH_PTC_CLK		52
#define AT91_SCMI_CLK_PERIPH_CLASSD_CLK		53
#define AT91_SCMI_CLK_PERIPH_DMA0_CLK		54
#define AT91_SCMI_CLK_PERIPH_DMA1_CLK		55
#define AT91_SCMI_CLK_PERIPH_AES_CLK		56
#define AT91_SCMI_CLK_PERIPH_AESB_CLK		57
#define AT91_SCMI_CLK_PERIPH_SHA_CLK		58
#define AT91_SCMI_CLK_PERIPH_MPDDR_CLK		59
#define AT91_SCMI_CLK_PERIPH_MATRIX0_CLK	60
#define AT91_SCMI_CLK_PERIPH_SDMMC0_HCLK	61
#define AT91_SCMI_CLK_PERIPH_SDMMC1_HCLK	62
#define AT91_SCMI_CLK_PERIPH_LCDC_CLK		63
#define AT91_SCMI_CLK_PERIPH_ISC_CLK		64
#define AT91_SCMI_CLK_PERIPH_QSPI0_CLK		65
#define AT91_SCMI_CLK_PERIPH_QSPI1_CLK		66

#define AT91_SCMI_CLK_GCK_SDMMC0_GCLK	67
#define AT91_SCMI_CLK_GCK_SDMMC1_GCLK	68
#define AT91_SCMI_CLK_GCK_TCB0_GCLK	69
#define AT91_SCMI_CLK_GCK_TCB1_GCLK	70
#define AT91_SCMI_CLK_GCK_PWM_GCLK	71
#define AT91_SCMI_CLK_GCK_ISC_GCLK	72
#define AT91_SCMI_CLK_GCK_PDMIC_GCLK	73
#define AT91_SCMI_CLK_GCK_I2S0_GCLK	74
#define AT91_SCMI_CLK_GCK_I2S1_GCLK	75
#define AT91_SCMI_CLK_GCK_CAN0_GCLK	76
#define AT91_SCMI_CLK_GCK_CAN1_GCLK	77
#define AT91_SCMI_CLK_GCK_CLASSD_GCLK	78

#define AT91_SCMI_CLK_PROG_PROG0	79
#define AT91_SCMI_CLK_PROG_PROG1	80
#define AT91_SCMI_CLK_PROG_PROG2	81

#define AT91_SCMI_CLK_SCKC_SLOWCK_32K	82

#endif
