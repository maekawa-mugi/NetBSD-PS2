#!/usr/bin/env python3
"""Exercise the production PS2 SPL code against toggle/W1C register models."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "sys/arch/playstation2/playstation2/playstation2_spl.c").read_text()
source = re.sub(r"^#include .*\n", "", source, flags=re.M)
interrupt = (root / "sys/arch/playstation2/playstation2/interrupt.c").read_text()
mask_update = re.search(r"void\nmd_imask_update\(void\)\n\{.*?\n\}",
                        interrupt, re.S).group()
mask_update = mask_update.replace('__asm volatile("sync.p" ::: "memory");', '')
intr_header = (root / "sys/arch/mips/include/intr.h").read_text()
table = re.search(r"struct splsw \{.*?\n\};", intr_header, re.S).group()
levels = "\n".join(re.findall(r"^#define\s+(?:IPL_\w+|_IPL_N)\s+.*$",
                               intr_header, re.M))
model = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define KASSERT(x) assert(x)
#define MIPS_SR_INT_IE 1
#define MIPS_SR_EXL 2
#define MIPS_SR_ERL 4
#define MIPS_INT_MASK 0xff00
#define MIPS_INT_MASK_0 0x400
#define MIPS_INT_MASK_1 0x800
#define MIPS_SOFT_INT_MASK_0 0x100
#define MIPS_SOFT_INT_MASK_1 0x200
#define MIPS_SOFT_INT_MASK 0x300
#define I_STAT_REG 0
#define I_MASK_REG 1
#define D_STAT_REG 2
static struct cpu_info { int ci_cpl, ci_idepth, ci_mtx_count; } cpu;
#define curcpu() (&cpu)
static uint32_t regs[3], status;
uint32_t __icu_mask[_IPL_N], __intc_enabled_channel, __dmac_enabled_channel;
volatile uint32_t md_imask;
static uint32_t last_dma_write;
static uint32_t mips_cp0_status_read(void) { return status; }
static void mips_cp0_status_write(uint32_t sr) { status = sr; }
static int _intr_suspend(void) {
    int old = status; status &= ~0x10000U; return old;
}
static void _intr_resume(int old) { status |= old & 0x10000; }
static uint32_t _reg_read_4(int reg) { return regs[reg]; }
static void _reg_write_4(int reg, uint32_t value) {
    if (reg == I_MASK_REG) regs[reg] ^= value;
    else if (reg == D_STAT_REG) {
        last_dma_write = value;
        regs[reg] = (regs[reg] & ~(value & 0xffff)) ^ (value & 0xffff0000);
    } else regs[reg] &= ~value;
}
void md_imask_update(void);
void softint_process(uint32_t);
void playstation2_softintr_dispatch(int);
'''
checks = r'''
static unsigned soft_calls;
void softint_process(uint32_t bits) {
    assert(cpu.ci_cpl == IPL_HIGH && (status & MIPS_SR_INT_IE));
    assert(!cpu.ci_idepth && !cpu.ci_mtx_count);
    soft_calls++;
    ps2_clrsoftintr(bits);
}
int main(void) {
    status = 0x10001;
    cpu.ci_cpl = IPL_HIGH;
    __intc_enabled_channel = 1U << 9;
    __dmac_enabled_channel = 1U << 5;
    for (int i = 0; i < _IPL_N; i++) {
        __icu_mask[i] = UINT32_MAX;
        if (i < IPL_SCHED) __icu_mask[i] &= ~(1U << 9);
        if (i < IPL_VM) __icu_mask[i] &= ~(1U << 21);
    }
    /* Device registration while HIGH must not enable its interrupt yet. */
    regs[I_MASK_REG] = 1U << 9;
    regs[D_STAT_REG] = (1U << 21) | (1U << 5);
    ps2_setipl(IPL_HIGH);
    assert(regs[I_MASK_REG] == 0);
    assert(regs[D_STAT_REG] == (1U << 5));
    assert((last_dma_write & 0xffff) == 0); /* no W1C status corruption */

    /* Select the highest raw request even while hardware delivery is masked. */
    regs[I_STAT_REG] = 1U << 9;
    uint32_t ipending;
    assert(ps2_splintr(&ipending) == IPL_SCHED);
    assert(ipending == MIPS_INT_MASK_0);
    regs[I_STAT_REG] = 0;
    assert(ps2_splintr(&ipending) == IPL_VM);
    assert(ipending == MIPS_INT_MASK_1);
    ps2_setipl(IPL_VM);
    assert(regs[I_MASK_REG] == (1U << 9));
    assert(regs[D_STAT_REG] == (1U << 5));
    ps2_setipl(IPL_NONE);
    assert(regs[D_STAT_REG] == ((1U << 21) | (1U << 5)));

    /* Lower-priority software work stays pending; higher work may run. */
    ps2_setipl(IPL_HIGH);
    ps2_setsoftintr(MIPS_SOFT_INT_MASK);
    assert(soft_calls == 0);
    ps2_splx(IPL_SOFTCLOCK);
    assert(soft_calls == 1 && soft_pending == MIPS_SOFT_INT_MASK_0);
    assert(cpu.ci_cpl == IPL_SOFTCLOCK);
    ps2_splx(IPL_NONE);
    assert(soft_calls == 2 && soft_pending == 0 && cpu.ci_cpl == IPL_NONE);

    /* Hard interrupt/exception contexts must defer softint dispatch. */
    cpu.ci_idepth = 1;
    ps2_setsoftintr(MIPS_SOFT_INT_MASK_0);
    assert(soft_calls == 2);
    cpu.ci_idepth = 0;
    status |= MIPS_SR_EXL;
    ps2_splx(IPL_NONE);
    assert(soft_calls == 2);
    status &= ~MIPS_SR_EXL;
    playstation2_softintr_dispatch(IPL_NONE);
    assert(soft_calls == 3 && soft_pending == 0);
    puts("PS2 SPL priority/toggle/W1C/softint checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix="ps2-spl-") as tmp:
    path = Path(tmp)
    (path / "check.c").write_text("\n".join(
        [levels, model, table, mask_update, source, checks]))
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(path / "check.c"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
