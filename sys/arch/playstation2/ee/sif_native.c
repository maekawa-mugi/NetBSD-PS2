/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Native, bounded, polled EE SIF bootstrap transport.
 * Hardware DMA follows the EE User Manual's source/destination chains.
 * Wire words implement the resident IOP SIF CMD/RPC protocol. This file
 * does not include/link SDK code, SDK headers, IRX binaries or SBIOS calls.
 *
 * At this stage the boot ROM's IOP kernel/services remain running. This
 * is not yet an IOP reset or interrupt-driven RPC stack. The optional
 * PS2_IOP_BOOTSTRAP profile tests an independently authored module loader.
 * A sleepable mutex serializes runtime heap/module RPC. Child
 * devices suballocate a reserved IOP DMA pool without issuing heap RPC.
 */
#include <sys/cdefs.h>
#include "opt_ps2_nosbios.h"
#include "opt_ps2_iop_bootstrap.h"
#ifndef PS2_NOSBIOS
#error "native SIF requires exclusive ownership in an SBIOS-free profile"
#endif
#include <sys/param.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <mips/cache.h>
#include <mips/cpuregs.h>
#include <machine/autoconf.h>
#include <playstation2/ee/sif_native.h>
#ifdef PS2_IOP_BOOTSTRAP
#include <playstation2/iop/bootstrap.h>
#include <playstation2/iop/usb_irq.h>
#include <playstation2/ee/intcvar.h>
#include <sys/intr.h>
#include "ps2_iop_images.h"
#endif

#define REG(a) (*(volatile uint32_t *)MIPS_PHYS_TO_KSEG1(a))
#define D5 0x1000c000
#define D6 0x1000c400
#define DSTAT 0x1000e010
#define SIF_SUB 0x1000f210
#define SIF_SMFLAG 0x1000f230
#define CMD_REG 0x80000001U
#define CMD_INIT 0x80000002U
#define RPC_END 0x80000008U
#define RPC_BIND 0x80000009U
#define RPC_CALL 0x8000000aU
#define RPC_READ 0x8000000cU
#define HEAP_SERVICE 0x80000003U
#define CMD_MAX 112
#define IO_LIMIT 0x00200000U /* bootstrap services are in retail IOP RAM */

/* Each object exclusively owns complete cache lines. DMA uses only its
 * uncached alias after the initial cache writeback/invalidate. */
static uint32_t rx_store[32] __aligned(64);
static uint32_t tx_store[64] __aligned(64);
static uint32_t reply_store[32] __aligned(64);
static volatile uint32_t *rx, *tx, *reply;
static uint32_t iop_command, sequence, active_cookie, active_command;
static uint32_t bound_server, bound_buffer;
static bool completed;
static unsigned received;
static kmutex_t rpc_lock;
static bool service_ready;
static int heap_call(unsigned, uint32_t, uint32_t *);

int
ps2sif_alloc(size_t size, uint32_t *address)
{
	int error;
	if (size == 0 || size > IO_LIMIT)
		return EINVAL;
	mutex_enter(&rpc_lock);
	if (!service_ready) {
		error = EIO;
	} else {
		error = heap_call(1, size, address);
		if (!error && (*address == 0 || *address >= IO_LIMIT ||
		    size > IO_LIMIT - *address))
			error = ENOMEM;
		if (error && error != ENOMEM)
			service_ready = false;
	}
	mutex_exit(&rpc_lock);
	return error;
}

int
ps2sif_free(uint32_t address)
{
	uint32_t result = 0;
	int error;
	if (address == 0 || address >= IO_LIMIT)
		return EINVAL;
	mutex_enter(&rpc_lock);
	error = service_ready ? heap_call(2, address, &result) : EIO;
	if (error)
		service_ready = false;
	else if (result != 0)
		error = EINVAL;
	mutex_exit(&rpc_lock);
	return error;
}

static uint32_t
physical(const volatile void *p)
{
	return (uint32_t)(uintptr_t)p & 0x1fffffff;
}

