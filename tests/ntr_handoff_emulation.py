#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the built NTR ARM9/ARM7 handoff with modeled remapping and buses.

This verifies instruction flow and register intent, not physical hardware.
"""
from pathlib import Path
import os
import struct
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

ROOT = Path(__file__).resolve().parents[1]
PREFIX = Path(os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful')) / 'toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
syms = subprocess.check_output([str(PREFIX) + 'nm', str(ROOT/'build/loader/loader.elf')], text=True)
symbols = {l.split()[-1]: int(l.split()[0], 16) for l in syms.splitlines() if len(l.split()) == 3}
loader = (ROOT/'data/load.bin').read_bytes()
p32 = lambda n: struct.pack('<I', n)

def machine():
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for a, n in [(0x01000000,0x8000),(0x02000000,0x1000000),(0x037f8000,0x18000),
                 (0x03fff000,0x1000),(0x04000000,0x10000),(0x06000000,0x20000)]: u.mem_map(a,n)
    u.mem_write(0x06000000, loader)
    u.reg_write(UC_ARM_REG_SP,0x0601f000)
    return u

off = symbols['startBinaryNTR_ARM9'] - 0x06000000
stage = loader[off:off+0x200]
for clock in (0x80, 0x81):
    u = machine(); target = 0x02004000
    u.mem_write(0x02ffd000, stage)
    u.mem_write(0x02fffdfb,b'\x01')
    u.mem_write(0x023ffe24,p32(target))
    u.mem_write(0x04004004,struct.pack('<H',clock))
    state = {'ack':False,'remap':False}
    def wr(u, access, a, n, v, _):
        if a == 0x023ffdfb and v == 2:
            assert 0x01000000 <= u.reg_read(UC_ARM_REG_PC) < 0x01008000
            state['ack'] = True
        if a == 0x04004008:
            assert state['ack'] and v == 0x03000000
            assert 0x01000000 <= u.reg_read(UC_ARM_REG_PC) < 0x01008000
            state['remap'] = True
            state['resume'] = u.reg_read(UC_ARM_REG_PC) + 4
            u.emu_stop()
    def rd(u, access, a, n, v, _):
        if a == 0x023ffdfb and state['ack']: u.mem_write(a,b'\x03')
    u.hook_add(UC_HOOK_MEM_WRITE,wr); u.hook_add(UC_HOOK_MEM_READ,rd)
    u.emu_start(0x02ffd000,target,count=5000)
    assert state['remap']
    # Unmap outside the hook (Unicorn cannot safely unmap a live translation).
    u.mem_unmap(0x02400000,0xC00000)
    u.emu_start(state['resume'],target,count=5000)
    assert state['remap'] and u.reg_read(UC_ARM_REG_PC)==target
    assert int.from_bytes(u.mem_read(0x04004004,2),'little') == clock & ~1
    assert u.mem_read(0x04000208,4)==bytes(4)
print('PASS: ARM9 enters ITCM before handshake, changes to 67 MHz/4 MiB/NTR, survives removal of high RAM')

for target in (0x02380000,0x037f8000):
    u = machine()
    u.mem_write(symbols['forceNTR'],p32(1))
    u.mem_write(0x02fffe34,p32(target)); u.mem_write(0x02fffff4,p32(0x02008000))
    u.mem_write(0x02fffe70,b'_arg'+p32(0x02050000)+p32(48))
    u.mem_write(0x02fffc80,b'firmware-data')
    state = {'remap':False,'ack':False}
    def code(u,a,n,_):
        if a == (symbols['boot_readFirmware'] & ~1):
            u.mem_write(0x02fff05d,b'\x57')
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def rd(u,access,a,n,v,_):
        if a==0x023ffdfb:
            state['ack']=True; u.mem_write(a,b'\x02')
    def wr(u,access,a,n,v,_):
        if a==0x04004008:
            assert state['ack'] and v==0x12a03000
            assert u.mem_read(0x023ffe70,12)==b'_arg'+p32(0x02050000)+p32(48)
            assert u.mem_read(0x023ff05d,1)==b'\x57'
            assert u.mem_read(0x023ffc80,13)==b'firmware-data'
            state['remap']=True; state['resume']=u.reg_read(UC_ARM_REG_PC)+2
            u.emu_stop()
    u.hook_add(UC_HOOK_CODE,code); u.hook_add(UC_HOOK_MEM_READ,rd); u.hook_add(UC_HOOK_MEM_WRITE,wr)
    u.emu_start(symbols['startBinary_ARM7']|1,target,count=30000)
    assert state['remap']
    u.mem_unmap(0x02400000,0xc00000)
    u.emu_start(state['resume']|1,target,count=5000)
    assert state['remap'] and u.reg_read(UC_ARM_REG_PC)==target
    assert u.mem_read(0x023ffdfb,1)==b'\x03'
print('PASS: ARM7 mirrors firmware/header/argv, waits for ARM9 ITCM, remaps, jumps to either ARM7 layout')

# Execute actual compiled bus code with deterministic ready/ACK/readback values.
for stuck in (False,True):
    u=machine(); state={'command':None,'page':0,'codec':{},'scfg':[],'pm':0x0c}
    def rd(u,access,a,n,v,_):
        if a==0x040001c0:
            u.mem_write(a,struct.pack('<H',0x80 if stuck else 0))
        if a==0x04004501: u.mem_write(a,b'\x10')
        if a==0x040001c2:
            u.mem_write(a,struct.pack('<H',state['pm']))
    def wr(u,access,a,n,v,_):
        if a==0x040001c2:
            ctrl=int.from_bytes(u.mem_read(0x040001c0,2),'little')
            if ctrl&0x800: state['command']=v
            elif ((ctrl>>8)&3)==2:
                reg=state['command']>>1
                if not (state['command']&1):
                    if reg==0 or (state['page']==255 and reg==127): state['page']=v
                    else: state['codec'][state['page'],reg]=v
            elif ((ctrl>>8)&3)==0 and state['command']==0: state['pm']=v
        if a==0x04004000: state['scfg'].append(v)
    u.hook_add(UC_HOOK_MEM_READ,rd); u.hook_add(UC_HOOK_MEM_WRITE,wr)
    u.reg_write(UC_ARM_REG_LR,0x02001000)
    u.emu_start(symbols['shim_ntr_prepare']|1,0x02001000,count=2000000)
    assert u.reg_read(UC_ARM_REG_PC)==0x02001000
    if stuck:
        assert not u.reg_read(UC_ARM_REG_R0) and not state['scfg']
    else:
        assert u.reg_read(UC_ARM_REG_R0)==1
        assert state['codec'][255,5]==0 and state['codec'][3,2]==0x98
        assert state['codec'][0,6]==21 and state['codec'][0,11]==0x87
        assert state['pm']==0x0d and state['scfg']==[0x703]
        assert u.mem_read(0x04004700,2)==struct.pack('<H',0x8008)
print('PASS: full NTR codec sequence, I2S unmute, DS touch compatibility, NTR BIOS; stuck SPI exits boundedly')
print('LIMIT: modeled registers and instruction execution; real DSpico/DSi downgrade needs hardware testing.')
