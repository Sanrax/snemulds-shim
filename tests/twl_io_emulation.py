#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual shipped ARM code and a real DSpico DLDI in Unicorn.

The card registers are a deterministic model, not physical DSpico hardware.
Usage: python3 tests/twl_io_emulation.py stock.srl DSpico.dldi
Requires: a host C compiler and the Python unicorn package.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R4,
    UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parents[1]
RETURN = 0x02ff0000
DONOR = 0x02001800
def u32(b, off=0): return struct.unpack_from('<I', b, off)[0]
def p32(n): return struct.pack('<I', n & 0xffffffff)
def read32(uc, addr): return u32(uc.mem_read(addr, 4))

raw = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2'
driver = Path(sys.argv[2]).read_bytes()
assert driver[0x60:0x64] == b'PICO' and driver[14] == 0, 'Expected PICO position-independent driver'
arm9 = raw[0x4000:0x4000+0x5bccc]
arm7 = bytearray(raw[0x5fe00:0x5fe00+0xf784])
arm7[0x6514:0x6518] = p32(0xea00000b)

with tempfile.TemporaryDirectory() as tmp:
    so = Path(tmp)/'patch.so'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',
                    '-I'+str(ROOT/'include'),str(ROOT/'source/tgds_io.c'),'-o',str(so)],check=True)
    lib = ctypes.CDLL(str(so))
    patch = lib.shim_tgds_dspico_io
    patch.argtypes = [ctypes.c_void_p,ctypes.c_size_t,ctypes.c_bool]
    patch.restype = ctypes.c_bool
    data = ctypes.create_string_buffer(arm9,len(arm9))
    assert patch(data,len(arm9),False) and data.raw == arm9
    assert not patch(data,len(arm9)-1,True) and data.raw == arm9
    for off in [0x518cc,0x51a00,0x51a60,0x51be0,0x51bf0,0x232d0,0x1841]:
        bad = bytearray(arm9); bad[off] ^= 1
        buf = ctypes.create_string_buffer(bytes(bad),len(bad))
        assert not patch(buf,len(bad),True) and buf.raw == bad
    assert patch(data,len(arm9),True)
    patched = data.raw
    assert not patch(data,len(arm9),True) and data.raw == patched
    allowed = set(range(0x518cc,0x518cc+84)) | set(range(0x51a60,0x51a60+84)) | set(range(0x232d0,0x232d4))
    assert all(i in allowed or a == b for i,(a,b) in enumerate(zip(arm9,patched)))
    touch_patch = lib.shim_tgds_twl_touch
    touch_patch.argtypes = patch.argtypes
    touch_patch.restype = ctypes.c_bool
    original_touch = bytes(arm7)
    buf = ctypes.create_string_buffer(original_touch,len(arm7))
    assert touch_patch(buf,len(arm7),False) and buf.raw == original_touch
    for off in [0x6c70,0x8aac,0x9100,0x920c,0x9408,0x914c,0x919c,0xa0ec,0xa1c4,0xdc28,0xdc8c,0xdcb4,0xdd08]:
        bad = bytearray(original_touch); bad[off] ^= 1
        test = ctypes.create_string_buffer(bytes(bad),len(bad))
        assert not touch_patch(test,len(bad),True) and test.raw == bad
    assert not touch_patch(buf,len(arm7)-1,True) and buf.raw == original_touch
    assert touch_patch(buf,len(arm7),True)
    fixed_touch = buf.raw
    changed = {i for i,(a,b) in enumerate(zip(original_touch,fixed_touch)) if a!=b}
    allowed_touch = set(range(0x919c,0x91a0)) | set(range(0xdc8c,0xdc90)) | set(range(0xdcb4,0xdcb8))
    allowed_touch |= set(range(0x8aac,0x911c)) | set(range(0x920c,0x940c)) | set(range(0x6c70,0x6c74))
    for off in [0xa104,0xa10c,0xa17c,0xa1a4]: allowed_touch |= set(range(off,off+4))
    assert changed <= allowed_touch
    assert not touch_patch(buf,len(arm7),True) and buf.raw == fixed_touch