static volatile uint32_t *
uncached(void *p, size_t n)
{
	mips_dcache_wbinv_range((vaddr_t)p, n);
	return (volatile uint32_t *)MIPS_PHYS_TO_KSEG1(physical(p));
}

static uint32_t
ticks(void)
{
	uint32_t value;
	__asm volatile("mfc0 %0,$9" : "=r"(value));
	return value;
}

static void
barrier(void)
{
	__asm volatile("sync.l; sync.p" ::: "memory");
}

static bool
wait_clear(uint32_t reg, uint32_t mask)
{
	uint32_t start = ticks();
	while (REG(reg) & mask) {
		if ((uint32_t)(ticks() - start) > 147456000U * 2)
			return false;
	}
	return true;
}

static void
receive_arm(void)
{
	rx[0] = 0;
	barrier();
	REG(DSTAT) = 1U << 5; /* W1C status only, never toggle DMA masks */
	REG(D5 + 0x20) = 0;
	REG(D5) = 0x184; /* destination chain, stop on tag IRQ */
}

static int
receive_poll(void)
{
	uint32_t packet[32];
	if (!(REG(DSTAT) & (1U << 5)) || (REG(D5) & 0x100))
		return 0;
	barrier();
	unsigned size = rx[0] & 255;
	if (size < 16 || size > CMD_MAX) {
		/* DMA may also complete without a command during shutdown. */
		receive_arm();
		return EPROTO;
	}
	for (unsigned i = 0; i < (size + 3) / 4; i++)
		packet[i] = rx[i];
	/* Re-arm before servicing the packet, so IOP can send the next one. */
	receive_arm();
	received++;
	if (packet[2] == RPC_END && size >= 48 &&
	    packet[7] == active_cookie && packet[8] == active_command) {
		if (packet[8] == RPC_BIND) {
			bound_server = packet[9];
			bound_buffer = packet[10];
		}
		completed = true;
	}
	return 0;
}

/* Submit one IOP destination tag plus payload through an EE END chain.
 * END belongs to the EE chain; bit31/30 belong to the IOP tag. */
static int
send_to_iop(uint32_t dest, const uint32_t *data, size_t bytes, bool command)
{
	if (dest == 0 || (dest & 3) || dest >= IO_LIMIT || bytes == 0 ||
	    (bytes & 3) || bytes > 128 || roundup(bytes, 16) > IO_LIMIT - dest)
		return EINVAL;
	if (!wait_clear(D6, 0x100))
		return ETIMEDOUT;
	unsigned qwords = (bytes + 15) / 16;
	tx[0] = 0x70000000U | (qwords + 1); /* EE END: IOP tag + data */
	tx[1] = tx[2] = tx[3] = 0;
	tx[4] = dest | (command ? 0xc0000000U : 0);
	tx[5] = qwords * 4;
	tx[6] = tx[7] = 0;
	for (unsigned i = 0; i < qwords * 4; i++)
		tx[8 + i] = i < bytes / 4 ? data[i] : 0;
	barrier();
	REG(D6 + 0x20) = 0;
	REG(D6 + 0x30) = physical(tx);
	REG(D6) = 0x105; /* source chain, start; no EE tag transfer */
	return wait_clear(D6, 0x100) ? 0 : ETIMEDOUT;
}

static int
send_command(uint32_t *packet, unsigned size, uint32_t command)
{
	if (size < 16 || size > CMD_MAX || (size & 3))
		return EINVAL;
	packet[0] = (packet[0] & 0xffffff00U) | size;
	packet[2] = command;
	return send_to_iop(iop_command, packet, size, true);
}

static int
wait_reply(void)
{
	uint32_t start = ticks();
	while (!completed) {
		int error = receive_poll();
		if (error)
			return error;
		if ((uint32_t)(ticks() - start) > 147456000U * 2)
			return ETIMEDOUT;
	}
	barrier();
	return 0;
}

static void
rpc_header(uint32_t *packet, uint32_t command)
{
	/* Opaque transaction cookie: returned by IOP, never an IOP pointer. */
	active_cookie = ++sequence;
	active_command = command;
	completed = false;
	packet[4] = 5;
	packet[5] = (uint32_t)(uintptr_t)reply;
	packet[6] = sequence;
	packet[7] = active_cookie;
}

