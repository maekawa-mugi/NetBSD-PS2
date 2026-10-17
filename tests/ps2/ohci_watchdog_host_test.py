#!/usr/bin/env python3
"""Test production watchdog pacing, fallback and shutdown without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'sys/arch/playstation2/dev/ohci_ps2native.c').read_text()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct { unsigned due; } callout_t;
struct ohci_softc { bool sc_dying; };
typedef void *device_t;
static int hz=100, depth, intr_work, stopped, shutdowns;
static bool active, corrupt;
static int splbio(void) { return depth++; }
static void splx(int s) { assert(depth==s+1); depth=s; }
static int ohci_intr(void *p) { (void)p; return intr_work; }
static bool ps2sif_usb_intr_active(void) { return active; }
static void ps2sif_usb_intr_stop(void) { active=false; stopped++; }
static void callout_schedule(callout_t *c,unsigned n) { c->due=n; }
static void callout_stop(callout_t *c) { c->due=0; }
static void *device_private(device_t p) { return p; }
#define aprint_normal_dev(d,...) ((void)(d),printf(__VA_ARGS__))
static bool ohci_shutdown(device_t d,int flags) {
    (void)d; (void)flags; assert(stopped); shutdowns++; return true;
}
static void ps2sif_usb_intr_poll(void);
'''
code += source[source.index('struct native_ohci {'):source.index('static int\nnative_match')]
code += r'''
static void ps2sif_usb_intr_poll(void) {
    assert(depth && !stopped);
    if(corrupt) active=false;
}
int main(void) {
    struct native_ohci sc={0};
    active=true; native_poll(&sc);
    assert(sc.poll.due==100 && sc.polls==1 && sc.poll_work==0 && !depth);
    intr_work=1; native_interrupt(&sc);
    assert(sc.irq_work==1 && sc.poll_work==0);
    native_poll(&sc);
    assert(sc.polls==2 && sc.poll_work==1 && sc.irq_work==1 && sc.poll.due==100);
    corrupt=true; native_poll(&sc);
    assert(sc.poll.due==1 && !active); /* fallback after relay health failure */
    assert(native_shutdown(&sc,0) && shutdowns==1 && sc.stopping);
    unsigned calls=sc.polls, work=sc.poll_work;
    native_poll(&sc);
    assert(sc.poll.due==0 && sc.polls==calls && sc.poll_work==work);
    assert(!native_interrupt(&sc) && sc.irq_work==1);
    struct native_ohci dying={.ohci.sc_dying=true};
    native_poll(&dying); assert(dying.polls==0 && dying.poll.due==0);
    puts("OHCI 1 Hz watchdog/tick fallback/IRQ attribution/shutdown checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='ps2-ohci-watchdog-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
