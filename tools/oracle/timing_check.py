"""Table/phrase timing regression against the ROM: vibrato, bend and slide started at different ticks."""
import tabtest as T, compare
V,A,P,L=17,1,13,10
tot=0
def go(label,s,n=30,fld='freq',tol=2):
    global tot
    r=T.show(s,n,fld); me=compare.mine_states(s.sav(),n+30); m=[me[(k,0)][fld] for k in range(n)]
    bad=sum(1 for a,b in zip(r,m) if abs(a-b)>tol); tot+=bad
    if bad: print(label,'bad',bad,'\n ROM',r[:16],'\n ME ',m[:16])
def mk(tables, rec=[0,0xF0,0,0,0x7F,1,0x20,3], notes=((0,25),)):
    s=T.tsong([], rec=rec, notes=notes)
    for t,rows in tables.items():
        for i,(e,tr,c1,v1,c2,v2) in enumerate(rows):
            s.m[0x1690+t*16+i]=e; s.m[0x3480+t*16+i]=tr; s.m[0x3680+t*16+i]=c1; s.m[0x3880+t*16+i]=v1; s.m[0x3A80+t*16+i]=c2; s.m[0x3C80+t*16+i]=v2
    return s
N=(0,0,0,0,0,0)
for d in range(0,4):
    go('V F4 at row %d'%d, mk({0:[N]*d+[(0,0,V,0xF4,0,0)]}))
    go('V 83 at row %d'%d, mk({0:[N]*d+[(0,0,V,0x83,0,0)]}))
    go('P30 at row %d'%d, mk({0:[N]*d+[(0,0,P,0x30,0,0)]}))
    go('P30 row %d then L08 row %d'%(d,d+3), mk({0:[N]*d+[(0,0,P,0x30,0,0),N,N,(0,0,L,8,0,0)]}))
go('A->V', mk({0:[(0,0,0,0,A,1)],1:[(0,0,V,0xF4,0,0)]}))
print('total bad',tot)