static int
bind_service(uint32_t service)
{
	uint32_t p[16] = {0};
	rpc_header(p, RPC_BIND);
	p[8] = service;
	int error = send_command(p, sizeof(p), RPC_BIND);
	if (!error)
		error = wait_reply();
	if (!error && (bound_server == 0 || bound_buffer == 0 ||
	    bound_buffer >= IO_LIMIT))
		error = ENXIO;
	return error;
}

static int
heap_call(unsigned operation, uint32_t argument, uint32_t *result)
{
	uint32_t p[16] = {0};
	rpc_header(p, RPC_CALL);
	p[8] = operation;
	p[9] = 4;
	p[10] = physical(reply);
	p[11] = 4;
	p[12] = 1;
	p[13] = bound_server;
	p[0] = 4U << 8;
	p[1] = bound_buffer;
	int error = send_to_iop(bound_buffer, &argument, 4, false);
	if (!error)
		error = send_command(p, sizeof(p), RPC_CALL);
	if (!error)
		error = wait_reply();
	if (!error)
		*result = reply[0];
	return error;
}

static int
read_iop(uint32_t source, uint32_t *data, size_t size)
{
	uint32_t p[16] = {0};
	rpc_header(p, RPC_READ);
	p[8] = source;
	p[9] = physical(reply);
	p[10] = size;
	int error = send_command(p, sizeof(p), RPC_READ);
	if (!error)
		error = wait_reply();
	if (!error)
		for (unsigned i = 0; i < size / 4; i++)
			data[i] = reply[i];
	return error;
}

static int
native_init(void)
{
	rx = uncached(rx_store, sizeof(rx_store));
	tx = uncached(tx_store, sizeof(tx_store));
	reply = uncached(reply_store, sizeof(reply_store));
	uint32_t start = ticks();
	while (!(REG(SIF_SMFLAG) & 0x20000))
		if ((uint32_t)(ticks() - start) > 147456000U * 2)
			return ETIMEDOUT;
	iop_command = REG(SIF_SUB);
	if (iop_command == 0 || iop_command >= IO_LIMIT)
		return ENXIO;
	/* This native owner never unmasks the bootstrap DMA channels. */
	REG(D5) = 0;
	REG(D6) = 0;
	REG(0x1000e000) |= 1; /* DMAC master enable */
	REG(0x1000f260) = 0xff;
	receive_arm();
	uint32_t p[8] = {0};
	p[4] = physical(rx);
	int error = send_command(p, 20, CMD_INIT);
	if (error)
		return error;
	memset(p, 0, sizeof(p));
	p[3] = 1; /* initialize IOP RPC and request the RPCINIT sreg reply */
	error = send_command(p, 16, CMD_INIT);
	/* The ELF boot loader may already have initialized IOP RPC. In that
	 * case the one-time RPCINIT event need not be sent again. A successful
	 * service bind below is the actual readiness test. */
	return error;
}

#ifdef PS2_IOP_BOOTSTRAP
static volatile uint32_t *
iop_window(uint32_t address)
{
	return (volatile uint32_t *)MIPS_PHYS_TO_KSEG1(0x1c000000U + address);
}

static bool
iop_function(uint32_t address)
{
	uint32_t physical_address = address & 0x1fffffffU;
	if (address >= IO_LIMIT && (address & 0xe0000000U) != 0x80000000U &&
	    (address & 0xe0000000U) != 0xa0000000U)
		return false;
	return physical_address >= 0x1000 && physical_address < IO_LIMIT &&
	    !(address & 3);
}

static int
find_bootstrap_apis(struct ps2_iop_boot_request *r)
{
	unsigned loaders = 0, cores = 0;
	/* Export headers are mutable linked-list entries after ROM linking;
	 * the original IRX export magic is no longer a reliable discriminator.
	 * Reject ambiguous tables rather than assuming a fixed ROM address. */
	for (uint32_t at = 0x1000; at < IO_LIMIT - 128; at += 4) {
		volatile uint32_t *v = iop_window(at);
		if (v[3] == 0x6c646f6dU && v[4] == 0x0064616fU &&
		    iop_function(v[13]) && iop_function(v[15])) {
			r->load_module = v[15];
			r->start_module = v[13];
			loaders++;
		}
		if (v[3] == 0x64616f6cU && v[4] == 0x65726f63U &&
		    iop_function(v[9])) {
			r->flush_icache = v[9];
			cores++;
		}
	}
	return loaders == 1 && cores == 1 ? 0 : ENOTSUP;
}

