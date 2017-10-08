#include <linux/input/mit200_ts.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
static void mit_ts_early_suspend(struct early_suspend *h);
static void mit_ts_late_resume(struct early_suspend *h);
#endif
/*           */
#ifdef CONFIG_FB
static int mit_ts_fb_notifier_call(struct notifier_block *self,
		unsigned long event, void *data);
#endif
static int mit_ts_config(struct mit_ts_info *info);

static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
		regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int lgd_power_on(struct mit_ts_info *info, int on)
{
	int ret;

	ret = reg_set_optimum_mode_check(info->vcc_i2c, 10000);
	if (ret < 0) {
		dev_err(&info->client->dev,
				"Regulator vcc_i2c set opt failed retval = %d\n",
				ret);
	}

	if (on) {
		ret = regulator_enable(info->vdd);
		if (ret < 0) {
			dev_err(&info->client->dev,
					"Regulator vdd enable failed retval = %d\n",
					ret);
		}

		ret = regulator_enable(info->vcc_i2c);
		if (ret < 0) {
			dev_err(&info->client->dev,
					"Regulator vcc enable failed retval = %d\n",
					ret);
		}
	} else {
		ret = regulator_disable(info->vcc_i2c);
		if (ret < 0) {
			dev_err(&info->client->dev,
					"Regulator vcc disable failed retval = %d\n",
					ret);
		}

		ret = regulator_disable(info->vdd);
		if (ret < 0) {
			dev_err(&info->client->dev,
					"Regulator vdd disable failed retval = %d\n", ret);
		}

	}

	return 0;
}

static int lgd_regulator_configure(struct mit_ts_info *info)
{
	info->vcc_i2c = regulator_get(&info->client->dev, "vcc_i2c");
	if (IS_ERR(info->vcc_i2c)) {
		dev_err(&info->client->dev, "Failed to get vcc-i2c regulator");
		return PTR_ERR(info->vcc_i2c);
	}

	info->vdd = regulator_get(&info->client->dev, "vdd");
	if (IS_ERR(info->vdd)) {
		dev_err(&info->client->dev, "Failed to get vdd regulator");
		return PTR_ERR(info->vdd);
	}

	return 0;
}
/*         */

static void mit_ts_enable(struct mit_ts_info *info)
{
	if (info->enabled)
		return;

	mutex_lock(&info->lock);

	info->enabled = true;
	enable_irq(info->irq);

	mutex_unlock(&info->lock);

}

static void mit_ts_disable(struct mit_ts_info *info)
{
	if (!info->enabled)
		return;

	mutex_lock(&info->lock);

	disable_irq(info->irq);

	info->enabled = false;

	mutex_unlock(&info->lock);
}

void mit_reboot(struct mit_ts_info *info)
{
	struct i2c_adapter *adapter = to_i2c_adapter(info->client->dev.parent);

	i2c_lock_adapter(adapter);
	msleep(50);

	lgd_power_on(info, 0);
	gpio_direction_output(info->pdata->gpio_reset, 0);
	msleep(150);

	lgd_power_on(info, 1);
	gpio_direction_output(info->pdata->gpio_reset, 1);
	msleep(50);

	i2c_unlock_adapter(adapter);
}

void mit_clear_input_data(struct mit_ts_info *info)
{
	int i;

	for (i = 0; i < MAX_FINGER_NUM; i++) {
		input_mt_slot(info->input_dev, i);
		input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
	}
	input_sync(info->input_dev);

	return;
}


void mit_report_input_data(struct mit_ts_info *info, u8 sz, u8 *buf)
{
	int i;
	struct i2c_client *client = info->client;
	int id;
	int x;
	int y;
	int touch_major;
	int pressure;
	u8 *tmp;

	if (buf[0] == MIT_ET_NOTIFY) {
		dev_info(&client->dev, "TSP mode changed (%d)\n", buf[1]);
		goto out;
	} else if (buf[0] == MIT_ET_ERROR) {
		dev_info(&client->dev, "Error detected, restarting TSP\n");
		mit_clear_input_data(info);
		goto out;
	}

	for (i = 0; i < sz; i += FINGER_EVENT_SZ) {
		tmp = buf + i;

		id = (tmp[0] & 0xf) - 1;
		x = tmp[2] | ((tmp[1] & 0xf) << 8);
		y = tmp[3] | (((tmp[1] >> 4) & 0xf) << 8);
		touch_major = tmp[4];
		pressure = tmp[5];
		input_mt_slot(info->input_dev, id);

		if (!(tmp[0] & 0x80)) {
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
			continue;
		}

		input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, true);
		input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, touch_major);
		input_report_abs(info->input_dev, ABS_MT_PRESSURE, pressure);
	}

	input_sync(info->input_dev);

