#include <linux/input/mit200_ts.h>


#ifdef __MIT_TEST_MODE__

static int mit_ts_rowcol(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	int ret;
	u8 cmd[2] = {MIT_REGH_CMD, MIT_REGL_ROW_NUM};
	u8 num[2];
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
			"failed to read bytes of touch data (%d)\n",
			 ret);
	}
	info->row_num = num[0];
	info->col_num = num[1];
	pr_info("row  %d\n", info->row_num);
	pr_info("col  %d\n", info->col_num);
	return 0;
}

static int get_intensity(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	int col, row;
	char tmp_data[255];
	u8 write_buf[8];
	u8 read_buf[60];


	u8 nLength = 0;

	s16 nIntensity;

	memset(info->get_data, 0, sizeof(info->get_data));
	sprintf(tmp_data, "Start-Intensity\n\n");
	strcat(info->get_data, tmp_data);
	memset(tmp_data, 0, 255);
	disable_irq(info->irq);
	for (col = 0; col < info->col_num; col++) {
		pr_info("[%2d]  ", col);

		sprintf(tmp_data, "[%2d]  ", col);
		strcat(info->get_data, tmp_data);

		write_buf[0] = MIT_REGH_CMD;
		write_buf[1] = MIT_REGL_UCMD;
		write_buf[2] = 0x70;
		write_buf[3] = 0xFF;
		write_buf[4] = col;

		if (i2c_master_send(client, write_buf, 5) != 5) {
			dev_err(&client->dev, "intensity i2c send failed\n");
			enable_irq(info->irq);
			return -1;
		}

		write_buf[0] = MIT_REGH_CMD;
		write_buf[1] = MIT_REGL_UCMD_RESULT_LENGTH;

		if (i2c_master_send(client, write_buf, 2) != 2) {
			dev_err(&client->dev, "send : i2c failed\n");
			enable_irq(info->irq);
			return -1;
		}

		if (i2c_master_recv(client, read_buf, 1) != 1) {
			dev_err(&client->dev, "recv : i2c failed\n");
			enable_irq(info->irq);
			return -1;
		}

		nLength = read_buf[0];
		write_buf[0] = MIT_REGH_CMD;
		write_buf[1] = MIT_REGL_UCMD_RESULT;

		if (i2c_master_send(client, write_buf, 2) != 2) {
			dev_err(&client->dev, "send : i2c failed\n");
			enable_irq(info->irq);
			return -1;
		}

		if (i2c_master_recv(client, read_buf, nLength) != nLength) {
			dev_err(&client->dev, "recv : i2c failed\n");
			enable_irq(info->irq);
			return -1;
		}

		nLength >>= 1;
		for (row = 0 ; row < nLength ; row++) {
			nIntensity = (s16)(read_buf[2*row] | (read_buf[2*row+1] << 8));
			sprintf(tmp_data, "%d, \t", nIntensity);
			strcat(info->get_data, tmp_data);
#if KERNEL_LOG_MSG
			pr_info("%d, \t", nIntensity);
		}
		pr_info("\n");
#else
		}
#endif
		sprintf(tmp_data, "\n");
		strcat(info->get_data, tmp_data);
	}
	enable_irq(info->irq);

	return 0;
}

static int mit_fs_open(struct inode *node, struct file *fp)
{
	struct mit_ts_info *info;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 buf[4] = {
		MIT_REGH_CMD,
		MIT_REGL_UCMD,
		0x20,
		true,
	};

	info = container_of(node->i_cdev, struct mit_ts_info, cdev);
	client = info->client;


	disable_irq(info->irq);
	fp->private_data = info;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	i2c_transfer(client->adapter, &msg, 1);

	info->log.data = kzalloc(MAX_LOG_LENGTH * 20 + 5, GFP_KERNEL);

	mit_clear_input_data(info);

	return 0;
}

static int mit_fs_release(struct inode *node, struct file *fp)
{
	struct mit_ts_info *info = fp->private_data;
	struct i2c_client *client = info->client;
	struct i2c_msg msg;
	u8 buf[4] = {
		MIT_REGH_CMD,
		MIT_REGL_UCMD,
		0x20,
		false,
	};
	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);


	mit_clear_input_data(info);


	i2c_transfer(client->adapter, &msg, 1);
	kfree(info->log.data);
	enable_irq(info->irq);

	return 0;
}

static void mit_event_handler(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	u8 sz;
	int ret;
	int row_num;
	u8 reg[2] = {MIT_REGH_CMD, MIT_REGL_EVENT_PKT_SZ};
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = reg,
			.len = 2,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = info->log.data,
		},

	};
	struct mit_log_pkt {
		u8	marker;
		u8	log_info;
		u8	code;
		u8	element_sz;
		u8	row_sz;
		u8	rigth_shift;
	} __packed * pkt = (struct mit_log_pkt *)info->log.data;

	memset(pkt, 0, sizeof(*pkt));
/*           */
	if (gpio_get_value(info->pdata->gpio_irq))
		return;
