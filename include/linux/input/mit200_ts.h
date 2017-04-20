#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/input/mit_ts.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
/*         */
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#define LGD_TOUCH_NAME "lgd_melfas_incell_touch"
#define LGE_TOUCH_NAME		"lge_touch"
/*       */

/* Firmware file name */
#define FW_NAME                 "melfas/mit200/L0M45P1_0_39.fw"
#define EXTRA_FW_PATH           "/sdcard/mit_ts.fw"

/* MIT TEST mode */
#define __MIT_TEST_MODE__

/*  Show the touch log  */
#define KERNEL_LOG_MSG       1
#define VERSION_NUM          2
#define MAX_FINGER_NUM       10
#define FINGER_EVENT_SZ      8
#define MAX_WIDTH            30
#define MAX_PRESSURE         255
#define MAX_LOG_LENGTH       128

/* Registers */

#define MIT_REGH_CMD         0x10

#define MIT_REGL_MODE_CONTROL       0x01
#define MIT_REGL_ROW_NUM            0x0B
#define MIT_REGL_COL_NUM            0x0C
#define MIT_REGL_RAW_TRACK          0x0E
#define MIT_REGL_EVENT_PKT_SZ       0x0F
#define MIT_REGL_INPUT_EVENT        0x10
#define MIT_REGL_UCMD               0xA0
#define MIT_REGL_UCMD_RESULT_LENGTH 0xAE
#define MIT_REGL_UCMD_RESULT        0xAF
#define MIT_FW_VERSION              0xC2

/* Universal commands */
#define MIT_UNIV_ENTER_TESTMODE     0x40
#define MIT_UNIV_TESTA_START        0x41
#define MIT_UNIV_GET_RAWDATA        0x44
#define MIT_UNIV_TESTB_START        0x48
#define MIT_UNIV_GET_OPENSHORT_TEST 0x50
#define MIT_UNIV_EXIT_TESTMODE      0x6F

/* Event types */
#define MIT_ET_LOG                  0xD
#define MIT_ET_NOTIFY               0xE
#define MIT_ET_ERROR                0xF

/* ISC mode */
#define ISC_MASS_ERASE   {0xFB, 0x4A, 0x00, 0x15, 0x00, 0x00}
#define ISC_PAGE_WRITE   {0xFB, 0x4A, 0x00, 0x5F, 0x00, 0x00}
#define ISC_FLASH_READ   {0xFB, 0x4A, 0x00, 0xC2, 0x00, 0x00}
#define ISC_STATUS_READ  {0xFB, 0x4A, 0x00, 0xC8, 0x00, 0x00}
#define ISC_EXIT         {0xFB, 0x4A, 0x00, 0x66, 0x00, 0x00}


enum {
	GET_COL_NUM	= 1,
	GET_ROW_NUM,
	GET_EVENT_DATA,
};

enum {
	LOG_TYPE_U08	= 2,
	LOG_TYPE_S08,
	LOG_TYPE_U16,
	LOG_TYPE_S16,
	LOG_TYPE_U32	= 8,
	LOG_TYPE_S32,
};

struct mit_ts_info {
	struct i2c_client   *client;
	struct input_dev    *input_dev;
	char                phys[32];
	struct regulator    *vcc_i2c; /*     */
	struct regulator    *vdd; /*     */
	u8              row_num;
	u8              col_num;

	int             irq;

	struct mit_ts_platform_data	*pdata;
	char                *fw_name;
	struct completion   init_done;
	struct pinctrl      *ts_pinctrl;
/*         */
#ifdef CONFIG_FB
	struct notifier_block fb_notifier;
#endif
/*       */
#ifdef CONFIG_EARLY_SUSPEND
	struct early_suspend  early_suspend;
#endif
	struct mutex          lock;
	bool                  enabled;

#ifdef __MIT_TEST_MODE__
	struct cdev         cdev;
	dev_t               mit_dev;
	struct class        *class;

	char            raw_cmd;
	u8              *get_data;
	struct mit_log_data {
		u8          *data;
		int         cmd;
	} log;
#endif
	struct completion fw_completion;
};
void mit_clear_input_data(struct mit_ts_info *info);
void mit_report_input_data(struct mit_ts_info *info, u8 sz, u8 *buf);
void mit_reboot(struct mit_ts_info *info);
int get_fw_version(struct i2c_client *client, u8 *buf);
int mit_flash_fw(struct mit_ts_info *info, const u8 *fw_data, size_t fw_size);

#ifdef __MIT_TEST_MODE__
int mit_sysfs_test_mode(struct mit_ts_info *info);
void mit_sysfs_remove(struct mit_ts_info *info);

int mit_ts_log(struct mit_ts_info *info);
void mit_ts_log_remove(struct mit_ts_info *info);
#endif
