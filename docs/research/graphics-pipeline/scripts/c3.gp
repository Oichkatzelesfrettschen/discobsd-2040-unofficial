\\ segment:offset aliasing, c3.gp
print("kernel lattice of (s,o) -> 16 s + o over Z: ", matkerint(Mat([16,1])));
cnt(a) = my(c=0); for(s=0,65535, my(o=(a-16*s)%(2^20)); if(o<65536,c++)); c;
print("fibre sizes of (s,o)->(16 s+o) mod 2^20, o in [0,65536), for a = 0, 1, 15, 16, 65535, 65536, 524288, 1048575: ", vector(8,i,cnt([0,1,15,16,65535,65536,524288,1048575][i])));
\\ CGA interleave: addr(y,x) = 8192*(y%2) + 80*(y\2) + x\4 on 200 lines x 320 pixels
v=List(); for(y=0,199, for(x4=0,79, listput(v, 8192*(y%2)+80*(y\2)+x4)));
v=vecsort(Vec(v),,8);
print("CGA 320x200x2bpp bytes addressed: ", #v, " distinct, max address ", vecmax(v), ", unused bytes inside the 2 x 8 KB banks: ", 16384-#v);
\\ EGA/VGA planar: pixel (x,y) 640 wide -> byte y*80 + x\8, bit 7 - x%8, plane = color bit
print("planar 640x350x4bpp bytes per plane: ", 80*350, ", total ", 4*80*350, " (one 28 KB plane per bit)");
\\ number of 4-bit packed words vs planes: basis change = 4x4 bit transpose per 32 pixels
print("bits moved per 8 pixels in chunky->planar: ", 8*4, " in ", 4, " bytes out");
