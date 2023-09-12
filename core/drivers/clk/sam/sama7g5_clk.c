// SPDX-License-Identifier: GPL-2.0+ or BSD-3-Clause
/*
 * Copyright (c) 2021, Microchip
 */
#include <assert.h>
#include <kernel/boot.h>
#include <libfdt.h>
#include <kernel/dt.h>
#include <kernel/panic.h>
#include <matrix.h>
#include <sama7g5.h>
#include <stdint.h>
#include <util.h>
#include <io.h>

#include "at91_clk.h"

#include <dt-bindings/clock/at91.h>

#define CLK_IS_CRITICAL 0

enum main_system_bus_clock {
	MSBC_MCK0,
	MSBC_MCK1,
	MSBC_MCK2,
	MSBC_MCK3,
	MSBC_GCLK,
};

/*
 * PLL clocks identifiers
 * @PLL_ID_CPU:		CPU PLL identifier
 * @PLL_ID_SYS:		System PLL identifier
 * @PLL_ID_DDR:		DDR PLL identifier
 * @PLL_ID_IMG:		Image subsystem PLL identifier
 * @PLL_ID_BAUD:	Baud PLL identifier
 * @PLL_ID_AUDIO:	Audio PLL identifier
 * @PLL_ID_ETH:		Ethernet PLL identifier
 */
enum pll_ids {
	PLL_ID_CPU,
	PLL_ID_SYS,
	PLL_ID_DDR,
	PLL_ID_IMG,
	PLL_ID_BAUD,
	PLL_ID_AUDIO,
	PLL_ID_ETH,
	PLL_ID_MAX,
};

/*
 * PLL type identifiers
 * @PLL_TYPE_FRAC:	fractional PLL identifier
 * @PLL_TYPE_DIV:	divider PLL identifier
 */
enum pll_type {
	PLL_TYPE_FRAC,
	PLL_TYPE_DIV,
	PLL_TYPE_CNT,
};

/* Layout for fractional PLLs. */
static const struct clk_pll_layout pll_layout_frac = {
	.mul_mask	= GENMASK_32(31, 24),
	.frac_mask	= GENMASK_32(21, 0),
	.mul_shift	= 24,
	.frac_shift	= 0,
};

/* Layout for DIVPMC dividers. */
static const struct clk_pll_layout pll_layout_divpmc = {
	.div_mask	= GENMASK_32(7, 0),
	.endiv_mask	= BIT(29),
	.div_shift	= 0,
	.endiv_shift	= 29,
};

/* Layout for DIVIO dividers. */
static const struct clk_pll_layout pll_layout_divio = {
	.div_mask	= GENMASK_32(19, 12),
	.endiv_mask	= BIT(30),
	.div_shift	= 12,
	.endiv_shift	= 30,
};

/*
 * CPU PLL output range.
 * Notice: The upper limit has been setup to 1000000002 due to hardware
 * block which cannot output exactly 1GHz.
 */
static const struct clk_range cpu_pll_outputs[] = {
	{ .min = 2343750, .max = 1000000002 },
};

/* PLL output range. */
static const struct clk_range pll_outputs[] = {
	{ .min = 2343750, .max = 1200000000 },
};

/* CPU PLL characteristics. */
static const struct clk_pll_charac cpu_pll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(cpu_pll_outputs),
	.output = cpu_pll_outputs,
};

/* PLL characteristics. */
static const struct clk_pll_charac pll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(pll_outputs),
	.output = pll_outputs,
};

/*
 * PLL clocks description
 * @n:		clock name
 * @p:		clock parent
 * @l:		clock layout
 * @c:		clock characteristics
 * @t:		clock type
 * @f:		clock flags
 * @eid:	export index in sama7g5->chws[] array
 * @safe_div:	intermediate divider need to be set on PRE_RATE_CHANGE
 *		notification
 */
struct sama7g5_pll {
	const char *n;
	const char *p;
	const struct clk_pll_layout *l;
	const struct clk_pll_charac *c;
	unsigned long f;
	uint8_t t;
	uint8_t eid;
	uint8_t safe_div;
};