def machine(driver_base=DONOR):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for addr,size in [(0x02000000,0x1000000),(0x03800000,0x10000),
                      (0x04000000,0x10000),(0x04100000,0x1000),(0x06000000,0x20000)]:
        uc.mem_map(addr,size)
    uc.mem_write(0x02000000,patched)
    uc.mem_write(0x03800000,bytes(arm7))
    d = bytearray(driver)
    delta = driver_base-u32(d,0x40)
    for i in [*range(0x40,0x60,4),*range(0x68,0x80,4)]: d[i:i+4] = p32(u32(d,i)+delta)
    d[15] = 14
    uc.mem_write(DONOR,bytes(d))
    return uc

def call(uc,entry,args):
    uc.reg_write(UC_ARM_REG_SP,0x0380fc00)
    for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2],args): uc.reg_write(reg,value)
    uc.reg_write(UC_ARM_REG_LR,RETURN)
    uc.emu_start(entry,RETURN,count=3000000)
    assert uc.reg_read(UC_ARM_REG_PC)==RETURN, 'Instruction budget exceeded'
    return uc.reg_read(UC_ARM_REG_R0)

# Run the real ARM7InitDLDI + relocator + DSpico startup routines. Only unrelated
# libutils setup and payload-type detection are stubbed. Both prior approaches
# initialize successfully in instruction emulation: startup alone was not proof.
for base in [DONOR,0x06000000]:
    uc = machine(base)
    uc.mem_write(0x02ffdfe8,p32(DONOR))
    def setup_hook(uc,addr,size,user):
        if addr in [0x0380e3d0,0x038095dc]:
            uc.reg_write(UC_ARM_REG_R0,4)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,setup_hook)
    assert call(uc,0x038064c0,[0,0,0x06000000])==1
    assert read32(uc,0x02ffdfe8)==0xffffff02
print('PASS: actual TGDS ARM7 initialization with v0.3 and v0.4 donor addresses')

def sector_data(lba): return bytes((lba*19+i*7) & 255 for i in range(512))

class Card:
    """PICO E3/E4/E5/F6 protocol model; all writes stay in this Python object."""
    def __init__(self,uc,ime=1):
        self.sector=0; self.queue=[]; self.left=0; self.words=[]; self.disk={}; self.commands=[]
        self.ime=ime; self.irq_opportunities=0
        uc.hook_add(UC_HOOK_MEM_READ,self.read)
        uc.hook_add(UC_HOOK_MEM_WRITE,self.write)
    def ownership(self,uc):
        assert not (read32(uc,0x04000204)&0x800), 'ARM9 must own Slot-1'
        assert read32(uc,0x04000208)==self.ime, 'Transfer must preserve caller IRQ state'
    def read(self,uc,access,addr,size,value,user):
        if addr==0x040001a4:
            self.ownership(uc)
            self.irq_opportunities += bool(self.ime)
            uc.mem_write(addr,p32(0x80800000 if self.queue or self.left else 0))
        elif addr==0x04100010:
            self.ownership(uc)
            assert self.queue
            uc.mem_write(addr,p32(self.queue.pop(0)))
    def write(self,uc,access,addr,size,value,user):
        if addr==0x040001a4:
            self.ownership(uc)
            cmd=bytes(uc.mem_read(0x040001a8,8))
            self.commands.append(cmd[0])
            if cmd[0]==0xe3:
                self.sector=int.from_bytes(cmd[4:],'big')
            elif cmd[0]==0xe4:
                self.queue=[1]
            elif cmd[0]==0xe5:
                data=self.disk.get(self.sector,sector_data(self.sector))
                self.queue=list(struct.unpack('<128I',data)); self.sector+=1
            elif cmd[0]==0xf6:
                self.sector=int.from_bytes(cmd[4:],'big'); self.left=128; self.words=[]
            else: raise AssertionError(cmd.hex())
        elif addr==0x04100010:
            self.ownership(uc)
            assert self.left
            self.words.append(value); self.left-=1
            if not self.left: self.disk[self.sector]=struct.pack('<128I',*self.words)

