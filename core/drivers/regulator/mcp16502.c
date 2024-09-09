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

enum mcp16502_reg_id
{
	BUCK1 = 0,
	BUCK2,
	BUCK3,
	BUCK4,
	BUCK_COUNT = BUCK4,
	LDO1,
	LDO2,
	MCP16502_REG_COUNT
};

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

struct mcp16502_pmic
{
	struct i2c_dev *i2c_dev;
	struct gpio *lpm_gpio;
};

struct mcp16502_priv
{
	const char *name;
	enum mcp16502_reg_id id;
	const struct mcp16502_vset_range *vset_range;
	struct mcp16502_pmic *pmic;
};

struct mcp16502_vset_range
{
	int uv_min;
	int uv_max;
	unsigned int uv_step;
};

#define MCP16502_VSET_RANGE(_name, _min, _step)		\
const struct mcp16502_vset_range _name ## _range = {	\
	.uv_min = _min,					\
	.uv_max = _min + VSET_COUNT * _step,		\
	.uv_step = _step,				\
}

MCP16502_VSET_RANGE(buck1_ldo12, 1200000, 50000);
MCP16502_VSET_RANGE(buck234, 600000, 25000);

#define MCP16502_REGULATOR(_name, _id, _ranges) \
	{					\
		.name = _name,			\
		.id = _id,			\
		.vset_range = &_ranges,		\
		.pmic = NULL,			\
	}

static struct mcp16502_priv mcp16502_regu[] = {
	/* MCP16502_REGULATOR(_name, _id, ranges, regulator_ops) */
	MCP16502_REGULATOR("VDD_IO", BUCK1, buck1_ldo12_range),
	MCP16502_REGULATOR("VDD_DDR", BUCK2, buck234_range),
	MCP16502_REGULATOR("VDD_CORE", BUCK3, buck234_range),
	MCP16502_REGULATOR("VDD_OTHER", BUCK4, buck234_range),
	MCP16502_REGULATOR("LDO1", LDO1, buck1_ldo12_range),
	MCP16502_REGULATOR("LDO2", LDO2, buck1_ldo12_range)
};

static void mcp16502_gpio_set_lpm_mode(struct mcp16502_pmic *pmic, bool lpm)
{
	gpio_set_value(pmic->lpm_gpio, !lpm);
}

/*
 * mcp16502_gpio_set_mode() - set the GPIO corresponding value
 *
 * Used to prepare transitioning into hibernate or resuming from it.
 */
static void mcp16502_gpio_set_mode(struct mcp16502_pmic *pmic, int mode)
{
	switch (mode)
	{
	case MCP16502_OPMODE_ACTIVE:
		mcp16502_gpio_set_lpm_mode(pmic, false);
		break;
	case MCP16502_OPMODE_LPM:
	case MCP16502_OPMODE_HIB:
		mcp16502_gpio_set_lpm_mode(pmic, true);
		break;
	default:
		EMSG("Invalid mode for mcp16502_gpio_set_mode");
	}
}

#ifdef CFG_PM_ARM32
static TEE_Result mcp16502_pm(enum pm_op op, uint32_t pm_hint __unused,
			       const struct pm_callback_handle *hdl __unused)
{
	struct mcp16502_pmic *pmic = hdl->handle;

	switch (op) {
	case PM_OP_RESUME:
		mcp16502_gpio_set_mode(pmic, MCP16502_OPMODE_ACTIVE);
		break;
	case PM_OP_SUSPEND:
		mcp16502_gpio_set_mode(pmic, MCP16502_OPMODE_LPM);
		break;
	default:
		break;
	}

	return TEE_SUCCESS;
}

static void mcp16502_pm_init(struct mcp16502_pmic *pmic)
{
	register_pm_driver_cb(mcp16502_pm, pmic, "mcp16502");
}
#else
static void mcp16502_pm_init(struct mcp16502_pmic *pmic)
{
}
#endif

static TEE_Result mcp16502_rm(struct mcp16502_pmic *pmic, unsigned int reg_off,
			      uint8_t mask, uint8_t *value)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t byte;

	res = i2c_smbus_read_byte_data(pmic->i2c_dev, reg_off, &byte);
	if (res)
		return res;

	byte &= ~mask;
	*value = byte;

	return TEE_SUCCESS;
}

static TEE_Result mcp16502_rmw(struct mcp16502_pmic *pmic, unsigned int reg_off,
			       uint8_t mask, uint8_t value)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t byte;

	res = i2c_smbus_read_byte_data(pmic->i2c_dev, reg_off, &byte);
	if (res)
		return res;

	byte &= ~mask;
	byte |= value;

	return i2c_smbus_write_byte_data(pmic->i2c_dev, reg_off, byte);
}

static TEE_Result mcp16502_set_state(struct regulator *regulator, bool enable)
{
	struct mcp16502_priv *priv = regulator->priv;
	uint32_t reg_off = MCP16502_REG_BASE(priv->id, A);

	if (enable)
		return mcp16502_rmw(priv->pmic, reg_off, MCP16502_EN, MCP16502_EN);
	else
		return mcp16502_rmw(priv->pmic, reg_off, MCP16502_EN, 0);
}

static TEE_Result mcp16502_get_state(struct regulator *regulator, bool *enabled)
{
	struct mcp16502_priv *priv = regulator->priv;
	uint32_t reg_off = MCP16502_REG_BASE(priv->id, A);

	return mcp16502_rm(priv->pmic, reg_off, MCP16502_EN, enabled);
}

