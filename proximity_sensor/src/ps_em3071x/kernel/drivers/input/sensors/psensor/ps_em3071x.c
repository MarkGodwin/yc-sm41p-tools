// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/drivers/input/sensors/psensor/ps_em3071x.c
 *
 * Copyright (C) 2020 Rockchip Co.,Ltd.
 * Author: Wang Jie <dave.wang@rock-chips.com>
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

#define EM3071X_CONFIG_REG		0x01
#define EM3071X_INT_REG			0x02
#define EM3071X_PS_L_REG		0x03
#define EM3071X_PS_H_REG		0x04
#define EM3071X_PS_REG			0x08
#define EM3071X_RESET_REG		0x0E
#define EM3071X_PS_OFFSET_REG	0x0F
#define EM3071X_PS_OFFSET_SHIFT 1

#define EM3071X_PS_INT_MASK		0xF0
#define EM3071X_PS_INT_FLAG		0x80

#define EM3071X_PS_MASK         0xF8
#define EM3071X_ALS_MASK        0x07

#define EM3071X_INT_CLEAR		0x00

#define EM3071X_PS_ENABLE       0x80
#define EM3071X_PS_LOWPOWER     0x40

#define EM3071X_PS_LED_200MA    0x07
#define EM3071X_PS_LED_100MA    0x06
#define EM3071X_PS_LED_50MA     0x05
#define EM3071X_PS_LED_25MA     0x04
#define EM3071X_PS_LED_120MA    0x03
#define EM3071X_PS_LED_60MA     0x02
#define EM3071X_PS_LED_30MA     0x01
#define EM3071X_PS_LED_15MA     0x00

#define EM3071X_PS_LED_SHIFT    3

#define EM3017X_ALS_EN          0x06
#define EM3071X_ALS_RANGE       0x01

static int ps_threshold_low;
static int ps_threshold_high;
static int ps_power;

static int em3071x_get_object(struct i2c_client *client)
{
	int val;
	val = sensor_read_reg(client, EM3071X_PS_REG);

	if (val >= ps_threshold_high) {
		val = 0;
	} else if (val <= ps_threshold_low) {
		val = 255;
	}
	else {
		val = 255 - val;
	}

	dev_dbg(&client->dev,
		 "%s: val = 0x%x\n", __func__, val);

	return val;
}

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	dev_info(&client->dev, "%s: activating proximity sensor\n", __func__);

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (enable) {
		sensor->ops->ctrl_data = EM3071X_PS_ENABLE | (ps_power << EM3071X_PS_LED_SHIFT) | (EM3071X_ALS_MASK & sensor->ops->ctrl_data);
	} else {
		sensor->ops->ctrl_data &= ~EM3071X_PS_ENABLE;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s: fail to active sensor\n", __func__);

	return result;

}

