"""bigdiff.py DEMO [minfreq]: summary of large ROM-vs-player mismatches (freq diff > minfreq, any vol/duty/en) by channel and field, and the first few."""
import sys, glob, os, collections
import lsdsng, lsdjsav as J, compare
n=sys.argv[1]; mf=int(sys.argv[2]) if len(sys.argv)>2 else 40
f=glob.glob('../../demos/%s*.lsdsng'%n)[0]; m=lsdsng.decompress(open(f,'rb').read()); sav=m+J.BASE[0x8000:]; tempo=m[0x3FB4]
cf='../../reference/oracle/conv/%s.conv'%os.path.basename(f)[:-7]
romsav=(open(cf,'rb').read()+J.BASE[0x8000:]) if m[0x7FFF]!=0x16 and os.path.exists(cf) else None
bad=[b for b in compare.compare(sav,tempo,200,romsav=romsav) if b[2]!='pan' and not (b[2]=='freq' and abs(b[3]-b[4])<=mf) and not (b[2]=='vol' and abs(b[3]-b[4])<=1)]
print(collections.Counter((b[1],b[2]) for b in bad)); print(bad[:10])
