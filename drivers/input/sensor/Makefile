#
# Makefile for the input sensor drivers.
#

# Each configuration option enables a list of files.
EXTRA_CFLAGS += -DBMA2X2_SENSOR_IDENTIFICATION_ENABLE
EXTRA_CFLAGS += -DCONFIG_BMA_USE_PLATFORM_DATA
EXTRA_CFLAGS += -DBMM_USE_BASIC_I2C_FUNC
EXTRA_CFLAGS += -DCONFIG_BMM_USE_PLATFORM_DATA
EXTRA_CFLAGS += -DBMA2X2_ACCEL_CALIBRATION

obj-$(CONFIG_SENSOR_APDS9130)	+= apds9130.o
obj-$(CONFIG_SENSOR_K2DH)       += k3dh_acc.o
obj-$(CONFIG_SENSOR_AK09911)    += akm09911.o
obj-$(CONFIG_SENSORS_BMC150)    += bma2x2_driver.o bmm050_driver.o bmm050.o
obj-$(CONFIG_SENSORS_BMC150_COMMON)    += bst_sensor_common.o
