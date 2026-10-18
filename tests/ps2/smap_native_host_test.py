#!/usr/bin/env python3
"""Run production SMAP TX reclamation and native SPD polling with mocked I/O."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
smap = (root / 'sys/arch/playstation2/dev/if_smap.c').read_text()
spd = (root / 'sys/arch/playstation2/dev/spd.c').read_text()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t u_int16_t;
#define MIPS_PHYS_TO_KSEG1(a) (a)
#include "if_smapreg.h"
#define FUNC_ENTER() ((void)0)
#define FUNC_EXIT() ((void)0)
#define ROUND4(x) (((x)+3)&~3)
#define if_statinc(p,n) ((p)->n++)
#define _wbflush() ((void)0)
struct ifnet { unsigned if_opackets,if_oerrors,if_collisions,if_timer; };
struct smap_softc {
    struct { struct ifnet ec_if; } ethercom;
    volatile struct smap_desc *tx_desc;
    int tx_done_index,tx_start_index,tx_desc_cnt,tx_buf_freesize;
};
'''
code += smap[smap.index('void\nsmap_txeof('):smap.index('void\nsmap_start(')]
code += r'''
typedef struct { int due; } callout_t;
typedef void *device_t;
struct native_spd { callout_t poll; bool stopping; unsigned completions; };
static uint16_t status,enabled;
static unsigned callbacks;
static int depth;
#define SPD_INTR_STATUS_REG16 1
#define SPD_INTR_ENABLE_REG16 2
static uint16_t _reg_read_2(int reg) { return reg==1?status:enabled; }
static void _reg_write_2(int reg,uint16_t v) { assert(reg==2); enabled=v; }
static int splnet(void) { return depth++; }
static void splx(int s) { assert(depth==s+1); depth=s; }
static int spd_intr(void *p) { assert(!p && depth); callbacks++; return 1; }
static void callout_schedule(callout_t *p,int n) { p->due=n; }
static void callout_stop(callout_t *p) { p->due=0; }
static void *device_private(device_t d) { return d; }
#define aprint_normal_dev(d,...) ((void)d,printf(__VA_ARGS__))
'''
start = spd.index('static void\nspd_native_poll(')
code += spd[start:spd.index('#endif', start)]
code += r'''
int main(void) {
    struct smap_desc ring[64]={0};
    struct smap_softc sc={.tx_desc=ring,.tx_buf_freesize=4096};
    smap_txeof(&sc);
    assert(sc.tx_done_index==0 && sc.ethercom.ec_if.if_opackets==0);
    for(unsigned i=0;i<130;i++) {
        unsigned at=i%64;
        ring[at].sz=342; ring[at].stat=SMAP_TXDESC_READY;
        sc.tx_desc_cnt=1; sc.tx_buf_freesize-=344;
        sc.tx_start_index=(at+1)%64;
        smap_txeof(&sc); assert(sc.tx_desc_cnt==1 && sc.tx_done_index==(int)at);
        ring[at].stat=0; smap_txeof(&sc);
        assert(sc.tx_desc_cnt==0 && sc.tx_buf_freesize==4096);
        assert(sc.tx_done_index==(int)((at+1)%64));
        assert(sc.ethercom.ec_if.if_opackets==i+1 && ring[at].sz==0);
        smap_txeof(&sc); assert(sc.ethercom.ec_if.if_opackets==i+1);
    }
    struct native_spd poll={0};
    status=0x20; enabled=0; spd_native_poll(&poll); assert(!callbacks);
    enabled=0x20; spd_native_poll(&poll); assert(callbacks==1 && poll.completions==1);
    assert(poll.poll.due==1 && !depth);
    assert(spd_native_shutdown(&poll,0));
    spd_native_poll(&poll); assert(callbacks==1 && enabled==0 && poll.poll.due==0);
    puts("SMAP TX empty/ready/completion/130-entry wrap and SPD poll/stop checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='ps2-smap-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'sys/arch/playstation2/dev'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    result = subprocess.run([str(path / 'test')], check=True, capture_output=True, text=True)
    print(result.stdout.splitlines()[-1])
