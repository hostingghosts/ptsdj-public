import glob, lsdsng, lsdjsav as J, compare, replay, sys
c=bytearray(open('../../reference/oracle/conv/TRIAC.conv','rb').read())
for r in range(256):
    for ch in (1,2,3): c[0x1290+r*4+ch]=0xFF
def edit(fn):
    d=bytearray(c); fn(d); return bytes(d)
def run(d, ticks=90, show=(58,70)):
    sav=d+J.BASE[0x8000:]
    rom=replay.rom_states(sav,167,ticks,frames=int(ticks/(167*0.4)*60)+150)
    me=compare.mine_states(sav,ticks+40)
    return [(k,rom[(k,0)]['freq'],me[(k,0)]['freq']) for k in range(*show)]
if __name__=='__main__':
    print(run(bytes(c)))