static int
upload_iop(uint32_t address, const uint32_t *data, size_t bytes)
{
	for (size_t off = 0; off < bytes; off += 128) {
		int error = send_to_iop(address + off, data + off / 4,
		    MIN(128, bytes - off), false);
		if (error)
			return error;
	}
	return 0;
}

int
ps2sif_exec_irx(const uint32_t *image, size_t bytes, const char *arguments,
    size_t argument_size, struct ps2sif_module_result *result)
{
	struct ps2_iop_boot_request r = {0};
	uint32_t p[16] = {0}, allocation = 0, ignored;
	uint32_t saved_server, saved_buffer, old_entry = 0;
	volatile uint32_t *server = NULL;
	bool submitted = false, finished = false;
	int error;
	if (image == NULL || ((uintptr_t)image & 3) || result == NULL ||
	    bytes < 52 || bytes > 65536 ||
	    (bytes & 3) || image[0] != 0x464c457fU ||
	    argument_size > sizeof(r.arguments) ||
	    (argument_size && (arguments == NULL || arguments[argument_size - 1] != 0)))
		return EINVAL;
	memset(result, 0, sizeof(*result));
	result->module_id = result->entry_result = result->start_result = -1;
	mutex_enter(&rpc_lock);
	if (!service_ready) {
		mutex_exit(&rpc_lock);
		return EIO;
	}
	saved_server = bound_server;
	saved_buffer = bound_buffer;
	error = find_bootstrap_apis(&r);
	if (error)
		goto out;
	size_t imagebytes = roundup(bytes, 16);
	size_t total = imagebytes + roundup(sizeof(ps2_iop_bootstrap_image), 16);
	error = heap_call(1, total, &allocation);
	if (error)
		goto out;
	if (!allocation || allocation >= IO_LIMIT || (allocation & 15) ||
	    total > IO_LIMIT - allocation) {
		error = EPROTO;
		allocation = 0; /* Never free an unvalidated server-supplied address. */
		goto out;
	}
	error = upload_iop(allocation, image, bytes);
	if (!error)
		error = upload_iop(allocation + imagebytes, ps2_iop_bootstrap_image,
		    sizeof(ps2_iop_bootstrap_image));
	if (!error)
		error = bind_service(0x80000006U);
	if (error)
		goto out;
	if (!bound_server || (bound_server & 3) || bound_server >= IO_LIMIT - 128 ||
	    !bound_buffer || (bound_buffer & 3) || bound_buffer >= IO_LIMIT - sizeof(r)) {
		error = EPROTO;
		goto out;
	}
	server = iop_window(bound_server);
	old_entry = server[1];
	if (server[0] != 0x80000006U || !iop_function(old_entry)) {
		error = EPROTO;
		server = NULL;
		goto out;
	}
	r.image = allocation;
	r.argument_size = argument_size;
	r.module_id = r.module_result = r.start_result = -1;
	memcpy(r.name, "NetBSD", 7);
	if (argument_size)
		memcpy(r.arguments, arguments, argument_size);
	error = upload_iop(bound_buffer, (const uint32_t *)&r, sizeof(r));
	if (error)
		goto out;
	/* Execute uncached on the IOP: no assumption about stale I-cache tags
	 * at newly allocated heap addresses. The generated code has no J/JAL,
	 * gp references or relocations. Only this EE owns LOADFILE transactions. */
	server[1] = (allocation + imagebytes) | 0xa0000000U;
	barrier();
	rpc_header(p, RPC_CALL);
	p[0] = sizeof(r) << 8;
	p[1] = bound_buffer;
	p[8] = PS2_IOP_BOOT_COMMAND;
	p[9] = sizeof(r);
	p[10] = physical(reply);
	p[11] = sizeof(*result);
	p[12] = 1;
	p[13] = bound_server;
	for (unsigned i = 0; i < 4; i++)
		reply[i] = 0xa5a5a5a5U;
	submitted = true; /* A DMA timeout does not prove IOP did not receive it. */
	error = send_command(p, sizeof(p), RPC_CALL);
	if (!error)
		error = wait_reply();
	finished = error == 0;
	server[1] = old_entry;
	barrier();
	if (!error) {
		result->module_id = reply[0];
		result->entry_result = reply[1];
		result->start_result = reply[2];
		result->signature = reply[3];
		if (result->signature != PS2_IOP_BOOT_SIGNATURE)
			error = EPROTO;
		else if (result->module_id < 0 || result->start_result < 0)
			error = ENOEXEC;
	}
out:
	/* Restore the RPC callback and client binding before any heap calls. */
	if (server != NULL && old_entry) {
		server[1] = old_entry;
		barrier();
	}
	bound_server = saved_server;
	bound_buffer = saved_buffer;
	if (allocation && (!submitted || finished)) {
		int free_error = heap_call(2, allocation, &ignored);
		if (free_error || ignored) {
			service_ready = false;
			if (!error)
				error = free_error ? free_error : EIO;
		}
	}
	if (submitted && !finished)
		service_ready = false; /* Retain all potentially executing code/data. */
	mutex_exit(&rpc_lock);
	return error;
}