out:
	return;

}

static irqreturn_t mit_ts_interrupt(int irq, void *dev_id)
{
	struct mit_ts_info *info = dev_id;
	struct i2c_client *client = info->client;
	u8 buf[MAX_FINGER_NUM * FINGER_EVENT_SZ] = { 0, };
	int ret;
	u8 sz = 0;
	u8 reg[2] = {MIT_REGH_CMD, MIT_REGL_INPUT_EVENT};
	u8 cmd[2] = {MIT_REGH_CMD, MIT_REGL_EVENT_PKT_SZ};
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = &sz,
		},
	};

	msg[1].len = 1;
	i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (sz == 0)
		return IRQ_HANDLED;
	msg[0].buf = reg;
	msg[1].buf = buf;
	msg[1].len = sz;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));

	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
				"failed to read %d bytes of touch data (%d)\n",
				sz, ret);
	} else {
		mit_report_input_data(info, sz, buf);
	}

	return IRQ_HANDLED;
}


static int mit_ts_input_open(struct input_dev *dev)
{
	struct mit_ts_info *info = input_get_drvdata(dev);
	int ret;

	ret = wait_for_completion_interruptible_timeout(&info->init_done,
			msecs_to_jiffies(5 * MSEC_PER_SEC));
	if (ret > 0) {
		if (info->irq != -1) {
			mit_ts_enable(info);
			ret = 0;
		} else {
			ret = -ENXIO;
		}
	} else {
		dev_err(&dev->dev, "error while waiting for device to init\n");
		ret = -ENXIO;
	}

	return ret;
}

static void mit_ts_input_close(struct input_dev *dev)
{
	struct mit_ts_info *info = input_get_drvdata(dev);

	mit_ts_disable(info);
}

static int mit_ts_config(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	int ret;
	u8 cmd[2] = {MIT_REGH_CMD, MIT_REGL_ROW_NUM};
	u8 num[2] = {0};

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = num,
			.len = 2,
		},
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"failed to read bytes of touch data (%d)\n", ret);
			mit_reboot(info);
	}
	info->row_num = num[0];
	info->col_num = num[1];
	pr_info("row  %d\n", info->row_num);
	pr_info("col  %d\n", info->col_num);

	pr_info("%s (addr=0x%x)\n", __func__, client->addr);

	ret = request_threaded_irq(client->irq, NULL, mit_ts_interrupt,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				"mit_ts", info);

	if (ret) {
		dev_err(&client->dev, "failed to register irq\n");
		goto out;
	}

	disable_irq(client->irq);
	info->irq = client->irq;
	barrier();

	dev_info(&client->dev, "MIT touch controller initialized\n");

	complete_all(&info->init_done);
out:
	return ret;
}

static void mit_fw_update_controller(const struct firmware *fw,
		void *context)
{
	struct mit_ts_info *info = context;
	int retires = 3;
	int ret;

	if (!fw) {
		dev_err(&info->client->dev, "failed to read firmware\n");
		complete_all(&info->init_done);
		complete(&info->fw_completion);
		return;
	}

	do {
		ret = mit_flash_fw(info, fw->data, fw->size);
	} while (ret && --retires);

	if (!retires) {
		dev_err(&info->client->dev, "failed to flash firmware after retires\n");
	}

	pr_info("%s complete\n", __func__);
	complete(&info->fw_completion);

	release_firmware(fw);
}

