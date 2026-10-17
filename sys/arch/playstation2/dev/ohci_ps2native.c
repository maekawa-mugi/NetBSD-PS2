/* SPDX-License-Identifier: BSD-2-Clause */
/* Native EE-side OHCI attachment, using IOP RAM and bounded tick polling. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <mips/cpuregs.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>
#include <playstation2/ee/sif_native.h>

struct native_ohci {
	struct ohci_softc ohci;
	callout_t poll;
	bool stopping;
};

static void
native_poll(void *arg)
{
	struct native_ohci *sc = arg;
	if (!sc->stopping && !sc->ohci.sc_dying) {
		(void)ohci_intr(&sc->ohci);
		callout_schedule(&sc->poll, 1);
	}
}

static bool
native_shutdown(device_t self, int flags)
{
	struct native_ohci *sc = device_private(self);
	/* Prevent the non-MPSAFE poll callout from rearming after HC reset. */
	sc->stopping = true;
	callout_stop(&sc->poll);
	return ohci_shutdown(self, flags);
}

static int
native_match(device_t parent, cfdata_t cf, void *aux)
{
	struct ps2sif_attach_args *sa = aux;
	return sa->dmat != NULL;
}

static void
native_attach(device_t parent, device_t self, void *aux)
{
	struct native_ohci *nsc = device_private(self);
	struct ohci_softc *sc = &nsc->ohci;
	struct ps2sif_attach_args *sa = aux;
	const bus_addr_t regs = MIPS_PHYS_TO_KSEG1(0x1f801600U);
	sc->sc_dev = self;
	sc->sc_bus.ub_hcpriv = sc;
	sc->sc_bus.ub_dmatag = sa->dmat;
	sc->sc_size = 0x100;
	sc->iot = bus_space_create(0, "native IOP OHCI", regs, sc->sc_size);
	sc->ioh = regs;
	aprint_normal(": native IOP OHCI, tick-polled completions\n");
	bus_space_write_4(sc->iot, sc->ioh, OHCI_INTERRUPT_DISABLE, OHCI_ALL_INTRS);
	int error = ohci_init(sc);
	if (error) {
		aprint_error_dev(self, "initialization failed: %d\n", error);
		return;
	}
	callout_init(&nsc->poll, 0);
	callout_setfunc(&nsc->poll, native_poll, nsc);
	pmf_device_register1(self, NULL, NULL, native_shutdown);
	callout_schedule(&nsc->poll, 1);
	sc->sc_child = config_found(self, &sc->sc_bus, usbctlprint, CFARGS_NONE);
}

CFATTACH_DECL_NEW(ohci_ps2native, sizeof(struct native_ohci),
    native_match, native_attach, NULL, NULL);