static void
bootstrap_selftest(device_t self)
{
	uint32_t mailbox;
	struct ps2sif_module_result result = {
		.module_id = -1, .entry_result = -1, .start_result = -1
	};
	int error = ps2sif_alloc(64, &mailbox);
	if (error) {
		aprint_error_dev(self, "IOP bootstrap mailbox allocation: %d\n", error);
		return;
	}
	volatile uint32_t *words = iop_window(mailbox);
	for (unsigned i = 0; i < 16; i++)
		words[i] = 0xa5a5a5a5U;
	barrier();
	char arg[9];
	snprintf(arg, sizeof(arg), "%08x", mailbox);
	error = ps2sif_exec_irx(ps2_iop_smoke_image, sizeof(ps2_iop_smoke_image),
	    arg, sizeof(arg), &result);
	bool valid = !error && words[0] == 0x4e425344U && words[1] == 0x494f5031U &&
	    words[2] == mailbox && words[3] == 2 && result.entry_result == 1;
	for (unsigned i = 4; i < 16; i++)
		if (words[i] != 0xa5a5a5a5U)
			valid = false;
	aprint_normal_dev(self, "independent IOP module: error=%d id=%d entry=%d start=%d mailbox=%s signature=%#x\n",
	    error, result.module_id, result.entry_result, result.start_result,
	    valid ? "OK" : "FAILED", result.signature);
	if (!error)
		(void)ps2sif_free(mailbox);
}
#endif

/* The native profile exclusively owns INTC channel 1 (no sbus attachment).
 * Mailbox and resident module are deliberately never freed, even on failure.
 * RPC timeout cannot establish whether an IOP handler was installed. */
#ifdef PS2_IOP_BOOTSTRAP
static volatile struct ps2_usb_irq_mailbox *usb_mailbox;
static int (*usb_handler)(void *);
static void *usb_context, *usb_ih;
static bool usb_ready, usb_stopped;
static unsigned usb_sbus_count, usb_recovered;

static bool
usb_mailbox_valid(void)
{
	if (usb_mailbox->magic != PS2_USB_IRQ_MAGIC)
		return false;
	for (unsigned i = 0; i < 12; i++)
		if (usb_mailbox->guard[i] != PS2_USB_IRQ_GUARD)
			return false;
	return true;
}

static int
usb_relay_intr(void *arg)
{
	(void)arg;
	if (!(REG(SIF_SMFLAG) & 0x400))
		return 0;
	REG(SIF_SMFLAG) = 0x400;
	barrier();
	usb_sbus_count++;
	if (usb_ready && !usb_stopped)
		ps2sif_usb_intr_poll();
	return 1;
}
#endif

