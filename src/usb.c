/*
 * Copyright 2016 Jerry Jacobs. All rights reserved.
 * Use of this source code is governed by the MIT
 * license that can be found in the LICENSE file.
 */

/**
 * @file src/usb.c
 */
#include <stlink2.h>
#include <stlink2-internal.h>
#include <stlink2/utils/hexstr.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define STLINK2_USB_TIMEOUT_MS 1000U
#define STLINK2_USB_RX_EP      (1 | LIBUSB_ENDPOINT_IN)  /**< USB RX endpoint */
#define STLINK2_USB_V2_TX_EP   (2 | LIBUSB_ENDPOINT_OUT) /**< USB TX endpoint for Stlink2 */
#define STLINK2_USB_V2_1_TX_EP (1 | LIBUSB_ENDPOINT_OUT) /**< USB TX endpoint for Stlink2-1 */

static bool stlink2_usb_claim(struct stlink2 *dev)
{
	int ret;
	int config;

	ret = libusb_kernel_driver_active(dev->usb.dev, 0);
	if (ret) {
		ret = libusb_detach_kernel_driver(dev->usb.dev, 0);
		if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
			STLINK2_LOG_TRACE(dev, "libusb_detach_kernel_driver (%s)\n", libusb_error_name(ret));
		} else if (ret) {
			STLINK2_LOG_ERROR(dev, "libusb_detach_kernel_driver failed (%s)\n", libusb_error_name(ret));
			return false;
		}
	}

	ret = libusb_get_configuration(dev->usb.dev, &config);
	if (ret) {
		STLINK2_LOG_ERROR(dev, "libusb_get_configuration failed (%s)\n", libusb_error_name(ret));
		return false;
	}

	ret = libusb_set_configuration(dev->usb.dev, 1);
	if (ret) {
		STLINK2_LOG_ERROR(dev, "libusb_set_configuration failed (%s)\n", libusb_error_name(ret));
		return false;
	}

	ret = libusb_claim_interface(dev->usb.dev, 0);
	if (ret != 0) {
		STLINK2_LOG_ERROR(dev, "libusb_claim_interface failed (%s)\n", libusb_error_name(ret));
		return false;
	}

	return true;
}

static char *stlink2_usb_read_serial_from_bin(const char *serial, size_t len)
{
	/** @todo need to get rid of this weird calculation... + 1, - 1 */
	const size_t size = (len * 2) + 1;
	char *_serial = malloc(size);

	if (!_serial)
		return NULL;

	stlink2_hexstr_from_bin(_serial, size - 1, serial, len);
	_serial[size - 1] = 0;

	return _serial;
}

/**
 * Read binary or hex encoded serial from usb handle
 * @note The pointer must be freed by the callee when != NULL
 * @return hex encoded string
 */
static char *stlink2_usb_read_serial(struct stlink2 *st, libusb_device_handle *handle,
				     struct libusb_device_descriptor *desc)
{
	int ret;
	char serial[128];
	bool ishexserial = true;

	memset(serial, 0, sizeof(serial));

	ret = libusb_get_string_descriptor_ascii(handle, desc->iSerialNumber, (unsigned char *)&serial, sizeof(serial));
	if (ret < 0) {
		STLINK2_LOG_ERROR(st, "libusb_get_string_descriptor_ascii failed (%s)\n", libusb_error_name(ret));
		return NULL;
	}

	for (int n = 0; n < ret; n++) {
		if (isxdigit(serial[n]))
			continue;

		ishexserial = false;
		break;
	}

	if (!ishexserial)
		return stlink2_usb_read_serial_from_bin(serial, (size_t)ret);

	return stlink2_strdup(serial);
}

/** Configure USB endpoint numbers */
static void stlink2_usb_config_endpoints(struct stlink2 *dev)
{
	dev->usb.tx_ep = STLINK2_USB_RX_EP;
	if (dev->usb.pid == STLINK2_USB_PID_V2)
		dev->usb.rx_ep = STLINK2_USB_V2_TX_EP;
	else if (dev->usb.pid == STLINK2_USB_PID_V2_1)
		dev->usb.rx_ep = STLINK2_USB_V2_1_TX_EP;
}

