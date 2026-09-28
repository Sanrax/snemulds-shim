#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual patched ARM instructions; deterministic codec registers/conversions.

No hardware claim: analog conversion, calibration storage, and SPI timing are
inputs to this model. Unlike the older IRQ test, touchReadXY and cdcTouchRead
execute here, including invalid readings, calibration, and frame publication.
"""
from pathlib import Path
import ctypes, hashlib, struct, subprocess, sys, tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
ROOT=Path(__file__).resolve().parents[1]
raw=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(raw).hexdigest()=='f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2'
original=raw[0x5fe00:0x5fe00+0xf784]
with tempfile.TemporaryDirectory() as tmp:
    so=Path(tmp)/'patch.so'
    subprocess.run(['cc','-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'source/tgds_io.c'),'-o',str(so)],check=True)
    lib=ctypes.CDLL(str(so)); fn=lib.shim_tgds_twl_touch
    fn.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_bool]; fn.restype=ctypes.c_bool
    buf=ctypes.create_string_buffer(original,len(original)); assert fn(buf,len(original),True)
    code=buf.raw

RETURN=0x02001000
def put16(u,a,v):u.mem_write(a,struct.pack('<H',v&65535))
def put32(u,a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
def get16(u,a):return int.from_bytes(u.mem_read(a,2),'little')
def get32(u,a):return int.from_bytes(u.mem_read(a,4),'little')
def call(u,entry,*args):
    u.reg_write(UC_ARM_REG_SP,0x0380fc00);u.reg_write(UC_ARM_REG_LR,RETURN)
    for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):u.reg_write(r,v)
    u.emu_start(entry,RETURN,count=50000)
    assert u.reg_read(UC_ARM_REG_PC)==RETURN,'Instruction budget exceeded'

def machine(image=code,cal=True):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for a,s in [(0x02000000,0x1000000),(0x03800000,0x10000),(0x04000000,0x10000),(0x06000000,0x20000)]:u.mem_map(a,s)
    u.mem_write(0x03800000,image);u.mem_write(0x0380e790,b'\x01')
    put16(u,0x04000130,0x3ff);put16(u,0x04000136,3);put32(u,0x04000208,1)
    if cal: # Known two-point firmware data at TGDS PersonalData+0x58.
        u.mem_write(0x02fff628,struct.pack('<HHBBHHBB',400,600,20,30,3600,3400,236,162))
    state={'bank':0,'regs':{(3,2):0xf8,(3,4):0x80,(3,0x12):0xe7},'down':False,
           'x':2000,'y':2000,'invalid':False,'diags':0,'writes':[],'arrays':0}
    def regread(bank,reg):
        if (bank,reg)==(3,9):return 0xc0 if state['down'] else 0x40
        if (bank,reg)==(3,0xe):return (state['regs'].get((bank,reg),0)&~2)|(0 if state['down'] else 2)
        return state['regs'].get((bank,reg),0)
    def hook(u,a,size,user):
        r=[u.reg_read(x) for x in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]
        if a==0x03809e08: state['bank']=r[0] # codec bank select
        elif a==0x03809d9c:u.reg_write(UC_ARM_REG_R0,regread(state['bank'],r[0]))
        elif a==0x03809e7c:u.reg_write(UC_ARM_REG_R0,regread(r[0],r[1]))
        elif a in [0x03809f68,0x03809fc8]:
            bank,reg=r[:2]
            value=r[2] if a==0x03809f68 else (regread(bank,reg)&~r[2])|(r[3]&r[2])
            state['regs'][bank,reg]=value&255;state['writes'].append((bank,reg,value&255))
        elif a==0x03809e94:
            assert r[0:2]==[0xfc,1] and r[3]==20
            active=not(state['regs'].get((3,2),0)&0x80 or state['regs'].get((3,4),0)&0x80 or state['regs'].get((3,0x12),0)&0xe0)
            x=state['x'] if active else 0; y=state['y'] if active else 0
            vals=[x]*5+[y]*5
            if state['invalid']: vals[0]|=0x8000
            u.mem_write(r[2],struct.pack('>10H',*vals));state['arrays']+=1
        elif a==0x0380b178: # actual general exception routine has sent FIFO; stop its deliberate wait
            state['diags']+=1;u.reg_write(UC_ARM_REG_PC,RETURN);return
        else:return
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    u.hook_add(UC_HOOK_CODE,hook)
    call(u,0x0380914c) # actual mode command and eager init
    return u,state

# The old init leaves stopped conversions; the patched init clears exactly
# documented stop/disable/hold controls, while keeping filter bits intact.
u,old=machine(original);call(u,0x0380a0ec)
assert old['regs'][3,2]&0x80 and old['regs'][3,4]&0x80 and old['regs'][3,0x12]&0xe0
u,s=machine()
assert s['regs'][3,2]==0x18 and s['regs'][3,4]==0x46 and s['regs'][3,0x12]==0
assert all(bank==3 for bank,_,_ in s['writes']), 'Touch init must not alter audio banks'
print('PASS: stock preserves stop/disable/hold; actual patched init clears them')

def frame(u):
    call(u,0x0380920c,0)
    before=get16(u,0x02fff240)
    call(u,0x0380dcb0)
    assert get16(u,0x02fff240)==before
    return struct.unpack('<6H',u.mem_read(0x02fff248,12))

for down in [False,True,True,False,True,False]:
    s['down']=down; coords=frame(u)
    assert bool(get16(u,0x02fff240)&0x40)==(not down)
    if down: assert coords==(2000,2000,128,96,0,0)
# Invalid conversion must not create a press at (0,0) or overwrite last position.
s['down']=True;s['invalid']=True
assert frame(u)==coords and get16(u,0x02fff240)&0x40
s['invalid']=False;s['x']=1;s['y']=4095
assert frame(u)[2:4]==(0,191)
s['x']=4095;s['y']=1
assert frame(u)[2:4]==(255,1) # This firmware calibration maps minimum raw Y to pixel 1.
assert get16(u,0x04000208)==1
print('PASS: real codec read, calibrated first press, hold, release, re-press, invalid sample and clipping')
u,s=machine(cal=False);s['down']=True;s['x']=2048;s['y']=2048
assert frame(u)[2:4]==(128,96)
print('PASS: missing firmware calibration uses bounded approximate coordinates, without divide-by-zero')

# The diagnostic path is removed. The old combo must act as ordinary input
# without touching PCM, audio controls, FIFO commands or the exception buffer.
assert code[0x6c70:0x6c84] == original[0x6c70:0x6c84]
u,s=machine()
put32(u,0x0380ffdc,0x12345678)
call(u,0x0380914c)
assert get32(u,0x0380ffdc)==0 # same mode-entry behavior as hardware-tested v0.9
put32(u,0x02ffff18,0x02100000);put32(u,0x02ffff34,0xffff022b)
call(u,0x03806c70) # unchanged original handler-disable routine
u.mem_write(0x02100000,b'Q'*128)
for down in [False,True,False]:
    s['down']=down
    put16(u,0x04000130,0x3ff&~0x304)
    coords=frame(u)
    assert get16(u,0x02fff242)==0x3ff&~0x304
    assert bool(get16(u,0x02fff240)&0x40)==(not down)
    assert s['diags']==0 and u.mem_read(0x02100000,128)==b'Q'*128
    assert get32(u,0x02ffff18)==0x02100000 and get32(u,0x02ffff34)==0xffff022b
print('PASS: L+R+SELECT passes through; no halt, exception-buffer writes or diagnostic FIFO message')
if len(sys.argv)>2:
    import json,zipfile
    with zipfile.ZipFile(sys.argv[2]) as z:
        patch=json.loads(z.read('snemulds-argv-shim-v0.9/tools/twl-input-v09.json'))
    assert hashlib.sha256(raw).hexdigest()==patch['input_sha256']
    baseline=bytearray(raw)
    for part in patch['patches']:
        a=part['offset'];data=bytes.fromhex(part['data']);baseline[a:a+len(data)]=data
    assert hashlib.sha256(baseline).hexdigest()==patch['output_sha256']
    old,os=machine(bytes(baseline[0x5fe00:0x5fe00+0xf784]))
    new,ns=machine()
    assert os['writes']==ns['writes']
    for down,x,y,invalid in [(False,2000,2000,False),(True,2000,2000,False),
                              (True,1200,2500,False),(True,1200,2500,True),
                              (False,1200,2500,False),(True,1,4095,False)]:
        for u,s in [(old,os),(new,ns)]:
            s.update(down=down,x=x,y=y,invalid=invalid)
            frame(u)
        assert old.mem_read(0x02fff240,20)==new.mem_read(0x02fff240,20)
        assert os['regs']==ns['regs'] and os['arrays']==ns['arrays']
        assert old.mem_read(0x04000000,0x10000)==new.mem_read(0x04000000,0x10000)
    print('PASS: normal input, codec writes and modeled hardware state match delivered v0.9')
print('LIMIT: register/instruction model, not physical touch, audible output, cache behavior or frame timing.')