static const struct sama7g5_pll sama7g5_plls[][PLL_ID_MAX] = {
	[PLL_ID_CPU] = {
		{ .n = "cpupll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .c = &cpu_pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		   /*
		    * This feeds cpupll_divpmcck which feeds CPU. It should
		    * not be disabled.
		    */
		  .f = CLK_IS_CRITICAL, },

		{ .n = "cpupll_divpmcck",
		  .p = "cpupll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &cpu_pll_characteristics,
		  .t = PLL_TYPE_DIV,
		   /* This feeds CPU. It should not be disabled. */
		  .f = CLK_IS_CRITICAL,
		  .eid = PMC_CPUPLL,
		  /*
		   * Safe div=15 should be safe even for switching b/w 1GHz and
		   * 90MHz (frac pll might go up to 1.2GHz).
		   */
		  .safe_div = 15, },
	},

	[PLL_ID_SYS] = {
		{ .n = "syspll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		   /*
		    * This feeds syspll_divpmcck which may feed critical parts
		    * of the systems like timers. Therefore it should not be
		    * disabled.
		    */
		  .f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE, },

		{ .n = "syspll_divpmcck",
		  .p = "syspll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		   /*
		    * This may feed critical parts of the systems like timers.
		    * Therefore it should not be disabled.
		    */
		  .f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
		  .eid = PMC_SYSPLL, },
	},

	[PLL_ID_DDR] = {
		{ .n = "ddrpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		   /*
		    * This feeds ddrpll_divpmcck which feeds DDR. It should not
		    * be disabled.
		    */
		  .f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE, },

		{ .n = "ddrpll_divpmcck",
		  .p = "ddrpll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		   /* This feeds DDR. It should not be disabled. */
		  .f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
		  .eid = PMC_DDRPLL, },
	},

	[PLL_ID_IMG] = {
		{ .n = "imgpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		  .f = CLK_SET_RATE_GATE, },

		{ .n = "imgpll_divpmcck",
		  .p = "imgpll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		  .f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,
		  .eid = PMC_IMGPLL, },
	},

	[PLL_ID_BAUD] = {
		{ .n = "baudpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		  .f = CLK_SET_RATE_GATE, },

		{ .n = "baudpll_divpmcck",
		  .p = "baudpll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		  .f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,
		  .eid = PMC_BAUDPLL, },
	},

	[PLL_ID_AUDIO] = {
		{ .n = "audiopll_fracck",
		  .p = "main_xtal",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		  .f = CLK_SET_RATE_GATE, },

		{ .n = "audiopll_divck",
		  .p = "audiopll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		  .f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,
		  .eid = PMC_AUDIOPMCPLL, },

		{ .n = "audiopll_diviock",
		  .p = "audiopll_fracck",
		  .l = &pll_layout_divio,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		  .f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,
		  .eid = PMC_AUDIOIOPLL, },
	},

	[PLL_ID_ETH] = {
		{ .n = "ethpll_fracck",
		  .p = "main_xtal",
		  .l = &pll_layout_frac,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_FRAC,
		  .f = CLK_SET_RATE_GATE, },

		{ .n = "ethpll_divpmcck",
		  .p = "ethpll_fracck",
		  .l = &pll_layout_divpmc,
		  .c = &pll_characteristics,
		  .t = PLL_TYPE_DIV,
		  .f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,
		  .eid = PMC_ETHPLL, },
	},
};

/*
 * Master clock (MCK[1..4]) description
 * @n:			clock name
 * @ep:			extra parents names array
 * @ep_chg_chg_id:	index in parents array that specifies the changeable
 *			parent
 * @ep_count:		extra parents count
 * @ep_mux_table:	mux table for extra parents
 * @id:			clock id
 * @eid:		export index in sama7g5->chws[] array
 */
struct sama7g5_mck {
	const char *n;
	const char *ep[4];
	int ep_chg_id;
	uint8_t ep_count;
	uint8_t ep_mux_table[4];
	uint8_t id;
	uint8_t eid;
};

static const struct sama7g5_mck sama7g5_mckx[] = {
	{ .n = "mck1",
	  .id = 1,
	  .ep = { "syspll_divpmcck", },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .eid = PMC_MCK1, },

	{ .n = "mck2",
	  .id = 2,
	  .ep = { "ddrpll_divpmcck", },
	  .ep_mux_table = { 6, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN, },

	{ .n = "mck3",
	  .id = 3,
	  .ep = { "syspll_divpmcck", "ddrpll_divpmcck", "imgpll_divpmcck", },
	  .ep_mux_table = { 5, 6, 7, },
	  .ep_count = 3,
	  .ep_chg_id = 5, },

	{ .n = "mck4",
	  .id = 4,
	  .ep = { "syspll_divpmcck", },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN, },
};

/*
 * System clock description
 * @n:	clock name
 * @p:	clock parent name
 * @id: clock id
 */
static const struct {
	const char *n;
	const char *p;
	uint8_t id;
} sama7g5_systemck[] = {
	{ .n = "pck0",		.p = "prog0", .id = 8, },
	{ .n = "pck1",		.p = "prog1", .id = 9, },
	{ .n = "pck2",		.p = "prog2", .id = 10, },
	{ .n = "pck3",		.p = "prog3", .id = 11, },
	{ .n = "pck4",		.p = "prog4", .id = 12, },
	{ .n = "pck5",		.p = "prog5", .id = 13, },
	{ .n = "pck6",		.p = "prog6", .id = 14, },
	{ .n = "pck7",		.p = "prog7", .id = 15, },
};

/*
 * Peripheral clock description
 * @n:		clock name
 * @p:		clock parent name
 * @r:		clock range values
 * @id:		clock id
 * @chgp:	index in parent array of the changeable parent
 */
static const struct {
	const char *n;
	const char *p;
	struct clk_range r;
	uint8_t chgp;
	uint8_t id;
} sama7g5_perick[] = {
	{ .n = "pioA_clk",	.p = "mck0", .id = 11, },
	{ .n = "securam_clk",	.p = "mck0", .id = 18, },
	{ .n = "sfr_clk",	.p = "mck1", .id = 19, },
	{ .n = "hsmc_clk",	.p = "mck1", .id = 21, },
	{ .n = "xdmac0_clk",	.p = "mck1", .id = 22, },
	{ .n = "xdmac1_clk",	.p = "mck1", .id = 23, },
	{ .n = "xdmac2_clk",	.p = "mck1", .id = 24, },
	{ .n = "acc_clk",	.p = "mck1", .id = 25, },
	{ .n = "aes_clk",	.p = "mck1", .id = 27, },
	{ .n = "tzaesbasc_clk",	.p = "mck1", .id = 28, },
	{ .n = "asrc_clk",	.p = "mck1", .id = 30,
	  .r = { .max = 200000000, }, },
	{ .n = "cpkcc_clk",	.p = "mck0", .id = 32, },
	{ .n = "csi_clk",	.p = "mck3", .id = 33,
	  .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "csi2dc_clk",	.p = "mck3", .id = 34,
	  .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "eic_clk",	.p = "mck1", .id = 37, },
	{ .n = "flex0_clk",	.p = "mck1", .id = 38, },
	{ .n = "flex1_clk",	.p = "mck1", .id = 39, },
	{ .n = "flex2_clk",	.p = "mck1", .id = 40, },
	{ .n = "flex3_clk",	.p = "mck1", .id = 41, },
	{ .n = "flex4_clk",	.p = "mck1", .id = 42, },
	{ .n = "flex5_clk",	.p = "mck1", .id = 43, },
	{ .n = "flex6_clk",	.p = "mck1", .id = 44, },
	{ .n = "flex7_clk",	.p = "mck1", .id = 45, },
	{ .n = "flex8_clk",	.p = "mck1", .id = 46, },
	{ .n = "flex9_clk",	.p = "mck1", .id = 47, },
	{ .n = "flex10_clk",	.p = "mck1", .id = 48, },
	{ .n = "flex11_clk",	.p = "mck1", .id = 49, },
	{ .n = "gmac0_clk",	.p = "mck1", .id = 51, },
	{ .n = "gmac1_clk",	.p = "mck1", .id = 52, },
	{ .n = "icm_clk",	.p = "mck1", .id = 55, },
	{ .n = "isc_clk",	.p = "mck3", .id = 56,
	  .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "i2smcc0_clk",	.p = "mck1", .id = 57,
	  .r = { .max = 200000000, }, },
	{ .n = "i2smcc1_clk",	.p = "mck1", .id = 58,
	  .r = { .max = 200000000, }, },
	{ .n = "matrix_clk",	.p = "mck1", .id = 60, },
	{ .n = "mcan0_clk",	.p = "mck1", .id = 61,
	  .r = { .max = 200000000, }, },
	{ .n = "mcan1_clk",	.p = "mck1", .id = 62,
	  .r = { .max = 200000000, }, },
	{ .n = "mcan2_clk",	.p = "mck1", .id = 63,
	  .r = { .max = 200000000, }, },
	{ .n = "mcan3_clk",	.p = "mck1", .id = 64,
	  .r = { .max = 200000000, }, },
	{ .n = "mcan4_clk",	.p = "mck1", .id = 65,
	  .r = { .max = 200000000, }, },
	{ .n = "mcan5_clk",	.p = "mck1", .id = 66,
	  .r = { .max = 200000000, }, },
	{ .n = "pdmc0_clk",	.p = "mck1", .id = 68,
	  .r = { .max = 200000000, }, },
	{ .n = "pdmc1_clk",	.p = "mck1", .id = 69,
	  .r = { .max = 200000000, }, },
	{ .n = "pit64b0_clk",	.p = "mck1", .id = 70, },
	{ .n = "pit64b1_clk",	.p = "mck1", .id = 71, },
	{ .n = "pit64b2_clk",	.p = "mck1", .id = 72, },
	{ .n = "pit64b3_clk",	.p = "mck1", .id = 73, },
	{ .n = "pit64b4_clk",	.p = "mck1", .id = 74, },
	{ .n = "pit64b5_clk",	.p = "mck1", .id = 75, },
	{ .n = "pwm_clk",	.p = "mck1", .id = 77, },
	{ .n = "qspi0_clk",	.p = "mck1", .id = 78, },
	{ .n = "qspi1_clk",	.p = "mck1", .id = 79, },
	{ .n = "sdmmc0_clk",	.p = "mck1", .id = 80, },
	{ .n = "sdmmc1_clk",	.p = "mck1", .id = 81, },
	{ .n = "sdmmc2_clk",	.p = "mck1", .id = 82, },
	{ .n = "sha_clk",	.p = "mck1", .id = 83, },
	{ .n = "spdifrx_clk",	.p = "mck1", .id = 84,
	  .r = { .max = 200000000, }, },
	{ .n = "spdiftx_clk",	.p = "mck1", .id = 85,
	  .r = { .max = 200000000, }, },
	{ .n = "ssc0_clk",	.p = "mck1", .id = 86,
	  .r = { .max = 200000000, }, },
	{ .n = "ssc1_clk",	.p = "mck1", .id = 87,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch0_clk",	.p = "mck1", .id = 88,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch1_clk",	.p = "mck1", .id = 89,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch2_clk",	.p = "mck1", .id = 90,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch0_clk",	.p = "mck1", .id = 91,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch1_clk",	.p = "mck1", .id = 92,
	  .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch2_clk",	.p = "mck1", .id = 93,
	  .r = { .max = 200000000, }, },
	{ .n = "tcpca_clk",	.p = "mck1", .id = 94, },
	{ .n = "tcpcb_clk",	.p = "mck1", .id = 95, },
	{ .n = "tdes_clk",	.p = "mck1", .id = 96, },
	{ .n = "trng_clk",	.p = "mck1", .id = 97, },
	{ .n = "udphsa_clk",	.p = "mck1", .id = 104, },
	{ .n = "udphsb_clk",	.p = "mck1", .id = 105, },
	{ .n = "uhphs_clk",	.p = "mck1", .id = 106, },
};

/*
 * Generic clock description
 * @n:			clock name
 * @pp:			PLL parents
 * @pp_mux_table:	PLL parents mux table
 * @r:			clock output range
 * @pp_chg_id:		id in parent array of changeable PLL parent
 * @pp_count:		PLL parents count
 * @id:			clock id
 */
struct sama7g5_gck {
	const char *n;
	const char *pp[8];
	const char pp_mux_table[8];
	struct clk_range r;
	int pp_chg_id;
	uint8_t pp_count;
	uint8_t id;
};

static const struct sama7g5_gck sama7g5_gcks[] = {
	{ .n  = "adc_gclk",
	  .id = 26,
	  .r = { .max = 100000000, },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 7, 9, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "asrc_gclk",
	  .id = 30,
	  .r = { .max = 200000000 },
	  .pp = { "audiopll_divck", },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "csi_gclk",
	  .id = 33,
	  .r = { .max = 27000000  },
	  .pp = { "ddrpll_divpmcck", "imgpll_divpmcck", },
	  .pp_mux_table = { 6, 7, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex0_gclk",
	  .id = 38,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex1_gclk",
	  .id = 39,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex2_gclk",
	  .id = 40,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex3_gclk",
	  .id = 41,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex4_gclk",
	  .id = 42,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex5_gclk",
	  .id = 43,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex6_gclk",
	  .id = 44,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex7_gclk",
	  .id = 45,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex8_gclk",
	  .id = 46,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex9_gclk",
	  .id = 47,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex10_gclk",
	  .id = 48,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex11_gclk",
	  .id = 49,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac0_gclk",
	  .id = 51,
	  .r = { .max = 125000000 },
	  .pp = { "ethpll_divpmcck", },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = 3, },

	{ .n  = "gmac1_gclk",
	  .id = 52,
	  .r = { .max = 50000000  },
	  .pp = { "ethpll_divpmcck", },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac0_tsu_gclk",
	  .id = 53,
	  .r = { .max = 300000000 },
	  .pp = { "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 9, 10, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac1_tsu_gclk",
	  .id = 54,
	  .r = { .max = 300000000 },
	  .pp = { "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 9, 10, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "i2smcc0_gclk",
	  .id = 57,
	  .r = { .max = 100000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "i2smcc1_gclk",
	  .id = 58,
	  .r = { .max = 100000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan0_gclk",
	  .id = 61,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan1_gclk",
	  .id = 62,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan2_gclk",
	  .id = 63,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan3_gclk",
	  .id = 64,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan4_gclk",
	  .id = 65,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan5_gclk",
	  .id = 66,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pdmc0_gclk",
	  .id = 68,
	  .r = { .max = 50000000  },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pdmc1_gclk",
	  .id = 69,
	  .r = { .max = 50000000, },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b0_gclk",
	  .id = 70,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b1_gclk",
	  .id = 71,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b2_gclk",
	  .id = 72,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b3_gclk",
	  .id = 73,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b4_gclk",
	  .id = 74,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b5_gclk",
	  .id = 75,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi0_gclk",
	  .id = 78,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi1_gclk",
	  .id = 79,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "sdmmc0_gclk",
	  .id = 80,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "sdmmc1_gclk",
	  .id = 81,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "sdmmc2_gclk",
	  .id = 82,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "spdifrx_gclk",
	  .id = 84,
	  .r = { .max = 150000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n = "spdiftx_gclk",
	  .id = 85,
	  .r = { .max = 25000000  },
	  .pp = { "syspll_divpmcck", "audiopll_divck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcb0_ch0_gclk",
	  .id = 88,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcb1_ch0_gclk",
	  .id = 91,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcpca_gclk",
	  .id = 94,
	  .r = { .max = 32768, },
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcpcb_gclk",
	  .id = 95,
	  .r = { .max = 32768, },
	  .pp_chg_id = INT_MIN, },
};

/* MCK0 characteristics. */
static const struct clk_master_charac mck0_characteristics = {
	.output = { .min = 32768, .max = 200000000 },
	.divisors = { 1, 2, 4, 3, 5 },
	.have_div3_pres = 1,
};

/* MCK0 layout. */
static const struct clk_master_layout mck0_layout = {
	.mask = 0x773,
	.pres_shift = 4,
	.offset = 0x28,
};

/* Peripheral clock layout. */
static const struct clk_pcr_layout sama7g5_pcr_layout = {
	.offset = 0x88,
	.cmd = BIT(31),
	.div_mask = GENMASK_32(27, 20),
	.gckcss_mask = GENMASK_32(12, 8),
	.pid_mask = GENMASK_32(6, 0),
};

static const struct clk_programmable_layout sama7g5_prog_layout = {
	.pres_mask = 0xff,
	.pres_shift = 8,
	.css_mask = 0x1f,
	.have_slck_mck = 0,
	.is_pres_direct = 1,
};

static const struct {
	const char *n;
	uint8_t id;
} sama7g5_progck[] = {
	{ .n = "prog0", .id = 0 },
	{ .n = "prog1", .id = 1 },
	{ .n = "prog2", .id = 2 },
	{ .n = "prog3", .id = 3 },
	{ .n = "prog4", .id = 4 },
	{ .n = "prog5", .id = 5 },
	{ .n = "prog6", .id = 6 },
	{ .n = "prog7", .id = 7 },
};

static struct pmc_data *sama7g5_pmc;

vaddr_t at91_pmc_get_base(void)
{
	assert(sama7g5_pmc);

	return sama7g5_pmc->base;
}

TEE_Result at91_pmc_clk_get(unsigned int type, unsigned int idx,
			    struct clk **clk)
{
	return pmc_clk_get(sama7g5_pmc, type, idx, clk);
}

static TEE_Result pmc_setup_sama7g5(const void *fdt, int nodeoffset,
				    const void *data __unused)
{
	size_t size = 0;
	vaddr_t base = 0;
	unsigned int i = 0;
	unsigned int j = 0;
	int bypass = 0;
	const uint32_t *fdt_prop = NULL;
	struct pmc_clk *pmc_clk = NULL;
	struct clk *parents[11] = {NULL};
	struct clk *main_clk = NULL;
	struct clk *clk = NULL;
	struct clk *main_rc_osc = NULL;
	struct clk *main_osc = NULL;
	struct clk *main_xtal_clk = NULL;

	struct clk *md_slck = NULL;
	struct clk *td_slck = NULL;
	struct clk *mck0_clk = NULL;
	struct clk *pll_div_clk[PLL_ID_MAX] = {NULL};
	struct clk *pll_frac_clk[PLL_ID_MAX] = {NULL};

	TEE_Result res = TEE_ERROR_GENERIC;

	/*
	 * We want PARENT_SIZE to be MAX(ARRAY_SIZE(sama7g5_systemck),11)
	 * but using this define won't allow static initialization of parents
	 * due to dynamic size.
	 */
//	COMPILE_TIME_ASSERT(ARRAY_SIZE(sama7g5_systemck) == PARENT_SIZE);
//	COMPILE_TIME_ASSERT(PARENT_SIZE >= 11);

	if (dt_map_dev(fdt, nodeoffset, &base, &size, DT_MAP_AUTO) < 0)
		panic();

	if (fdt_get_status(fdt, nodeoffset) == DT_STATUS_OK_SEC)
		matrix_configure_periph_secure(ID_PMC);

	res = clk_dt_get_by_name(fdt, nodeoffset, "md_slck", &md_slck);
	if (res)
		panic();

	res = clk_dt_get_by_name(fdt, nodeoffset, "td_slck", &td_slck);
	if (res)
		panic();

	res = clk_dt_get_by_name(fdt, nodeoffset, "main_xtal", &main_xtal_clk);
	if (res)
		panic();

	sama7g5_pmc = pmc_data_allocate(PMC_SAMA7G5_CORE_CLK_COUNT,
					ARRAY_SIZE(sama7g5_systemck),
					ARRAY_SIZE(sama7g5_perick),
					ARRAY_SIZE(sama7g5_gcks), 8);
	if (!sama7g5_pmc)
		panic();
	sama7g5_pmc->base = base;

	main_rc_osc = pmc_register_main_rc_osc(sama7g5_pmc, "main_rc_osc",
					       12000000);
	if (!main_rc_osc)
		panic();

	fdt_prop = fdt_getprop(fdt, nodeoffset, "atmel,osc-bypass", NULL);
	if (fdt_prop)
		bypass = fdt32_to_cpu(*fdt_prop);

	main_osc = pmc_register_main_osc(sama7g5_pmc, "main_osc",
					 main_xtal_clk, bypass);
	if (!main_osc)
		panic();

	parents[0] = main_rc_osc;
	parents[1] = main_osc;
	main_clk = at91_clk_register_sam9x5_main(sama7g5_pmc, "mainck",
						 parents, 2);
	if (!main_clk)
		panic();
	pmc_clk = &sama7g5_pmc->chws[PMC_MAIN];
	pmc_clk->clk = main_clk;
	pmc_clk->id = PMC_MAIN;

	for (i = 0; i < PLL_ID_MAX; i++) {
		struct clk *parent;
		struct pmc_data *pmc = sama7g5_pmc;
		const struct sama7g5_pll *pll;

		for (j = 0; j < 3; j++) {
			pll = &sama7g5_plls[i][j];
			if (!pll->n)
				continue;

			switch (pll->t) {
			case PLL_TYPE_FRAC:
				if (!strcmp(pll->p, "mainck"))
					parent = main_clk;
				else if (!strcmp(pll->p, "main_xtal"))
					parent = main_xtal_clk;
				else
					parent = pmc_clk_get_by_name(pmc->chws,
								     pmc->ncore,
								     pll->p);

				clk = sam9x60_clk_register_frac_pll(sama7g5_pmc,
								    pll->n,
								    parent, i,
								    pll->c,
								    pll->l,
								    pll->f);
				pll_frac_clk[i] = clk;
				break;

			case PLL_TYPE_DIV:
				parent = clk;
				clk = sam9x60_clk_register_div_pll(sama7g5_pmc,
								   pll->n,
								   parent, i,
								   pll->c,
								   pll->l,
								   pll->f,
								   pll->safe_div
								   );
				break;

			default:
				continue;
			}
			if (!clk)
				panic();

			if (pll->eid) {
				sama7g5_pmc->chws[pll->eid].clk = clk;
				sama7g5_pmc->chws[pll->eid].id = pll->eid;
			}
		}
		pll = &sama7g5_plls[i][PLL_TYPE_DIV];
		pll_div_clk[i] = sama7g5_pmc->chws[pll->eid].clk;
	}

	parents[0] = md_slck;
	parents[1] = main_clk;
	parents[2] = pll_div_clk[PLL_ID_CPU];
	parents[3] = pll_div_clk[PLL_ID_SYS];

	clk = at91_clk_register_master_pres(sama7g5_pmc, "fclk", 4,
					    parents,
					    &mck0_layout,
					    &mck0_characteristics, INT_MIN);
	if (!clk)
		panic();

	pmc_clk = &sama7g5_pmc->chws[PMC_MCK_PRES];
	pmc_clk->clk = clk;
	pmc_clk->id = PMC_MCK_PRES;

	mck0_clk = at91_clk_register_master_div(sama7g5_pmc, "mck0",
						clk,
						&mck0_layout,
						&mck0_characteristics);
	if (!mck0_clk)
		panic();

	pmc_clk = &sama7g5_pmc->chws[PMC_MCK];
	pmc_clk->clk = mck0_clk;
	pmc_clk->id = PMC_MCK;

	parents[0] = md_slck;
	parents[1] = td_slck;
	parents[2] = main_clk;
	parents[3] = mck0_clk;
	for (i = 0; i < ARRAY_SIZE(sama7g5_mckx); i++) {
		const struct sama7g5_mck *mck = &sama7g5_mckx[i];
		uint8_t num_parents = 4 + mck->ep_count;
		uint32_t *mux_table = calloc(1, num_parents * sizeof(uint32_t));

		if (!mux_table)
			panic();

		mux_table[0] = 0;
		mux_table[1] = 1;
		mux_table[2] = 2;
		mux_table[3] = 3;
		for (j = 0; j < mck->ep_count; j++) {
			parents[4 + j] = pmc_clk_get_by_name(sama7g5_pmc->chws,
							     sama7g5_pmc->ncore,
							     mck->ep[j]);
			mux_table[4 + j] = mck->ep_mux_table[j];
		}

		clk = at91_clk_sama7g5_register_master(sama7g5_pmc,
						       mck->n,
						       num_parents, parents,
						       mux_table,
						       mck->id,
						       mck->ep_chg_id);
		if (!clk)
			panic();

		sama7g5_pmc->chws[PMC_MCK1 + i].clk = clk;
	}

	parents[0] = md_slck;
	parents[1] = td_slck;
	parents[2] = main_clk;
	parents[3] = pll_div_clk[PLL_ID_SYS];
	parents[4] = pll_div_clk[PLL_ID_DDR];
	parents[5] = pll_div_clk[PLL_ID_IMG];
	parents[6] = pll_div_clk[PLL_ID_BAUD];
	parents[7] = pll_div_clk[PLL_ID_AUDIO];
	parents[8] = pll_div_clk[PLL_ID_ETH];
	for (i = 0; i < ARRAY_SIZE(sama7g5_progck); i++) {
		clk = at91_clk_register_programmable(sama7g5_pmc,
						     sama7g5_progck[i].n,
						     parents,
						     9, i,
						     &sama7g5_prog_layout);
		if (!clk)
			panic();

		pmc_clk = &sama7g5_pmc->pchws[i];
		pmc_clk->clk = clk;
		pmc_clk->id = sama7g5_progck[i].id;
	}

	for (i = 0; i < ARRAY_SIZE(sama7g5_systemck); i++) {
		clk = at91_clk_register_system(sama7g5_pmc,
					       sama7g5_systemck[i].n,
					       sama7g5_pmc->pchws[i].clk,
					       sama7g5_systemck[i].id);
		if (!clk)
			panic();

		pmc_clk = &sama7g5_pmc->shws[i];
		pmc_clk->clk = clk;
		pmc_clk->id = sama7g5_systemck[i].id;
	}

	for (i = 0; i < ARRAY_SIZE(sama7g5_perick); i++) {
		parents[0] = pmc_clk_get_by_name(sama7g5_pmc->chws,
						 sama7g5_pmc->ncore,
						 sama7g5_perick[i].p);
		clk = at91_clk_register_sam9x5_periph(sama7g5_pmc,
						      &sama7g5_pcr_layout,
						      sama7g5_perick[i].n,
						      parents[0],
						      sama7g5_perick[i].id,
						      &sama7g5_perick[i].r);
		if (!clk)
			panic();

		pmc_clk = &sama7g5_pmc->phws[i];
		pmc_clk->clk = clk;
		pmc_clk->id = sama7g5_perick[i].id;
	}

	parents[0] = md_slck;
	parents[1] = td_slck;
	parents[2] = main_clk;
	for (i = 0; i < ARRAY_SIZE(sama7g5_gcks); i++) {
		const struct sama7g5_gck *gck = &sama7g5_gcks[i];
		uint8_t num_parents = 3 + gck->pp_count;
		uint32_t *mux_table = calloc(1, num_parents * sizeof(uint32_t));

		if (!mux_table)
			panic();

		mux_table[0] = 0;
		mux_table[1] = 1;
		mux_table[2] = 2;
		for (j = 0; j < gck->pp_count; j++) {
			parents[3 + j] = pmc_clk_get_by_name(sama7g5_pmc->chws,
							     sama7g5_pmc->ncore,
							     gck->pp[j]
							     );
			mux_table[3 + j] = gck->pp_mux_table[j];
		}

		clk = at91_clk_register_generated(sama7g5_pmc,
						  &sama7g5_pcr_layout,
						  gck->n,
						  parents, mux_table,
						  num_parents,
						  gck->id,
						  &gck->r,
						  gck->pp_chg_id);
		if (!clk)
			panic();

		pmc_clk = &sama7g5_pmc->ghws[i];
		pmc_clk->clk = clk;
		pmc_clk->id = gck->id;
	}

	clk_set_rate(pll_frac_clk[PLL_ID_ETH], 625000000);
	clk_set_rate(pll_div_clk[PLL_ID_ETH], 625000000);

	clk_dt_register_clk_provider(fdt, nodeoffset, clk_dt_pmc_get,
				     sama7g5_pmc);

	pmc_register_pm();

	return TEE_SUCCESS;
}

CLK_DT_DECLARE(sama7g5_clk, "microchip,sama7g5-pmc", pmc_setup_sama7g5);
