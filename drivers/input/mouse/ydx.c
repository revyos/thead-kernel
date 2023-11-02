// SPDX-License-Identifier: GPL-2.0-only
/*
 * YDX Trackpoint
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/property.h>
#include <linux/of.h>

static void ydx_scan(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);
	uint8_t obuf[2] = {0x24, 0x00};
	uint8_t ibuf[7];
	int8_t x,y;
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = obuf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 7,
			.buf = ibuf,
		},
	};

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

	if (ret != ARRAY_SIZE(msgs))
		return;
	if (ibuf[0] != 0x07)
		return;

	x = (uint8_t) ibuf[4];
	y = (uint8_t) ibuf[5];

	input_report_rel(input, REL_X, x);
	input_report_rel(input, REL_Y, y);
	input_sync(input);
}

static int ydx_probe(struct i2c_client *client,
		     const struct i2c_device_id *dev_id)
{
	struct device *dev = &client->dev;
	union i2c_smbus_data dummy;
	struct input_dev *input;
	int error;

	/* Make sure there is something at this address */
	if (i2c_smbus_xfer(client->adapter, client->addr, 0,
			I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &dummy) < 0)
		return -ENODEV;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = "ydx";
	input->id.bustype = BUS_I2C;

	input_set_drvdata(input, client);

	input_set_capability(input, EV_REL, REL_X);
	input_set_capability(input, EV_REL, REL_Y);
	input_set_capability(input, EV_KEY, BTN_LEFT);
	input_set_capability(input, EV_KEY, BTN_RIGHT);

	__set_bit(INPUT_PROP_POINTER, input->propbit);
	__set_bit(INPUT_PROP_POINTING_STICK, input->propbit);
	error = input_setup_polling(input, ydx_scan);
	if (error)
		return error;

	input_set_poll_interval(input, 10);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "could not register input device\n");
		return error;
	}

	return 0;
}

static const struct of_device_id ydx_of_match[] = {
	{ .compatible = "ydx", },
	{ },
};
MODULE_DEVICE_TABLE(of, ydx_of_match);

static const struct i2c_device_id ydx_id_table[] = {
	{ "ydx", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ydx_id_table);

static struct i2c_driver ydx_driver = {
	.driver = {
		.name = "ydx",
		.of_match_table = of_match_ptr(ydx_of_match),
	},

	.probe = ydx_probe,
	.id_table = ydx_id_table,
};

module_i2c_driver(ydx_driver);

MODULE_AUTHOR("Icenowy Zheng <uwu@icenowy.me>");
MODULE_DESCRIPTION("YDX Trackpoint driver");
MODULE_LICENSE("GPL");
