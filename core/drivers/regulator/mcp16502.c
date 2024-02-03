// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright 2023 Microchip
 */

#include <assert.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <drivers/regulator.h>
#include <io.h>
#include <kernel/delay.h>
#include <kernel/pm.h>
#include <libfdt.h>
#include <platform_config.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <string.h>

#define MCP16502_MODE_AUTO_PFM 0
#define MCP16502_MODE_FPWM BIT32(6)

#define VDD_LOW_SEL 0x0D
#define VDD_HIGH_SEL 0x3F
#define VSET_COUNT (VDD_HIGH_SEL - VDD_LOW_SEL)

#define MCP16502_VSET_MASK 0x3F
#define MCP16502_EN BIT32(7)
#define MCP16502_MODE BIT32(6)

#define MCP16502_REG_BASE(i, r) ((((i) + 1) << 4) + MCP16502_REG_##r)
#define MCP16502_STAT_BASE(i) ((i) + 5)

#define MCP16502_OPMODE_ACTIVE 0x2
#define MCP16502_OPMODE_LPM 0x4
#define MCP16502_OPMODE_HIB 0x8

/**
 * enum mcp16502_reg_type - MCP16502 regulators's registers
 * @MCP16502_REG_A: active state register
 * @MCP16502_REG_LPM: low power mode state register
 * @MCP16502_REG_HIB: hibernate state register
 * @MCP16502_REG_SEQ: startup sequence register
 * @MCP16502_REG_CFG: configuration register
 */
enum mcp16502_reg_type
{
	MCP16502_REG_A,
	MCP16502_REG_LPM,
	MCP16502_REG_HIB,
	MCP16502_REG_HPM,
	MCP16502_REG_SEQ,
	MCP16502_REG_CFG,
};

struct mcp16502
{
	struct i2c_dev *i2c_dev;
	struct gpio *lpm_gpio;
};

static void mcp16502_gpio_set_lpm_mode(struct mcp16502 *mcp, bool lpm)
{
	gpio_set_value(mcp->lpm_gpio, !lpm);
}

/*
 * mcp16502_gpio_set_mode() - set the GPIO corresponding value
 *
 * Used to prepare transitioning into hibernate or resuming from it.
 */
static void mcp16502_gpio_set_mode(struct mcp16502 *mcp, int mode)
{
	switch (mode)
	{
	case MCP16502_OPMODE_ACTIVE:
		mcp16502_gpio_set_lpm_mode(mcp, false);
		break;
	case MCP16502_OPMODE_LPM:
	case MCP16502_OPMODE_HIB:
		mcp16502_gpio_set_lpm_mode(mcp, true);
		break;
	default:
		EMSG("Invalid mode for mcp16502_gpio_set_mode");
	}
}

#ifdef CFG_PM_ARM32
static TEE_Result mcp16502_pm(enum pm_op op, uint32_t pm_hint __unused,
			       const struct pm_callback_handle *hdl __unused)
{
	struct mcp16502 *mcp = hdl->handle;

	switch (op) {
	case PM_OP_RESUME:
		mcp16502_gpio_set_mode(mcp, MCP16502_OPMODE_ACTIVE);
		break;
	case PM_OP_SUSPEND:
		mcp16502_gpio_set_mode(mcp, MCP16502_OPMODE_LPM);
		break;
	default:
		break;
	}

	return TEE_SUCCESS;
}

static void mcp16502_pm_init(struct mcp16502 *mcp)
{
	register_pm_driver_cb(mcp16502_pm, mcp, "mcp16502");
}
#else
static void mcp16502_pm_init(struct mcp16502 *mcp)
{
}
#endif

static TEE_Result mcp16502_set_state(struct regulator *regulator, bool enable) {
	return TEE_SUCCESS;
}


static TEE_Result mcp16502_get_state(struct regulator *regulator, bool *enabled) {
	return TEE_SUCCESS;
}