saved_regs=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
            UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
cases=0
for count in [1,2,64]:
    for buffer in [0x02100000,0x02100001,0x02400000]:
        for ime in [0,1]:
            uc=machine(); card=Card(uc,ime)
            uc.mem_write(0x04000204,struct.pack('<H',0xE8C0))
            uc.mem_write(0x04000208,p32(ime))
            for reg in saved_regs: uc.reg_write(reg,0xabc00000+reg)
            assert call(uc,0x020518cc,[37,count,buffer])==1
            expected=b''.join(sector_data(37+i) for i in range(count))
            assert uc.mem_read(buffer,len(expected))==expected
            assert card.commands.count(0xe5)==count
            # Real writeSectors code, including unaligned transfers and flags.
            assert call(uc,0x02051a60,[91,count,buffer])==1
            assert b''.join(card.disk[91+i] for i in range(count))==expected
            assert card.commands.count(0xf6)==count
            assert bool(card.irq_opportunities)==bool(ime)
            assert uc.mem_read(0x04000204,2)==struct.pack('<H',0xE8C0)
            assert read32(uc,0x04000208)==ime
            for reg in saved_regs: assert uc.reg_read(reg)==0xabc00000+reg
            cases+=1
print(f'PASS: {cases} real DSpico read/write cases, aligned/unaligned and extended RAM')

# Driver-reported failures must be returned, while restoring caller state.
for entry,fn in [(0x020518cc,0x70),(0x02051a60,0x74)]:
    uc=machine(); uc.mem_write(0x02180000,p32(0xe3a00000)+p32(0xe12fff1e))
    uc.mem_write(DONOR+fn,p32(0x02180000))
    uc.mem_write(0x04000204,struct.pack('<H',0xE8C0)); uc.mem_write(0x04000208,p32(1))
    assert call(uc,entry,[0,1,0x02100000])==0
    assert uc.mem_read(0x04000204,2)==struct.pack('<H',0xE8C0) and read32(uc,0x04000208)==1
print('PASS: failure propagation, register preservation, and patch rejection without partial edits')

# Execute the stock FS_init control flow with supplied f_mount results, proving
# diagnostics preserve success and encode the actual failure without masking it.
for result in [0,1,3,13]:
    uc=machine(); uc.mem_write(0x027ff05d,b'\x57'); errors=[]
    def fs_hook(uc,addr,size,user):
        if addr==0x02023204:  # initTGDS, unrelated device-table setup
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        elif addr==0x02021800:  # f_mount outcome supplied by the test
            uc.reg_write(UC_ARM_REG_R0,result)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        elif addr==0x02026c20:  # capture handleDSInitError arguments
            errors.append((uc.reg_read(UC_ARM_REG_R0),uc.reg_read(UC_ARM_REG_R1)))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,fs_hook)
    assert call(uc,0x020232a8,[0,0,0])==result
    assert errors==([] if result==0 else [(256+result,0x57)])
print('PASS: actual FS_init success path and diagnostic stages 257/259/269')

