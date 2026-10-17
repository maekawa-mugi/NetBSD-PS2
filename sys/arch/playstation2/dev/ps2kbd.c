/* SPDX-License-Identifier: BSD-2-Clause */
/* ASCII boot keyboard adapter for the direct GS/SIO console.
 * The BSD USB/HID stack owns enumeration and interrupt transfers. No IRX.
 * Only report-id-zero boot keyboards with eight-byte input are supported.
 * US/JIS ASCII positions and software repeat; no LEDs, IME or wscons.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/uhidev.h>
#include <dev/hid/hid.h>
#include <playstation2/playstation2/sio_console.h>

struct ps2kbd {
	struct uhidev *hid;
	kmutex_t lock;
	callout_t repeat;
	uint8_t previous[8];
	unsigned repeat_key;
	bool caps;
	bool jis;
	bool dying;
	bool opened;
};

static int
ascii_key(unsigned key, unsigned mods, bool caps, bool jis)
{
	bool shift = (mods & 0x22) != 0;
	bool ctrl = (mods & 0x11) != 0;
	if (key >= 4 && key <= 29) {
		if (ctrl)
			return key - 3;
		return (shift != caps ? 'A' : 'a') + key - 4;
	}
	int c;
	if (key >= 30 && key <= 39) {
		c = (shift ? (jis ? "!\"#$%&'()0" : "!@#$%^&*()") :
		    "1234567890")[key-30];
		goto control;
	}
	if (jis) {
		/* ASCII subset of NetBSD dev/hid/hidkbdmap.c's KB_JP map. */
		switch (key) {
		case 45: c = shift ? '=' : '-'; break;
		case 46: c = shift ? '~' : '^'; break;
		case 47: c = shift ? '`' : '@'; break;
		case 48: c = shift ? '{' : '['; break;
		case 49: case 50: c = shift ? '}' : ']'; break;
		case 51: c = shift ? '+' : ';'; break;
		case 52: c = shift ? '*' : ':'; break;
		case 53: return -1; /* Zenkaku/Hankaku needs an input method. */
		case 135: c = shift ? '_' : '\\'; break; /* Ro */
		case 137: c = shift ? '|' : '\\'; break; /* Yen */
		default: goto common;
		}
		goto control;
	}
common:
	switch (key) {
	case 40: return '\r';
	case 41: return 27;
	case 42: return '\b';
	case 43: return '\t';
	case 44: c = ' '; break;
	case 45: c = shift ? '_' : '-'; break;
	case 46: c = shift ? '+' : '='; break;
	case 47: c = shift ? '{' : '['; break;
	case 48: c = shift ? '}' : ']'; break;
	case 49: c = shift ? '|' : '\\'; break;
	case 51: c = shift ? ':' : ';'; break;
	case 52: c = shift ? '"' : '\''; break;
	case 53: c = shift ? '~' : '`'; break;
	case 54: c = shift ? '<' : ','; break;
	case 55: c = shift ? '>' : '.'; break;
	case 56: c = shift ? '?' : '/'; break;
	case 76: return 127;
	default: return -1;
	}
control:
	if (ctrl) {
		switch (c) {
		case ' ': case '@': return 0;
		case '[': case '{': return 27;
		case '\\': case '|': return 28;
		case ']': case '}': return 29;
		case '^': return 30;
		case '_': return 31;
		case '?': return 127;
		}
		/* Preserve familiar Ctrl+- even on an unshifted US keyboard. */
		if (!jis && key == 45)
			return 31;
	}
	return c;
}

static bool
key_down(const uint8_t *report, unsigned key)
{
	if (key != 0)
		for (unsigned i = 2; i < 8; i++)
			if (report[i] == key)
				return true;
	return false;
}

static void
keyboard_repeat(void *cookie)
{
	struct ps2kbd *sc = cookie;
	mutex_enter(&sc->lock);
	if (!sc->dying && key_down(sc->previous, sc->repeat_key)) {
		int c = ascii_key(sc->repeat_key, sc->previous[0], sc->caps,
		    sc->jis);
		if (c >= 0)
			ps2_console_input(c);
		callout_schedule(&sc->repeat, MAX(1, mstohz(33)));
	}
	mutex_exit(&sc->lock);
}

static void
keyboard_report(void *cookie, void *data, unsigned length)
{
	struct ps2kbd *sc = cookie;
	const uint8_t *r = data;
	if (length != 8)
		return;
	mutex_enter(&sc->lock);
	if (sc->dying)
		goto out;
	/* HID rollover/error reports contain no usable key transitions. */
	for (unsigned i = 2; i < 8; i++)
		if (r[i] > 0 && r[i] < 4) {
			sc->repeat_key = 0;
			callout_stop(&sc->repeat);
			goto out;
		}
	if (!key_down(r, sc->repeat_key)) {
		sc->repeat_key = 0;
		callout_stop(&sc->repeat);
	}
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
		int c = ascii_key(r[i], r[0], sc->caps, sc->jis);
		if (c >= 0) {
			ps2_console_input(c);
			sc->repeat_key = r[i];
			callout_schedule(&sc->repeat, MAX(1, mstohz(500)));
		}
	}
	memcpy(sc->previous, r, sizeof(sc->previous));
out:
	mutex_exit(&sc->lock);
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
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	callout_init(&sc->repeat, 0);
	callout_setfunc(&sc->repeat, keyboard_repeat, sc);
	usb_hid_descriptor_t *hd = usbd_get_hid_descriptor(ua->uiaa->uiaa_iface);
	/* Many keyboards leave the HID country code unspecified. CPJ1 is JIS. */
	sc->jis = (hd != NULL && hd->bCountryCode == 15) ||
	    (ua->uiaa->uiaa_vendor == 0x0510 && ua->uiaa->uiaa_product == 0x0002);
	aprint_normal(": boot keyboard to direct console (%s ASCII, repeat 500/33 ms)\n",
	    sc->jis ? "JIS" : "US");
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
keyboard_activate(device_t self, enum devact act)
{
	struct ps2kbd *sc = device_private(self);
	if (act != DVACT_DEACTIVATE)
		return EOPNOTSUPP;
	mutex_enter(&sc->lock);
	sc->dying = true;
	sc->repeat_key = 0;
	callout_stop(&sc->repeat);
	mutex_exit(&sc->lock);
	return 0;
}

static int
keyboard_detach(device_t self, int flags)
{
	struct ps2kbd *sc = device_private(self);
	(void)keyboard_activate(self, DVACT_DEACTIVATE);
	if (sc->opened) {
		uhidev_close(sc->hid);
		sc->opened = false;
		pmf_device_deregister(self);
	}
	/* No producer may rearm repeat after uhidev_close's callback barrier. */
	callout_halt(&sc->repeat, NULL);
	callout_destroy(&sc->repeat);
	mutex_destroy(&sc->lock);
	return 0;
}

CFATTACH_DECL_NEW(ps2kbd, sizeof(struct ps2kbd), keyboard_match,
    keyboard_attach, keyboard_detach, keyboard_activate);