static TEE_Result mcp16502_get_voltage(struct regulator *regulator, int *level_uv) {
	return TEE_SUCCESS;
}

static TEE_Result mcp16502_set_voltage(struct regulator *regulator, int level_uv) {
	return TEE_SUCCESS;
}

static TEE_Result mcp16502_list_voltages(struct regulator *regulator,
					 struct regulator_voltages_desc **out_desc,
					 const int **out_levels)
{
	return TEE_SUCCESS;
}


static TEE_Result mcp16502_regu_init(struct regulator *regulator,
				     const void *fdt __unused, int node __unused) {
	return TEE_SUCCESS;
}

#if 0
static const char * const mcp16502_regu_name_ids[] = {
	"VDD_IO", "VDD_DDR", "VDD_CORE", "VDD_OTHER", "LDO1", "LDO2"
};
#endif

static const struct regulator_ops mcp16502_regu_buck_ops = {
	.set_state = mcp16502_set_state,
	.get_state = mcp16502_get_state,
	.set_voltage = mcp16502_set_voltage,
	.get_voltage = mcp16502_get_voltage,
	.supported_voltages = mcp16502_list_voltages,
	.supplied_init = mcp16502_regu_init,
};
DECLARE_KEEP_PAGER(mcp16502_regu_buck_ops);

static const struct regulator_ops mcp16502_regu_ldo_ops = {
	.set_state = mcp16502_set_state,
	.get_state = mcp16502_get_state,
	.set_voltage = mcp16502_set_voltage,
	.get_voltage = mcp16502_get_voltage,
	.supplied_init = mcp16502_regu_init,
};
DECLARE_KEEP_PAGER(mcp16502_regu_ldo_ops);

static TEE_Result mcp16502_register_regulator(const void *fdt, int node)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	struct regu_dt_desc *desc;

	desc = calloc(1, sizeof(struct regu_dt_desc));
	if (!desc)
		return TEE_ERROR_OUT_OF_MEMORY;

	res = regulator_dt_register(fdt, node, node, desc);
	if (res)
		EMSG("Failed to register %s, error: %#"PRIx32, desc->name, res);

	if (!strncmp(desc->name, "LDO", 3))
		desc->ops = &mcp16502_regu_buck_ops;
	else
		desc->ops = &mcp16502_regu_ldo_ops;

	return res;
}

static TEE_Result mcp16502_register_regulators(const void *fdt, int node)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	int regs_node = 0;
	int reg = 0;

	regs_node = fdt_subnode_offset(fdt, node, "regulators");
	if (regs_node < 0)
		return TEE_ERROR_GENERIC;

	fdt_for_each_subnode(reg, fdt, regs_node) {
		res = mcp16502_register_regulator(fdt, reg);
		if (res)
			return res;
	}

	return TEE_SUCCESS;
}

static TEE_Result mcp16502_probe(struct i2c_dev *i2c_dev, const void *fdt,
				 int node, const void *compat_data __unused)
{
	struct mcp16502 *mcp = NULL;

	mcp = calloc(1, sizeof(struct mcp16502));
	if (!mcp)
		return TEE_ERROR_OUT_OF_MEMORY;

	mcp->i2c_dev = i2c_dev;

	/* The LPM gpio is described as optional in the bindings */
	gpio_dt_get_by_index(fdt, node, 0, "lpm", &mcp->lpm_gpio);
	if (mcp->lpm_gpio)
	{
		gpio_set_direction(mcp->lpm_gpio, GPIO_DIR_OUT);
		gpio_set_value(mcp->lpm_gpio, GPIO_LEVEL_LOW);
	}

	mcp16502_gpio_set_lpm_mode(mcp, false);

	mcp16502_pm_init(mcp);

	return mcp16502_register_regulators(fdt, node);
}

static const struct dt_device_match mcp16502_match_table[] = {
	{ .compatible = "microchip,mcp16502" },
	{ }
};

DEFINE_I2C_DEV_DRIVER(mcp16502, mcp16502_match_table, mcp16502_probe);