static TEE_Result mcp16502_get_voltage(struct regulator *regulator, int *level_uv)
{
	struct mcp16502_priv *priv = regulator->priv;
	const struct mcp16502_vset_range *vset_r = priv->vset_range;
	uint8_t vset;
	uint32_t reg_off = MCP16502_REG_BASE(priv->id, A);
	TEE_Result res = TEE_ERROR_GENERIC;

	res = mcp16502_rm(priv->pmic, reg_off, MCP16502_VSET_MASK, &vset);
	if (res)
		return res;

	*level_uv = (vset - VDD_LOW_SEL) * vset_r->uv_step + vset_r->uv_min;

	return res;
}

static TEE_Result mcp16502_set_voltage(struct regulator *regulator, int level_uv)
{
	struct mcp16502_priv *priv = regulator->priv;
	const struct mcp16502_vset_range *vset_r = priv->vset_range;
	uint8_t vset = 0;
	uint32_t reg_off = MCP16502_REG_BASE(priv->id, A);
	TEE_Result res = TEE_ERROR_GENERIC;

	if (level_uv < vset_r->uv_min || level_uv > vset_r->uv_max)
		return TEE_ERROR_BAD_PARAMETERS;

	vset = VDD_LOW_SEL + (level_uv - vset_r->uv_min) / vset_r->uv_step;

	res = mcp16502_rmw(priv->pmic, reg_off, MCP16502_VSET_MASK, vset);

	return res;
}

static TEE_Result mcp16502_list_voltages(struct regulator *regulator,
					 struct regulator_voltages_desc **out_desc,
					 const int **out_levels)
{
	struct mcp16502_priv *priv = regulator->priv;
	const struct mcp16502_vset_range *vset_r = priv->vset_range;
	unsigned int i = 0;

	// TODO
	if (offset > VSET_COUNT)
		return TEE_ERROR_BAD_PARAMETERS;

	for (i = 0; i < *count; i++)
		voltage[i] = vset_r->uv_min + (offset + i) * vset_r->uv_step;

	return TEE_SUCCESS;
}


static TEE_Result mcp16502_supplied_init(struct regulator *regulator,
					 const void *fdt __unused,
					 int node __unused) {
	// TODO
	return TEE_SUCCESS;
}

static const struct regulator_ops mcp16502_regu_buck_ops = {
	.set_state = mcp16502_set_state,
	.get_state = mcp16502_get_state,
	.set_voltage = mcp16502_set_voltage,
	.get_voltage = mcp16502_get_voltage,
	.supported_voltages = mcp16502_list_voltages,
	.supplied_init = mcp16502_supplied_init,
};
DECLARE_KEEP_PAGER(mcp16502_regu_buck_ops);

static const struct regulator_ops mcp16502_regu_ldo_ops = {
	.set_state = mcp16502_set_state,
	.get_state = mcp16502_get_state,
	.set_voltage = mcp16502_set_voltage,
	.get_voltage = mcp16502_get_voltage,
	.supplied_init = mcp16502_supplied_init,
};
DECLARE_KEEP_PAGER(mcp16502_regu_ldo_ops);

static TEE_Result mcp16502_register_regulator(const void *fdt, int node,
					      struct mcp16502_pmic *pmic)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	struct regu_dt_desc *desc;
	int i = 0;

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

	for (i = 0; i < MCP16502_REG_COUNT; i++) {
		if (strcmp(desc->name, mcp16502_regu[i].name) == 0) {
			struct mcp16502_priv priv = mcp16502_regu[i];
			priv.pmic = pmic;
			desc->priv = &priv;
			break;
		}
	}

	return res;
}

static TEE_Result mcp16502_register_regulators(const void *fdt, int node,
					       struct mcp16502_pmic *pmic)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	int regs_node = 0;
	int reg = 0;

	regs_node = fdt_subnode_offset(fdt, node, "regulators");
	if (regs_node < 0)
		return TEE_ERROR_GENERIC;

	fdt_for_each_subnode(reg, fdt, regs_node) {
		res = mcp16502_register_regulator(fdt, reg, pmic);
		if (res)
			return res;
	}

	return TEE_SUCCESS;
}

static TEE_Result mcp16502_probe(struct i2c_dev *i2c_dev, const void *fdt,
				 int node, const void *compat_data __unused)
{
	struct mcp16502_pmic *pmic = NULL;

	pmic = calloc(1, sizeof(struct mcp16502_pmic));
	if (!pmic)
		return TEE_ERROR_OUT_OF_MEMORY;

	pmic->i2c_dev = i2c_dev;

	/* The LPM gpio is described as optional in the bindings */
	gpio_dt_get_by_index(fdt, node, 0, "lpm", &pmic->lpm_gpio);
	if (pmic->lpm_gpio)
	{
		gpio_set_direction(pmic->lpm_gpio, GPIO_DIR_OUT);
		gpio_set_value(pmic->lpm_gpio, GPIO_LEVEL_LOW);
	}

	mcp16502_gpio_set_lpm_mode(pmic, false);

	mcp16502_pm_init(pmic);

	return mcp16502_register_regulators(fdt, node, pmic);
}

static const struct dt_device_match mcp16502_match_table[] = {
	{ .compatible = "microchip,mcp16502" },
	{ }
};

DEFINE_I2C_DEV_DRIVER(mcp16502, mcp16502_match_table, mcp16502_probe);