void
ps2sif_usb_intr_poll(void)
{
#ifdef PS2_IOP_BOOTSTRAP
	if (!usb_ready || usb_stopped)
		return;
	int s = splbio();
	if (!usb_mailbox_valid()) {
		usb_stopped = true;
		REG(0x1f801614) = 0x80000000U;
		aprint_error("ps2sif: USB IRQ mailbox corrupt; polling fallback\n");
	} else {
		uint32_t seq = usb_mailbox->sequence;
		if (seq != usb_mailbox->acknowledged) {
			/* IOP masks MIE before publishing seq and never rearms it.
			 * Clear only our flag before rearm, preserving later events. */
			if (REG(SIF_SMFLAG) & 0x400) {
				REG(SIF_SMFLAG) = 0x400;
				usb_recovered++;
			}
			(void)usb_handler(usb_context);
			usb_mailbox->acknowledged = seq;
			barrier();
			if (!usb_stopped)
				REG(0x1f801610) = 0x80000000U;
			barrier();
		}
	}
	splx(s);
#endif
}

bool
ps2sif_usb_intr_active(void)
{
#ifdef PS2_IOP_BOOTSTRAP
	return usb_ready && !usb_stopped;
#else
	return false;
#endif
}

int
ps2sif_usb_intr_establish(int (*handler)(void *), void *context)
{
#ifdef PS2_IOP_BOOTSTRAP
	uint32_t address;
	struct ps2sif_module_result result;
	if (usb_mailbox != NULL || handler == NULL)
		return EBUSY;
	int error = ps2sif_alloc(sizeof(*usb_mailbox), &address);
	if (error)
		return error;
	usb_mailbox = (volatile struct ps2_usb_irq_mailbox *)iop_window(address);
	usb_mailbox->magic = usb_mailbox->sequence = 0;
	usb_mailbox->interrupts = usb_mailbox->acknowledged = 0;
	for (unsigned i = 0; i < 12; i++)
		usb_mailbox->guard[i] = PS2_USB_IRQ_GUARD;
	barrier();
	usb_handler = handler;
	usb_context = context;
	REG(SIF_SMFLAG) = 0x400;
	usb_ih = intc_intr_establish(I_CH1_SBUS, IPL_BIO, usb_relay_intr, NULL);
	char arg[9];
	snprintf(arg, sizeof(arg), "%08x", address);
	error = ps2sif_exec_irx(ps2_iop_usb_irq_image,
	    sizeof(ps2_iop_usb_irq_image), arg, sizeof(arg), &result);
	if (!error && (result.entry_result != 0 || !usb_mailbox_valid()))
		error = EPROTO;
	if (error) {
		printf("ps2sif: USB relay load: error=%d id=%d entry=%d start=%d magic=%#x\n",
		    error, result.module_id, result.entry_result, result.start_result,
		    usb_mailbox->magic);
		printf("ps2sif: USB mailbox %#x seq=%#x IRQ=%#x ack=%#x guards=%#x/%#x\n",
		    address, usb_mailbox->sequence, usb_mailbox->interrupts,
		    usb_mailbox->acknowledged, usb_mailbox->guard[0], usb_mailbox->guard[11]);
		ps2sif_usb_intr_stop();
		return error;
	}
	usb_ready = true;
	/* Startup notification may arrive while the synchronous RPC is active.
	 * The tick poll also recovers a lost edge using the shared sequence. */
	return 0;
#else
	(void)handler;
	(void)context;
	return ENOTSUP;
#endif
}

void
ps2sif_usb_intr_stop(void)
{
#ifdef PS2_IOP_BOOTSTRAP
	if (usb_mailbox == NULL)
		return;
	int s = splbio();
	usb_stopped = true;
	REG(0x1f801614) = 0x80000000U;
	barrier();
	REG(SIF_SMFLAG) = 0x400;
	if (usb_ih != NULL) {
		intc_intr_disestablish(usb_ih);
		usb_ih = NULL;
	}
	printf("ps2sif: USB IRQ stop: IOP=%u SBUS=%u recovered=%u seq=%u ack=%u\n",
	    usb_mailbox->interrupts, usb_sbus_count, usb_recovered,
	    usb_mailbox->sequence, usb_mailbox->acknowledged);
	splx(s);
#endif
}

