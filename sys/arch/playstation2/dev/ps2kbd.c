/* SPDX-License-Identifier: BSD-2-Clause */
/* Temporary ASCII boot keyboard adapter for the direct GS/SIO console.
 * The BSD USB/HID stack owns enumeration and interrupt transfers. No IRX.
 * Only report-id-zero boot keyboards with eight-byte input are supported.
 * US ASCII key positions, Shift/Caps/Ctrl; no repeat, LEDs, IME or wscons.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/uhidev.h>
#include <dev/hid/hid.h>
#include <playstation2/playstation2/sio_console.h>

struct ps2kbd {
	struct uhidev *hid;
	uint8_t previous[8];
	bool caps;
	bool opened;
};

static int
ascii_key(unsigned key, unsigned mods, bool caps)
{
	bool shift = (mods & 0x22) != 0;
	bool ctrl = (mods & 0x11) != 0;
	if (key >= 4 && key <= 29) {
		if (ctrl)
			return key - 3;
		return (shift != caps ? 'A' : 'a') + key - 4;
	}
	if (key >= 30 && key <= 39)
		return (shift ? "!@#$%^&*()" : "1234567890")[key-30];
	switch (key) {
	case 40: return '\r';
	case 41: return 27;
	case 42: return '\b';
	case 43: return '\t';
	case 44: return ' ';
	case 45: return ctrl ? 31 : (shift ? '_' : '-');
	case 46: return shift ? '+' : '=';
	case 47: return ctrl ? 27 : (shift ? '{' : '[');
	case 48: return ctrl ? 29 : (shift ? '}' : ']');
	case 49: return ctrl ? 28 : (shift ? '|' : '\\');
	case 51: return shift ? ':' : ';';
	case 52: return shift ? '"' : '\'';
	case 53: return shift ? '~' : '`';
	case 54: return shift ? '<' : ',';
	case 55: return shift ? '>' : '.';
	case 56: return shift ? '?' : '/';
	case 76: return 127;
	default: return -1;
	}
}

static void
keyboard_report(void *cookie, void *data, unsigned length)
{
	struct ps2kbd *sc = cookie;
	const uint8_t *r = data;
	if (length != 8)
		return;
	/* HID rollover/error reports contain no usable key transitions. */
	for (unsigned i = 2; i < 8; i++)
		if (r[i] > 0 && r[i] < 4)
			return;
	for (unsigned i = 2; i < 8; i++) {
		if (r[i] == 0)
			continue;
		bool held = false;
		for (unsigned j = 2; j < 8; j++)
			if (sc->previous[j] == r[i])
				held = true;
		for (unsigned j = 2; j < i; j++)
			if (r[j] == r[i])
				held = true;
		if (held)
			continue;
		if (r[i] == 57) {
			sc->caps = !sc->caps;
			continue;
		}
		int c = ascii_key(r[i], r[0], sc->caps);
		if (c >= 0)
			ps2_console_input(c);
	}
	memcpy(sc->previous, r, sizeof(sc->previous));
}

static int
keyboard_match(device_t parent, cfdata_t cf, void *aux)
{
	struct uhidev_attach_arg *ua = aux;
	usb_interface_descriptor_t *id = usbd_get_interface_descriptor(ua->uiaa->uiaa_iface);
	void *desc;
	int size;
	if (ua->reportid != 0 || id->bInterfaceSubClass != UISUBCLASS_BOOT ||
	    id->bInterfaceProtocol != UIPROTO_BOOT_KEYBOARD)
		return UMATCH_NONE;
	uhidev_get_report_desc(ua->parent, &desc, &size);
	return hid_report_size(desc, size, hid_input, 0) == 8 ?
	    UMATCH_IFACECLASS : UMATCH_NONE;
}

static void
keyboard_attach(device_t parent, device_t self, void *aux)
{
	struct ps2kbd *sc = device_private(self);
	struct uhidev_attach_arg *ua = aux;
	sc->hid = ua->parent;
	aprint_normal(": boot keyboard to direct console (US ASCII positions)\n");
	if (usbd_set_protocol(ua->uiaa->uiaa_iface, 0) != USBD_NORMAL_COMPLETION) {
		aprint_error_dev(self, "cannot select boot protocol\n");
		return;
	}
	(void)usbd_set_idle(ua->uiaa->uiaa_iface, 0, 0);
	int error = uhidev_open(sc->hid, keyboard_report, sc);
	if (error) {
		aprint_error_dev(self, "interrupt pipe failed: %d\n", error);
		return;
	}
	sc->opened = true;
	pmf_device_register(self, NULL, NULL);
}

static int
keyboard_detach(device_t self, int flags)
{
	struct ps2kbd *sc = device_private(self);
	if (sc->opened) {
		uhidev_close(sc->hid);
		pmf_device_deregister(self);
	}
	return 0;
}

CFATTACH_DECL_NEW(ps2kbd, sizeof(struct ps2kbd), keyboard_match,
    keyboard_attach, keyboard_detach, NULL);