static ssize_t mit_fw_update(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	struct file *fp;
	mm_segment_t old_fs;
	size_t fw_size, nread;
	int error = 0;
	int result = 0;

	disable_irq(client->irq);
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(EXTRA_FW_PATH, O_RDONLY, S_IRUSR);
	if (IS_ERR(fp)) {
		dev_err(&info->client->dev,
				"%s: failed to open %s.\n", __func__, EXTRA_FW_PATH);
		error = -ENOENT;
		goto open_err;
	}
	fw_size = fp->f_path.dentry->d_inode->i_size;
	if (0 < fw_size) {
		unsigned char *fw_data;
		fw_data = kzalloc(fw_size, GFP_KERNEL);
		nread = vfs_read(fp, (char __user *)fw_data, fw_size, &fp->f_pos);

		dev_info(&info->client->dev,
				"%s: start, file path %s, size %u Bytes\n",
				__func__, EXTRA_FW_PATH, fw_size);

		if (nread != fw_size) {
			dev_err(&info->client->dev,
					"%s: failed to read firmware file, nread %u Bytes\n",
					__func__, nread);

		    error = -EIO;

		} else {
			result = mit_flash_fw(info, fw_data, fw_size);
		}
		kfree(fw_data);
	}
	filp_close(fp, current->files);

open_err:
	enable_irq(client->irq);
	set_fs(old_fs);
	return result;
}

static DEVICE_ATTR(fw_update, 0666, mit_fw_update, NULL);

static ssize_t mit_read_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	char data[255];
	int ret;
	u8 ver[2];

	get_fw_version(client, ver);

	sprintf(data, "f/w version 0x%x, 0x%x\n", ver[0], ver[1]);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", data);
	return ret;
}

static DEVICE_ATTR(version, 0666, mit_read_version, NULL);

static struct attribute *mit_attrs[] = {
	&dev_attr_fw_update.attr,
	&dev_attr_version.attr,
	NULL,
};

static const struct attribute_group mit_attr_group = {
	.attrs = mit_attrs,
};

/*           */
static int lgd_incell_parse_dt(struct device *dev,
				struct mit_ts_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	u32 temp_val;
	int rc;

	pr_info("[TOUCH]*******%s\n", __func__);


	rc = of_property_read_u32(np, "lgd_mit200,max_x", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read max_x\n");
		return rc;
	} else {
		pdata->max_x = temp_val;
	}

	rc = of_property_read_u32(np, "lgd_mit200,max_y", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read max_y\n");
		return rc;
	} else {
		pdata->max_y = temp_val;
	}

	pdata->gpio_irq = of_get_named_gpio_flags(np,
			"lgd_mit200,irq_gpio", 0, &pdata->irq_flags);

	pr_info("[TOUCH]gpio_irq %d\n", pdata->gpio_irq);
	pr_info("[TOUCH]irq_flags %d\n", (int)pdata->irq_flags);

	pdata->gpio_reset = of_get_named_gpio_flags(np,
			"lgd_mit200,reset_gpio", 0, &pdata->reset_flags);

	pr_info("[TOUCH]gpio_reset %d\n", pdata->gpio_reset);
	pr_info("[TOUCH]reset_flags %d\n", (int)pdata->reset_flags);

	/*                    */

	pdata->gpio_17 = of_get_named_gpio_flags(np,
			"lgd_mit200,gpio_17", 0, &pdata->gpio_17);

	/*                  */

	pdata->name = LGD_TOUCH_NAME;

	return 0;
}
/*         */

static int mit_ts_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mit_ts_info *info;
	struct input_dev *input_dev;
	/*           */
	struct mit_ts_platform_data *platform_data = client->dev.platform_data;
	/*         */
	int ret = 0;
	const char *fw_name = FW_NAME;
	struct pinctrl_state *pinset_state;
	/*                                 */
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	input_dev = input_allocate_device();

	if (!info || !input_dev) {
		dev_err(&client->dev, "Failed to allocated memory\n");
		return -ENOMEM;
	}

	info->client = client;
	info->input_dev = input_dev;
	info->pdata = client->dev.platform_data;
	init_completion(&info->init_done);
	info->irq = -1;

	pr_info("[TOUCH]%s start\n", __func__);

/*           */
	if (client->dev.of_node) {
		platform_data = devm_kzalloc(&client->dev,
			sizeof(*platform_data),
			GFP_KERNEL);
		if (!platform_data) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = lgd_incell_parse_dt(&client->dev, platform_data);
		if (ret)
			return ret;
	} else {
		info->pdata = client->dev.platform_data;
	}

	info->pdata = platform_data;
