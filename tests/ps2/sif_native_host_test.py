#!/usr/bin/env python3
"""Compile the actual native SIF packet encoder with a mocked DMA boundary."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "sys/arch/playstation2/ee/sif_native.c").read_text()
start = source.index("static int\nsend_to_iop(")
end = source.index("static int\nwait_reply(", start)
encoder = source[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define D6 0x1000c400
#define IO_LIMIT 0x00200000U
#define CMD_MAX 112
#define roundup(x,y) (((x)+(y)-1)/(y)*(y))
static uint32_t registers[16], storage[64];
static volatile uint32_t *tx = storage;
static uint32_t iop_command = 0x19600;
static bool timeout;
#define REG(a) registers[((a)-D6)/4]
static bool wait_clear(uint32_t r, uint32_t m) { (void)r; (void)m; return !timeout; }
static uint32_t physical(const volatile void *p) { (void)p; return 0x123000; }
static void barrier(void) {}
'''
harness += encoder
harness += r'''
int main(void) {
    uint32_t data[32];
    for (unsigned i=0;i<32;i++) data[i] = 0x5a000000+i;
    for (unsigned bytes=4;bytes<=128;bytes+=4) {
        memset(storage,0xa5,sizeof(storage));
        memset(registers,0,sizeof(registers));
        unsigned q=(bytes+15)/16;
        assert(send_to_iop(0x45000,data,bytes,true)==0);
        assert(storage[0]==(0x70000000U| (q+1)));
        assert(storage[1]==0 && storage[2]==0 && storage[3]==0);
        assert(storage[4]==0xc0045000 && storage[5]==q*4);
        assert(storage[6]==0 && storage[7]==0);
        assert(memcmp(&storage[8],data,bytes)==0);
        for(unsigned i=bytes/4;i<q*4;i++) assert(storage[8+i]==0);
        assert(REG(D6)==0x105 && REG(D6+0x20)==0 && REG(D6+0x30)==0x123000);
    }
    assert(send_to_iop(0x45000,data,64,false)==0);
    assert(storage[4]==0x45000);
    assert(send_to_iop(0,data,16,true)==EINVAL);
    assert(send_to_iop(1,data,16,true)==EINVAL);
    assert(send_to_iop(IO_LIMIT,data,16,true)==EINVAL);
    assert(send_to_iop(IO_LIMIT-4,data,4,true)==EINVAL); /* rounded DMA overrun */
    assert(send_to_iop(0x45000,data,0,true)==EINVAL);
    assert(send_to_iop(0x45000,data,15,true)==EINVAL);
    assert(send_to_iop(0x45000,data,132,true)==EINVAL);
    timeout=true;
    REG(D6)=0;
    assert(send_to_iop(0x45000,data,16,true)==ETIMEDOUT && REG(D6)==0);
    timeout=false;
    uint32_t p[32]={0};
    p[0]=4<<8; p[1]=0x45600;
    assert(send_command(p,64,0x8000000a)==0);
    assert(p[0]==0x440 && p[1]==0x45600 && p[2]==0x8000000a);
    assert(send_command(p,12,0)==EINVAL);
    assert(send_command(p,116,0)==EINVAL);
    assert(send_command(p,21,0)==EINVAL);
    puts("Native SIF DMA tags/padding/bounds/timeout/CMD header checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix="ps2-sif-test-") as tmp:
    c = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c.write_text(harness)
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
