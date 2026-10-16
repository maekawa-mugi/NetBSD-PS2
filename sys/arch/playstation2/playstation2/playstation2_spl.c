/* EE interrupt priorities: INTC/DMAC masks, with software pending flags.
 * EE Core has no Cause.IP[1:0] software interrupt inputs.
 */
#define __INTR_PRIVATE
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/intr.h>
#include <machine/intr.h>
#include <mips/locore.h>
#include <playstation2/playstation2/interrupt.h>
#include <playstation2/ee/eevar.h>
#include <playstation2/ee/intcvar.h>
#include <playstation2/ee/intcreg.h>
#include <playstation2/ee/dmacvar.h>
#include <playstation2/ee/dmacreg.h>

#define EE_EIE 0x00010000
static uint32_t soft_pending;
static bool soft_running;

static void
ps2_setipl(int ipl)
{
	int s = _intr_suspend();
	KASSERT(ipl >= IPL_NONE && ipl <= IPL_HIGH);
	curcpu()->ci_cpl = ipl;
	md_imask = __icu_mask[ipl];
	md_imask_update();
	/* Device masks implement the IPL; only INT0 and INT1 are CPU inputs. */
	uint32_t sr = mips_cp0_status_read();
	mips_cp0_status_write((sr & ~MIPS_INT_MASK) |
	    MIPS_INT_MASK_0 | MIPS_INT_MASK_1);
	_intr_resume(s);
}

static int
ps2_splraise(int ipl)
{
	int old = curcpu()->ci_cpl;
	if (ipl > old)
		ps2_setipl(ipl);
	return old;
}

/* Called with the interrupted thread's IPL, after restoring ci_idepth.
 * Preserve the caller's current IPL (HIGH in the assembly return paths).
 */
void
playstation2_softintr_dispatch(int ppl)
{
	struct cpu_info *ci = curcpu();
	uint32_t mask = 0, sr = mips_cp0_status_read();
	if (soft_running || ci->ci_idepth != 0 || ci->ci_mtx_count != 0 ||
	    !(sr & MIPS_SR_INT_IE) || (sr & (MIPS_SR_EXL | MIPS_SR_ERL)))
		return;
	if (ppl < IPL_SOFTCLOCK)
		mask |= MIPS_SOFT_INT_MASK_0;
	if (ppl < IPL_SOFTSERIAL)
		mask |= MIPS_SOFT_INT_MASK_1;

	int old = ps2_splraise(IPL_HIGH);
	soft_running = true;
	while (soft_pending & mask)
		softint_process(soft_pending & mask);
	soft_running = false;
	ps2_setipl(old);
}

static void
ps2_splx(int ipl)
{
	ps2_setipl(ipl);
	playstation2_softintr_dispatch(ipl);
}

static void
ps2_spl0(void)
{
	ps2_setipl(IPL_NONE);
	mips_cp0_status_write(mips_cp0_status_read() | EE_EIE | MIPS_SR_INT_IE);
	playstation2_softintr_dispatch(IPL_NONE);
}

static void
ps2_setsoftintr(uint32_t bits)
{
	int s = _intr_suspend();
	soft_pending |= bits & MIPS_SOFT_INT_MASK;
	_intr_resume(s);
	playstation2_softintr_dispatch(curcpu()->ci_cpl);
}

static void
ps2_clrsoftintr(uint32_t bits)
{
	int s = _intr_suspend();
	soft_pending &= ~bits;
	_intr_resume(s);
}

static int
ps2_splintr(uint32_t *ipending)
{
	/* IPL_HIGH masks hardware delivery, but raw requests remain readable. */
	uint32_t pending = (_reg_read_4(I_STAT_REG) & __intc_enabled_channel) |
	    ((_reg_read_4(D_STAT_REG) & __dmac_enabled_channel) << 16);
	*ipending = 0;
	for (int ipl = IPL_HIGH; ipl > IPL_NONE; ipl--) {
		uint32_t active = pending & ~__icu_mask[ipl - 1];
		if (active == 0)
			continue;
		if (active & 0xffff)
			*ipending |= MIPS_INT_MASK_0;
		if (active & 0xffff0000)
			*ipending |= MIPS_INT_MASK_1;
		return ipl;
	}
	return IPL_NONE;
}

static void
ps2_splcheck(void)
{
	KASSERT(curcpu()->ci_cpl >= IPL_NONE && curcpu()->ci_cpl <= IPL_HIGH);
	KASSERT(md_imask == __icu_mask[curcpu()->ci_cpl]);
}

#define PS2_SPL(name, level) \
static int name(void) { return ps2_splraise(level); }
PS2_SPL(ps2_splhigh, IPL_HIGH)
PS2_SPL(ps2_splsched, IPL_SCHED)
PS2_SPL(ps2_splvm, IPL_VM)
PS2_SPL(ps2_splsoftserial, IPL_SOFTSERIAL)
PS2_SPL(ps2_splsoftnet, IPL_SOFTNET)
PS2_SPL(ps2_splsoftbio, IPL_SOFTBIO)
PS2_SPL(ps2_splsoftclock, IPL_SOFTCLOCK)

const struct splsw playstation2_splsw = {
	.splsw_splhigh = ps2_splhigh,
	.splsw_splsched = ps2_splsched,
	.splsw_splvm = ps2_splvm,
	.splsw_splsoftserial = ps2_splsoftserial,
	.splsw_splsoftnet = ps2_splsoftnet,
	.splsw_splsoftbio = ps2_splsoftbio,
	.splsw_splsoftclock = ps2_splsoftclock,
	.splsw_splraise = ps2_splraise,
	.splsw_spl0 = ps2_spl0,
	.splsw_splx = ps2_splx,
	.splsw_splhigh_noprof = ps2_splhigh,
	.splsw_splx_noprof = ps2_splx,
	.splsw__setsoftintr = ps2_setsoftintr,
	.splsw__clrsoftintr = ps2_clrsoftintr,
	.splsw_splintr = ps2_splintr,
	.splsw_splcheck = ps2_splcheck,
};