/*         */
/*                    */

	/* Get pinctrl if target uses pinctrl */
	info->ts_pinctrl = devm_pinctrl_get(&(client->dev));
	if (IS_ERR(info->ts_pinctrl)) {
		if (PTR_ERR(info->ts_pinctrl) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		pr_debug("Target does not use pinctrl\n");
		info->ts_pinctrl = NULL;
	}
	pr_info(" after pinctrl_get\n");
	if (info->ts_pinctrl) {
		pinset_state =
			pinctrl_lookup_state(info->ts_pinctrl, "pmx_ts_active");
		if (IS_ERR(pinset_state)) {
			pr_info("cannot get ts pinctrl active state\n");
			ret = pinctrl_select_state(info->ts_pinctrl,
					pinset_state);
			if (ret)
				pr_info("cannot set ts pinctrl active state\n");
		}
	}


/*                  */
	mutex_init(&info->lock);
	init_completion(&info->fw_completion);
	pr_info("init_completion\n");

/*         */
	lgd_regulator_configure(info);
	lgd_power_on(info, 1);

	/*                    */
	/* Added interrupt pin high */
	ret = gpio_request(info->pdata->gpio_irq, "touch_irq");
	if (ret)
		pr_err("[TOUCH]can't request gpio irq !\n");


	ret = gpio_direction_input(info->pdata->gpio_irq);
	if (ret)
		pr_err("[TOUCH]unable to set direction input for gpio\n");


	info->client->irq = gpio_to_irq(info->pdata->gpio_irq);

	/* Added GPIO 17 pin (unused) */
	ret = gpio_request(info->pdata->gpio_17, "dead_pin");
	if (ret)
		pr_err("[TOUCH] can't request gpio_17!\n");


	ret = gpio_direction_input(info->pdata->gpio_17);
	if (ret)
		pr_err("[TOUCH] unable to set direction input for gpio 17\n");


	/*                  */

	/* Added reset pin high. */
	ret = gpio_request(info->pdata->gpio_reset, "touch_reset");
	if (ret < 0)
		pr_err("[TOUCH]can't request qpio!\n");

	gpio_direction_output(info->pdata->gpio_reset, 1);
	msleep(10);

#ifdef CONFIG_FB
	info->fb_notifier.notifier_call = mit_ts_fb_notifier_call;
	ret = fb_register_client(&info->fb_notifier);
	if (ret) {
		dev_err(&client->dev,
				"%s: failed to register fb_notifier: %d\n",
				__func__, ret);
	}

#endif
/*       */
	i2c_set_clientdata(client, info);

	info->fw_name = kstrdup(fw_name, GFP_KERNEL);

	ret = request_firmware_nowait(THIS_MODULE, true, fw_name,
			&info->client->dev, GFP_KERNEL,
			info, mit_fw_update_controller);
		if (ret) {
			dev_err(&client->dev, "failed to schedule firmware update\n");
			return -EIO;
		}

	kfree(info->fw_name);

	ret = wait_for_completion_interruptible_timeout(&info->fw_completion,
			msecs_to_jiffies(30 * MSEC_PER_SEC));
	if (ret <= 0) {
		dev_err(&client->dev, "firmware update timeout\n");
		return -ETIMEDOUT;
	}

	mit_ts_config(info);

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = mit_ts_early_suspend;
	info->early_suspend.resume = mit_ts_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

	input_mt_init_slots(input_dev, MAX_FINGER_NUM, 0);

	snprintf(info->phys, sizeof(info->phys),
		"%s/input0", dev_name(&client->dev));

	input_dev->name = "touch_dev";
	input_dev->phys = info->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mit_ts_input_open;
	input_dev->close = mit_ts_input_close;
	/*           */
	input_dev->dev.init_name = LGE_TOUCH_NAME;
	/*         */
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, MAX_WIDTH, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, MAX_PRESSURE, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, info->pdata->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, info->pdata->max_y, 0, 0);

	input_set_drvdata(input_dev, info);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "failed to register input dev\n");
		return -EIO;
	}

#ifdef __MIT_TEST_MODE__
	if (mit_sysfs_test_mode(info)) {
		dev_err(&client->dev, "failed to create sysfs test mode group\n");
		return -EAGAIN;
	}
	if (mit_ts_log(info)) {
		dev_err(&client->dev, "failed to create mit log mode\n");
		return -EAGAIN;
	}