/*         */
	if (i2c_master_send(client, reg, 2) != 2)
		dev_err(&client->dev, "send : i2c failed\n");
	if (i2c_master_recv(client, &sz, 1) != 1)
		dev_err(&client->dev, "recv : i2c failed\n");
	msg[1].len = sz;

	reg[1] = MIT_REGL_INPUT_EVENT;
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
				"failed to read %d bytes of data\n",
				sz);
		return;
	}

	if ((pkt->marker & 0xf) == MIT_ET_LOG) {
		if ((pkt->log_info & 0x7) == 0x1) {
			pkt->element_sz = 0;
			pkt->row_sz = 0;

			return;
		}

		switch (pkt->log_info >> 4) {
		case LOG_TYPE_U08:
		case LOG_TYPE_S08:
			msg[1].len = pkt->element_sz;
			break;
		case LOG_TYPE_U16:
		case LOG_TYPE_S16:
			msg[1].len = pkt->element_sz * 2;
			break;
		case LOG_TYPE_U32:
		case LOG_TYPE_S32:
			msg[1].len = pkt->element_sz * 4;
			break;
		default:
			dev_err(&client->dev, "invalied log type\n");
			return;
		}

		msg[1].buf = info->log.data + sizeof(struct mit_log_pkt);
		reg[1] = MIT_REGL_UCMD_RESULT;
		row_num = pkt->row_sz ? pkt->row_sz : 1;

		while (row_num--) {
			/*           */
			while (gpio_get_value(info->pdata->gpio_irq))
				;
			/*         */
			ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
			msg[1].buf += msg[1].len;
		};
	} else {
		mit_report_input_data(info, sz, info->log.data);
		memset(pkt, 0, sizeof(*pkt));
	}

	return;
}


static ssize_t mit_fs_read(struct file *fp,
		char *rbuf, size_t cnt, loff_t *fpos)
{
	struct mit_ts_info *info = fp->private_data;
	struct i2c_client *client = info->client;
	int ret = 0;

	pr_info("[LGE_TOUCH_TEST] mit_fs_read call! rbuf = %s\n", rbuf);
	switch (info->log.cmd) {
	case GET_COL_NUM:
		ret = copy_to_user(rbuf, &info->col_num, 1);
		break;
	case GET_ROW_NUM:
		ret = copy_to_user(rbuf, &info->row_num, 1);
		break;
	case GET_EVENT_DATA:
		mit_event_handler(info);
		/* copy data without log marker */
		ret = copy_to_user(rbuf, info->log.data + 1, cnt);
		break;
	default:
		dev_err(&client->dev, "unknown command\n");
		ret = -EFAULT;
		break;
	}

	return ret;
}

static ssize_t mit_fs_write(struct file *fp,
		const char *wbuf, size_t cnt, loff_t *fpos)
{
	struct mit_ts_info *info = fp->private_data;
	struct i2c_client *client = info->client;
	u8 *buf;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = cnt,
	};
	int ret = 0;

	mutex_lock(&info->lock);

	if (!info->enabled)
		goto tsp_disabled;

	msg.buf = buf = kzalloc(cnt + 1, GFP_KERNEL);

	if ((buf == NULL) || copy_from_user(buf, wbuf, cnt)) {
		dev_err(&client->dev, "failed to read data from user\n");
		ret = -EIO;
		goto out;
	}

	if (cnt == 1) {
		info->log.cmd = *buf;
	} else {
		if (i2c_transfer(client->adapter, &msg, 1) != 1) {
			dev_err(&client->dev, "failed to transfer data\n");
			ret = -EIO;
			goto out;
		}
	}

	ret = 0;

out:
	kfree(buf);
tsp_disabled:
	mutex_unlock(&info->lock);

	return ret;
}

static const struct file_operations mit_fops = {
	.owner      = THIS_MODULE,
	.open       = mit_fs_open,
	.release    = mit_fs_release,
	.read       = mit_fs_read,
	.write      = mit_fs_write,
};

static ssize_t mit_intensity_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
	int ret = 0;
	mit_ts_rowcol(info);
	memset(info->get_data, 0, sizeof(info->get_data));
	if (get_intensity(info) == -1) {
		dev_err(&info->client->dev, "intensity printf failed");
		ret = -1;
		return ret;
	}
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->get_data);
	return ret;
}

static DEVICE_ATTR(intensity, 0666, mit_intensity_read, NULL);

static struct attribute *mit_attrs_test[] = {
	&dev_attr_intensity.attr,
	NULL,
};

static const struct attribute_group mit_attr_test_group = {
	.attrs = mit_attrs_test,
};

int mit_sysfs_test_mode(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	if (sysfs_create_group(&client->dev.kobj, &mit_attr_test_group)) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		return -EAGAIN;
	}

	info->get_data = kzalloc(sizeof(u8)*4096, GFP_KERNEL);

	return 0;
}

void mit_sysfs_remove(struct mit_ts_info *info)
{
	kfree(info->get_data);
	sysfs_remove_group(&info->client->dev.kobj, &mit_attr_test_group);
	return;
}

int mit_ts_log(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	if (alloc_chrdev_region(&info->mit_dev, 0, 1, "mit_ts")) {
		dev_err(&client->dev, "failed to allocate device region\n");
		return -ENOMEM;
	}

	cdev_init(&info->cdev, &mit_fops);
	info->cdev.owner = THIS_MODULE;

	if (cdev_add(&info->cdev, info->mit_dev, 1)) {
		dev_err(&client->dev, "failed to add ch dev\n");
		return -EIO;
	}

	info->class = class_create(THIS_MODULE, "mit_ts");
	device_create(info->class, NULL, info->mit_dev, NULL, "mit_ts");

	return 0;
}

void mit_ts_log_remove(struct mit_ts_info *info)
{
	device_destroy(info->class, info->mit_dev);
	class_destroy(info->class);
}


#endif
