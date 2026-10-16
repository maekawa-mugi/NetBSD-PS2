/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Native, bounded, polled EE SIF bootstrap transport.
 * Hardware DMA follows the EE User Manual's source/destination chains.
 * Wire words implement the resident IOP SIF CMD/RPC protocol. This file
 * does not include/link SDK code, SDK headers, IRX binaries or SBIOS calls.
 *
 * At this stage the boot ROM's IOP kernel/services remain running. This
 * is not yet an IOP reset, module loader, interrupt-driven RPC stack or
 * peripheral driver. A sleepable mutex serializes runtime heap RPC. Child
 * devices suballocate a reserved IOP DMA pool without issuing heap RPC.
 */
#include <sys/cdefs.h>
#include "opt_ps2_nosbios.h"
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
bind_heap(void)
{
	uint32_t p[16] = {0};
	rpc_header(p, RPC_BIND);
	p[8] = HEAP_SERVICE;
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
	error = bind_heap();
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
		    .dmat = ps2_iopdma_init(pool, poolsize)
		};
		if (sa.dmat == NULL) {
			(void)ps2sif_free(pool);
			aprint_error_dev(self, "IOP DMA arena creation failed\n");
			return;
		}
		aprint_normal_dev(self, "direct RAM window OK, runtime DMA pool %#x + %zu KiB\n",
		    pool, poolsize / 1024);
		config_found(self, &sa, NULL, CFARGS_NONE);
	}
}

CFATTACH_DECL_NEW(ps2sif, 0, ps2sif_match, ps2sif_attach, NULL, NULL);