#endif
	if (sysfs_create_group(&client->dev.kobj, &mit_attr_group)) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		return -EAGAIN;
	}

	if (sysfs_create_link(NULL, &client->dev.kobj, "mit_ts")) {
		dev_err(&client->dev, "failed to create sysfs symlink\n");
		return -EAGAIN;
	}
	dev_notice(&client->dev, "mit dev initialized\n");

	pr_info("[TOUCH]%s done!!!!!\n", __func__);
	return 0;

}

static int __exit mit_ts_remove(struct i2c_client *client)
{
	struct mit_ts_info *info = i2c_get_clientdata(client);

	if (info->irq >= 0)
		free_irq(info->irq, info);

	input_unregister_device(info->input_dev);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&info->early_suspend);
#endif
/*           */
#ifdef CONFIG_FB
	fb_unregister_client(&info->fb_notifier);
#endif
/*         */
#ifdef __MIT_TEST_MODE__
	mit_sysfs_remove(info);
	mit_ts_log_remove(info);
	kfree(info->fw_name);
	kfree(info);
#endif

	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int mit_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = i2c_get_clientdata(client);

	mutex_lock(&info->input_dev->mutex);

	if (info->input_dev->users) {
		mit_ts_disable(info);
		mit_clear_input_data(info);
	}

	mutex_unlock(&info->input_dev->mutex);
	gpio_direction_output(info->pdata->gpio_reset, 0);

	return 0;

}

static int mit_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = i2c_get_clientdata(client);

	gpio_direction_output(info->pdata->gpio_reset, 1);
	mutex_lock(&info->input_dev->mutex);

	if (info->input_dev->users)
		mit_ts_enable(info);

	mutex_unlock(&info->input_dev->mutex);

	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mit_ts_early_suspend(struct early_suspend *h)
{
	struct mit_ts_info *info;
	info = container_of(h, struct mit_ts_info, early_suspend);
	mit_ts_suspend(&info->client->dev);
}

static void mit_ts_late_resume(struct early_suspend *h)
{
	struct mit_ts_info *info;
	info = container_of(h, struct mit_ts_info, early_suspend);
	mit_ts_resume(&info->client->dev);
}
#endif

/*           */
#ifdef CONFIG_FB
static int mit_ts_fb_notifier_call(struct notifier_block *self,
				   unsigned long event,
				   void *data)
{
	struct fb_event *evdata = data;
	int *fb;
	struct mit_ts_info *info = container_of(self,
			struct mit_ts_info, fb_notifier);
	dev_info(&info->client->dev, "%s\n", __func__);
	if (evdata && evdata->data && event ==
			FB_EVENT_BLANK && info && info->client) {
		fb = evdata->data;
		switch (*fb) {
		case FB_BLANK_UNBLANK:
			mit_ts_resume(&info->client->dev);
			break;
		case FB_BLANK_POWERDOWN:
			mit_ts_suspend(&info->client->dev);
			break;
		default:
			break;
		}
	}
	return 0;
}
#endif
/*         */

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops mit_ts_pm_ops = {
	.suspend	= mit_ts_suspend,
	.resume		= mit_ts_resume,
};
#endif

static const struct i2c_device_id mit_ts_id[] = {
	{ "mit_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mit_ts_id);

/*           */
static struct of_device_id lgd_match_table[] = {
	{ .compatible = "lgd_mit200,incell",},
	{ },};
/*         */

static struct i2c_driver mit_ts_driver = {
	.probe      = mit_ts_probe,
	.remove     = __exit_p(mit_ts_remove),
	.driver     = {
		.name   = "mit_ts",
		.of_match_table = lgd_match_table, /*     */
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &mit_ts_pm_ops,
#endif
	},
	.id_table   = mit_ts_id,
};

static int __init mit_ts_init(void)
{
	pr_info("[TOUCH]%s start\n", __func__);
	return i2c_add_driver(&mit_ts_driver);
}

static void __exit mit_ts_exit(void)
{
	return i2c_del_driver(&mit_ts_driver);
}

module_init(mit_ts_init);
module_exit(mit_ts_exit);

MODULE_VERSION("0.1");
MODULE_DESCRIPTION("MIT-200 Touchscreen driver");
MODULE_LICENSE("GPL");