# Run the real IRQ input pipeline: TGDS task followed by SnemulDS VcounterUser.
# Only codec measurements/mode detection are supplied; the stock key processing,
# ordering, timeout and patched branch instructions execute unchanged.
def touch_machine(code):
    uc=machine(); uc.mem_write(0x03800000,code)
    uc.mem_write(0x04000130,struct.pack('<H',0x3ff))
    uc.mem_write(0x04000136,struct.pack('<H',3)) # DSi legacy pen bit always 0
    uc.mem_write(u32(code,0x91a4),b'\x01\x01') # useTWLTSC, sampling enabled
    state={'down':False,'inits':0,'sleep':[]}
    def hooks(uc,addr,size,user):
        if addr==0x03808a5c: # touchPenDown: supplied physical pen state
            uc.reg_write(UC_ARM_REG_R0,int(state['down']))
        elif addr==0x03808aac: # touchReadXY: supplied calibrated measurement
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0),struct.pack('<6H',1500,2300,96,120,0,0))
        elif addr==0x0380a0ec: # observe eager scanner initialization
            state['inits']+=1
        elif addr in [0x038098a4,0x03807f4c]: # observe existing lid handling
            state['sleep'].append((addr,uc.reg_read(UC_ARM_REG_R0)))
        else: return
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,hooks)
    return uc,state

# Stock overwrites a pen-UP result with EXTKEYIN's permanently low DSi bit.
for code,is_fixed in [(original_touch,False),(fixed_touch,True)]:
    uc,state=touch_machine(code)
    call(uc,0x0380914c,[])
    assert state['inits']==int(is_fixed)
    for down in [False,True,True,False,True,False]:
        state['down']=down
        call(uc,0x0380920c,[0])
        before=u32(uc.mem_read(0x02fff240,2)+b'\0\0')
        call(uc,0x0380dcb0,[])
        after=u32(uc.mem_read(0x02fff240,2)+b'\0\0')
        if is_fixed:
            assert before==after
            assert not bool(after&0x40)==down
        elif not down:
            assert before&0x40 and not (after&0x40), 'Reproduce stock overwrite'
    # Simulate the old inactivity timeout with audio active and TSC active.
    uc.mem_write(u32(code,0xdca4),b'\0') # SPC_disable
    uc.mem_write(u32(code,0xdca8),p32(600)) # touchscreenTimeoutCounter
    uc.mem_write(u32(code,0xdcac),b'\x01') # TSCKeyActive
    # Clear the shared input and key edge state by sampling release.
    state['down']=False; call(uc,0x0380920c,[0])
    call(uc,0x0380dc28,[])
    assert bool(uc.mem_read(u32(code,0x941c)+1,1)[0])==is_fixed
    # The original lid handling still dims the screen and silences the mixer.
    uc.mem_write(0x02fff240,struct.pack('<H',0xc3))
    uc.mem_write(u32(code,0xdcac),b'\x01')
    call(uc,0x0380dcb0,[])
    assert state['sleep']==[(0x038098a4,0),(0x03807f4c,0)]
print('PASS: reproduced stock pen overwrite; fixed press/release, eager init, timeout and lid handling')

# Execute the exact compiled handoff function from the loader, not a C model.
prefix='/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
symbols=subprocess.check_output([prefix+'nm',str(ROOT/'build/loader/loader.elf')],text=True)
entry=int(next(line.split()[0] for line in symbols.splitlines() if line.endswith(' shim_twl_audio_prepare')),16)|1
loader=(ROOT/'data/load.bin').read_bytes()
for initial in [0,0x4000,0xc000,0x8000,0x8008,0xa008,0x600f,0xffff]:
    uc=machine(); uc.mem_write(0x06000000,loader)
    uc.mem_write(0x04004700,struct.pack('<H',initial)); writes=[]
    def watch(uc,access,addr,size,value,user):
        if 0x04000000 <= addr < 0x05000000: writes.append((addr,size,value))
    uc.hook_add(UC_HOOK_MEM_WRITE,watch)
    call(uc,entry,[])
    out=int.from_bytes(uc.mem_read(0x04004700,2),'little')
    assert out&0xc00f==0x8008 and (out&0x3ff0)==(initial&0x3ff0)
    assert writes==[(0x04004700,2,out)]
print('PASS: compiled TWL audio handoff enables/unmutes output and preserves frequency in 8 inherited states')
print('LIMIT: this models card registers; it does not emulate physical DSi timing, MPU, caches, or the complete game.')