bool stlink2_usb_probe_dev(libusb_device *dev, struct stlink2 *st, bool attach)
{
	int ret = 0;
	struct libusb_device_descriptor desc;
	libusb_device_handle *devh;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret) {
		STLINK2_LOG_ERROR(st, "libusb_get_device_descriptor failed (%s)\n", libusb_error_name(ret));
		return false;
	}

	if (desc.idProduct != STLINK2_USB_PID_V2 &&
	    desc.idProduct != STLINK2_USB_PID_V2_1)
		return false;

	ret = libusb_open(dev, &devh);
	if (ret) {
		STLINK2_LOG_ERROR(st, "libusb_open failed (%s)\n", libusb_error_name(ret));
		return false;
	}

	st->serial = stlink2_usb_read_serial(st, devh, &desc);
	if (!st->serial) {
		STLINK2_LOG_ERROR(st, "stlink2_usb_read_serial failed\n");
		libusb_close(devh);
		return false;
	}

	if (!attach) {
		libusb_close(devh);
		return true;
	}

	st->usb.dev     = devh;
	st->usb.timeout = STLINK2_USB_TIMEOUT_MS;
	st->usb.pid     = desc.idProduct;

	stlink2_usb_claim(st);
	stlink2_usb_config_endpoints(st);
	stlink2_usb_set_name_from_pid(st);

	return true;
}

/**
 * Set programmer name based on USB PID
 */
void stlink2_usb_set_name_from_pid(struct stlink2 *dev)
{
	static const char *stlinkv2   = "st-link/v2";
	static const char *stlinkv2_1 = "st-link/v2-1";

	if (dev->usb.pid == STLINK2_USB_PID_V2)
		dev->name = stlinkv2;
	else if (dev->usb.pid == STLINK2_USB_PID_V2_1)
		dev->name = stlinkv2_1;
}

void stlink2_usb_reset(struct stlink2 *dev)
{
	int ret;

	ret = libusb_clear_halt(dev->usb.dev, dev->usb.rx_ep);
	STLINK2_LOG_DEBUG(dev, "libusb_clear_halt rx_ep: %d\n", ret);

	ret = libusb_clear_halt(dev->usb.dev, dev->usb.tx_ep);
	STLINK2_LOG_DEBUG(dev, "libusb_clear_halt tx_ep: %d\n", ret);

	ret = libusb_reset_device(dev->usb.dev);
	STLINK2_LOG_DEBUG(dev, "libusb_reset_device: %d\n", ret);
}

void stlink2_usb_cleanup(struct stlink2 *dev)
{
#ifndef STLINK2_HAVE_MACOSX
	(void)dev;
#else
	/* WORKAROUND for OS/X 10.11+ read from ST-Link, must be performed even times
           or else LIBUSB_ERROR_TIMEOUT can occur at next clean application start */
	if (dev->usb.xfer_count & 1)
		stlink2_get_mode(dev);
#endif
}

ssize_t stlink2_usb_send_recv(struct stlink2 *dev,
			      uint8_t *txbuf, size_t txsize,
			      uint8_t *rxbuf, size_t rxsize)
{
	int ret;
	int res;
	int tries = 0;

	while (tries < 5) {
		ret = libusb_bulk_transfer(dev->usb.dev, dev->usb.rx_ep,
					   txbuf,
					   (int)txsize,
					   &res,
					   dev->usb.timeout);
		if (ret)
			STLINK2_LOG_ERROR(dev, "libusb_bulk_transfer tx failed (%s)\n", libusb_error_name(ret));
		else
			break;

		tries++;
	}

	if (ret) {
		STLINK2_LOG_ERROR(dev, "libusb_bulk_transfer tx failed (%s)\n", libusb_error_name(ret));
		return 0;
	}

	STLINK2_LOG_TRACE(dev, "USB > ");
	for (size_t n = 0; n < txsize; n++)
		STLINK2_LOG_WRITE(TRACE, dev, "%02x ", txbuf[n]);
	STLINK2_LOG_WRITE(TRACE, dev, "\n");

	if (!rxbuf || !rxsize)
		return 0;

	tries = 0;
	while (tries < 5) {
	ret = libusb_bulk_transfer(dev->usb.dev, dev->usb.tx_ep,
				   rxbuf,
				   (int)rxsize,
				   &res,
				   dev->usb.timeout);
		if (ret == 0)
			break;
		else
			STLINK2_LOG_WARN(dev, "libusb_bulk_transfer rx failed (%s)\n", libusb_error_name(ret));

		tries++;
	}

	if (ret) {
		STLINK2_LOG_ERROR(dev, "libusb_bulk_transfer rx failed (%s)\n", libusb_error_name(ret));
		return 0;
	}

	STLINK2_LOG_TRACE(dev, "USB < ");
	for (size_t n = 0; n < rxsize; n++)
		STLINK2_LOG_WRITE(TRACE, dev, "%02x ", rxbuf[n]);
	STLINK2_LOG_WRITE(TRACE, dev, "\n");

#ifdef STLINK2_HAVE_MACOSX
	dev->usb.xfer_count++;
#endif

	return 0;
}