static int
ps2sif_match(device_t parent, cfdata_t cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	return ma->ma_name != NULL && strcmp(ma->ma_name, "ps2sif") == 0;
}

static void
ps2sif_attach(device_t parent, device_t self, void *aux)
{
	uint32_t address = 0, freed = 0, expected[16], observed[16];
	int error;
	const char *stage = "CMD/RPC init";
	aprint_normal(": native SIF bootstrap (no SBIOS/RTE)\n");
	mutex_init(&rpc_lock, MUTEX_DEFAULT, IPL_NONE);
	error = native_init();
	if (error)
		goto done;
	stage = "heap bind";
	error = bind_service(HEAP_SERVICE);
	if (error)
		goto done;
	stage = "heap allocate";
	error = heap_call(1, 4096, &address);
	if (error)
		goto done;
	if (address == 0 || address >= IO_LIMIT - 4096) {
		error = EPROTO;
		goto done;
	}
	for (unsigned i = 0; i < 16; i++)
		expected[i] = 0x4e425344U ^ (i * 0x01010101U);
	stage = "EE-to-IOP DMA";
	error = send_to_iop(address, expected, sizeof(expected), false);
	if (error)
		goto release;
	stage = "IOP-to-EE DMA";
	error = read_iop(address, observed, sizeof(observed));
	if (error)
		goto release;
	stage = "DMA data comparison";
	if (memcmp(expected, observed, sizeof(expected)))
		error = EIO;
	if (!error) {
		stage = "direct IOP RAM window";
		volatile uint32_t *window = (volatile uint32_t *)
		    MIPS_PHYS_TO_KSEG1(0x1c000000U + address);
		for (unsigned i = 0; i < 16; i++)
			if (window[i] != expected[i])
				error = EIO;
		window[0] = expected[0] = ~expected[0];
		barrier();
		if (!error)
			error = read_iop(address, observed, sizeof(observed));
		if (!error && memcmp(expected, observed, sizeof(expected)))
			error = EIO;
	}
release:
	{
		int release_error = heap_call(2, address, &freed);
		if (!error) {
			stage = "heap free";
			error = release_error ? release_error : (freed ? EIO : 0);
		}
	}
done:
	if (error) {
		aprint_error_dev(self, "%s failed: %d (SMFLAG=%#x SUB=%#x D5=%#x D6=%#x packets=%u)\n",
		    stage, error, REG(SIF_SMFLAG), REG(SIF_SUB), REG(D5), REG(D6), received);
		REG(D5) = 0;
		REG(D6) = 0;
	} else {
		aprint_normal_dev(self, "CMD/RPC, heap alloc/free, 64-byte DMA round-trip OK (IOP=%#x packets=%u)\n",
		    address, received);
		service_ready = true;
		uint32_t pool;
		const size_t poolsize = 512 * 1024;
		error = ps2sif_alloc(poolsize, &pool);
		if (error) {
			aprint_error_dev(self, "IOP DMA pool allocation failed: %d\n", error);
			return;
		}
		struct ps2sif_attach_args sa = {
		    .name = "ohci",
		    .dmat = ps2_iopdma_init(pool, poolsize)
		};
		if (sa.dmat == NULL) {
			(void)ps2sif_free(pool);
			aprint_error_dev(self, "IOP DMA arena creation failed\n");
			return;
		}
		aprint_normal_dev(self, "direct RAM window OK, runtime DMA pool %#x + %zu KiB\n",
		    pool, poolsize / 1024);
#ifdef PS2_IOP_BOOTSTRAP
		bootstrap_selftest(self);
#endif
		config_found(self, &sa, NULL, CFARGS_NONE);
		sa.name = "spd";
		config_found(self, &sa, NULL, CFARGS_NONE);
	}
}

CFATTACH_DECL_NEW(ps2sif, 0, ps2sif_match, ps2sif_attach, NULL, NULL);