static int em3071x_set_threshold(struct i2c_client *client,
				 int threshold_high, int threshold_low)
{
	int result = 0;

	/* These thresholds configure the interrupts, but I don't know what pin the interrupt is on */
	result = sensor_write_reg(client, EM3071X_PS_L_REG, threshold_low);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to write EM3071X_PS_L_REG(%d)\n",
			__func__, result);
		return result;
	}

	result = sensor_write_reg(client, EM3071X_PS_H_REG, threshold_high);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to write EM3071X_PS_H_REG(%d)\n",
			__func__, result);
		return result;
	}

	dev_info(&client->dev,
		"%s: set threshold_high = %d, set threshold_low = %d\n",
		__func__, threshold_high, threshold_low);
	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	int ps_val = 0;
	int ret = 0;
	int result = 0;

	dev_info(&client->dev, "%s: line = %d, The proximity driver is starting.\n",
			__func__, __LINE__);

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s: line = %d, result = %d.\n",
			__func__, __LINE__, result);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	/* initialize the EM3071X chip */

	/* Don't reset the chip, as the ALS sensor will be upset
	result = sensor_write_reg(client, EM3071X_CONFIG_REG, 0X00);
	if (result) {
		dev_err(&client->dev,
			"%s: write EM3071X_CONFIG_REG reg fail(%d)\n",
			__func__, result);
		return result;
	}*/

	result = sensor_write_reg(client, EM3071X_INT_REG, EM3071X_INT_CLEAR);
	if (result) {
		dev_err(&client->dev,
			"%s: write EM3071X_INT_REG reg fail(%d)\n",
			__func__, result);
		return result;
	}

	ret = of_property_read_u32(np, "ps_power", &ps_val);
	if (ret)
	{
		ps_val = EM3071X_PS_LED_50MA;
		dev_warn(&client->dev,
			"%s: Unable to get ps_offset\n", __func__);
	}

	ps_power = ps_val;

	ret = of_property_read_u32(np, "ps_offset", &ps_val);
	if (ret)
	{
		ps_val = 0x05;
		dev_warn(&client->dev,
			"%s: Unable to get ps_offset\n", __func__);
	}
	ps_val <<= EM3071X_PS_OFFSET_SHIFT;

	result = sensor_write_reg(client, EM3071X_PS_OFFSET_REG, ps_val);
	if (result) {
		dev_err(&client->dev,
			"%s: write EM3071X_PS_OFFSET reg fail(%d)\n",
			__func__, result);
		return result;
	}

	ret = of_property_read_u32(np, "ps_threshold_low", &ps_val);
	if (ret)
		dev_warn(&client->dev,
			"%s: Unable to get ps_threshold_low\n", __func__);

	ps_threshold_low = ps_val;

	ret = of_property_read_u32(np, "ps_threshold_high", &ps_val);
	if (ret)
		dev_warn(&client->dev,
			"%s: Unable to get ps_threshold_high\n", __func__);

	ps_threshold_high = ps_val;

	ret = em3071x_set_threshold(client,
				    ps_threshold_high, ps_threshold_low);
	if (ret)
		dev_err(&client->dev,
			"%s: em3071x set threshold failed\n", __func__);

	return result;
}

static int em3071x_get_status(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	int val;

	val = sensor_read_reg(client, sensor->ops->int_status_reg);

	return val;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char value = 0;
	u8 status;

	status = em3071x_get_status(client);
	dev_dbg(&client->dev, "em3071x_get_status: status = 0x%x\n", status);

	if (sensor->pdata->irq_enable) {
		if (status & EM3071X_PS_INT_FLAG) {
			result = sensor_write_reg(client, EM3071X_INT_REG, status & ~EM3071X_PS_INT_MASK);
			if (result) {
				dev_err(&client->dev,
					"%s: write EM3071X_INT_REG reg fail(%d)\n", __func__, result);
				return result;
			}

			value = em3071x_get_object(client);
			input_report_abs(sensor->input_dev, ABS_DISTANCE, value);
			input_sync(sensor->input_dev);
		}
	} else {
		value = em3071x_get_object(client);
		input_report_abs(sensor->input_dev, ABS_DISTANCE, value);
		input_sync(sensor->input_dev);
	}

	return result;
}

struct sensor_operate psensor_em3071x_ops = {
	.name		= "ps_em3071x",
	.type		= SENSOR_TYPE_PROXIMITY,
	.id_i2c		= PROXIMITY_ID_EM3071X,
	.read_reg	= SENSOR_UNKNOW_DATA,
	.read_len	= 1,
	.id_reg		= 0,
	.id_data	= 0x31,
	.precision	= 8,
	.ctrl_reg	= 0x01,
	.int_status_reg = 0x02,
	.range		= {0, 255},
	.brightness	= {10, 255},
	.trig		= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

static int proximity_em3071x_probe(
	struct i2c_client *client,
	const struct i2c_device_id *devid)
{
	dev_info(&client->dev, "proximity_em3071x_probe called\n");

	return sensor_register_device(client, NULL, devid, &psensor_em3071x_ops);
}

static int proximity_em3071x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &psensor_em3071x_ops);
}

static const struct i2c_device_id proximity_em3071x_id[] = {
	{"ps_em3071x", PROXIMITY_ID_EM3071X},
	{}
};

static struct i2c_driver proximity_em3071x_driver = {
	.probe = proximity_em3071x_probe,
	.remove = proximity_em3071x_remove,
	.shutdown = sensor_shutdown,
	.id_table = proximity_em3071x_id,
	.driver = {
		.name = "proximity_em3071x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(proximity_em3071x_driver);

MODULE_AUTHOR("Wang Jie <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("em3071x proximity driver");
MODULE_LICENSE("GPL");
