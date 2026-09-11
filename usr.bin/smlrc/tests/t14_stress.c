/*
 * The paths the back end had to be designed around and that the other tests
 * do not reach: calls through function pointers, whole-structure assignment
 * and structure arguments passed by value, frames and local offsets past the
 * reach of a Thumb-1 immediate, and a function long enough that its branches
 * and literal pools cannot use the short encodings.
 */
int printf(char *fmt, ...);

struct big {
	int a;
	int b;
	char pad[24];
	int c;
};

static int add(int x, int y) { return x + y; }
static int sub(int x, int y) { return x - y; }
static int mul(int x, int y) { return x * y; }

static int (*table[3])(int, int);

/* Returned and taken by value, which routes through the structure copy and
   structure push helpers the front end synthesizes. */
static struct big
mkbig(int a, int b, int c)
{
	struct big s;

	s.a = a;
	s.b = b;
	s.c = c;
	s.pad[0] = 'x';
	s.pad[23] = 'y';
	return s;
}

static int
takebig(struct big s)
{
	return s.a + s.b + s.c + s.pad[0] + s.pad[23];
}

/* 512 bytes of locals puts every slot past the 124-byte reach of LDR's
   immediate offset and the 508-byte reach of SUB SP's. */
static int
bigframe(int seed)
{
	int arr[128];
	int i;
	int sum;

	for (i = 0; i < 128; i++)
		arr[i] = seed + i;
	sum = 0;
	for (i = 0; i < 128; i++)
		sum = sum + arr[i];
	return sum;
}

static int
longfxn(int *v)
{
	int sum;
	int k;

	sum = 0;
	/* The loop body is thousands of bytes long, so the branch closing the
	   loop and the branch leaving it both reach far past what a Thumb-1 B
	   or B<cond> encodes. */
	for (k = 0; k < 2; k++) {
		if (v[0] > 0) { sum = sum + v[0] * 3; } else { sum = sum - 0; }
		if (v[1] > 1) { sum = sum + v[1] * 4; } else { sum = sum - 1; }
		if (v[2] > 2) { sum = sum + v[2] * 5; } else { sum = sum - 2; }
		if (v[3] > 3) { sum = sum + v[3] * 6; } else { sum = sum - 3; }
		if (v[4] > 4) { sum = sum + v[4] * 7; } else { sum = sum - 4; }
		if (v[5] > 5) { sum = sum + v[5] * 8; } else { sum = sum - 5; }
		if (v[6] > 6) { sum = sum + v[6] * 9; } else { sum = sum - 6; }
		if (v[7] > 7) { sum = sum + v[7] * 10; } else { sum = sum - 7; }
		if (v[8] > 8) { sum = sum + v[8] * 11; } else { sum = sum - 8; }
		if (v[9] > 9) { sum = sum + v[9] * 12; } else { sum = sum - 9; }
		if (v[10] > 10) { sum = sum + v[10] * 13; } else { sum = sum - 10; }
		if (v[11] > 11) { sum = sum + v[11] * 14; } else { sum = sum - 11; }
		if (v[12] > 12) { sum = sum + v[12] * 15; } else { sum = sum - 12; }
		if (v[13] > 13) { sum = sum + v[13] * 16; } else { sum = sum - 13; }
		if (v[14] > 14) { sum = sum + v[14] * 17; } else { sum = sum - 14; }
		if (v[15] > 15) { sum = sum + v[15] * 18; } else { sum = sum - 15; }
		if (v[16] > 16) { sum = sum + v[16] * 19; } else { sum = sum - 16; }
		if (v[17] > 17) { sum = sum + v[17] * 20; } else { sum = sum - 17; }
		if (v[18] > 18) { sum = sum + v[18] * 21; } else { sum = sum - 18; }
		if (v[19] > 19) { sum = sum + v[19] * 22; } else { sum = sum - 19; }
		if (v[20] > 20) { sum = sum + v[20] * 23; } else { sum = sum - 20; }
		if (v[21] > 21) { sum = sum + v[21] * 24; } else { sum = sum - 21; }
		if (v[22] > 22) { sum = sum + v[22] * 25; } else { sum = sum - 22; }
		if (v[23] > 23) { sum = sum + v[23] * 26; } else { sum = sum - 23; }
		if (v[24] > 24) { sum = sum + v[24] * 27; } else { sum = sum - 24; }
		if (v[25] > 25) { sum = sum + v[25] * 28; } else { sum = sum - 25; }
		if (v[26] > 26) { sum = sum + v[26] * 29; } else { sum = sum - 26; }
		if (v[27] > 27) { sum = sum + v[27] * 30; } else { sum = sum - 27; }
		if (v[28] > 28) { sum = sum + v[28] * 31; } else { sum = sum - 28; }
		if (v[29] > 29) { sum = sum + v[29] * 32; } else { sum = sum - 29; }
		if (v[30] > 30) { sum = sum + v[30] * 33; } else { sum = sum - 30; }
		if (v[31] > 31) { sum = sum + v[31] * 34; } else { sum = sum - 31; }
		if (v[32] > 32) { sum = sum + v[32] * 35; } else { sum = sum - 32; }
		if (v[33] > 33) { sum = sum + v[33] * 36; } else { sum = sum - 33; }
		if (v[34] > 34) { sum = sum + v[34] * 37; } else { sum = sum - 34; }
		if (v[35] > 35) { sum = sum + v[35] * 38; } else { sum = sum - 35; }
		if (v[36] > 36) { sum = sum + v[36] * 39; } else { sum = sum - 36; }
		if (v[37] > 37) { sum = sum + v[37] * 40; } else { sum = sum - 37; }
		if (v[38] > 38) { sum = sum + v[38] * 41; } else { sum = sum - 38; }
		if (v[39] > 39) { sum = sum + v[39] * 42; } else { sum = sum - 39; }
		if (v[0] > 40) { sum = sum + v[0] * 43; } else { sum = sum - 40; }
		if (v[1] > 41) { sum = sum + v[1] * 44; } else { sum = sum - 41; }
		if (v[2] > 42) { sum = sum + v[2] * 45; } else { sum = sum - 42; }
		if (v[3] > 43) { sum = sum + v[3] * 46; } else { sum = sum - 43; }
		if (v[4] > 44) { sum = sum + v[4] * 47; } else { sum = sum - 44; }
		if (v[5] > 45) { sum = sum + v[5] * 48; } else { sum = sum - 45; }
		if (v[6] > 46) { sum = sum + v[6] * 49; } else { sum = sum - 46; }
		if (v[7] > 47) { sum = sum + v[7] * 50; } else { sum = sum - 47; }
		if (v[8] > 48) { sum = sum + v[8] * 51; } else { sum = sum - 48; }
		if (v[9] > 49) { sum = sum + v[9] * 52; } else { sum = sum - 49; }
		if (v[10] > 50) { sum = sum + v[10] * 53; } else { sum = sum - 50; }
		if (v[11] > 51) { sum = sum + v[11] * 54; } else { sum = sum - 51; }
		if (v[12] > 52) { sum = sum + v[12] * 55; } else { sum = sum - 52; }
		if (v[13] > 53) { sum = sum + v[13] * 56; } else { sum = sum - 53; }
		if (v[14] > 54) { sum = sum + v[14] * 57; } else { sum = sum - 54; }
		if (v[15] > 55) { sum = sum + v[15] * 58; } else { sum = sum - 55; }
		if (v[16] > 56) { sum = sum + v[16] * 59; } else { sum = sum - 56; }
		if (v[17] > 57) { sum = sum + v[17] * 60; } else { sum = sum - 57; }
		if (v[18] > 58) { sum = sum + v[18] * 61; } else { sum = sum - 58; }
		if (v[19] > 59) { sum = sum + v[19] * 62; } else { sum = sum - 59; }
		if (v[20] > 60) { sum = sum + v[20] * 63; } else { sum = sum - 60; }
		if (v[21] > 61) { sum = sum + v[21] * 64; } else { sum = sum - 61; }
		if (v[22] > 62) { sum = sum + v[22] * 65; } else { sum = sum - 62; }
		if (v[23] > 63) { sum = sum + v[23] * 66; } else { sum = sum - 63; }
		if (v[24] > 64) { sum = sum + v[24] * 67; } else { sum = sum - 64; }
		if (v[25] > 65) { sum = sum + v[25] * 68; } else { sum = sum - 65; }
		if (v[26] > 66) { sum = sum + v[26] * 69; } else { sum = sum - 66; }
		if (v[27] > 67) { sum = sum + v[27] * 70; } else { sum = sum - 67; }
		if (v[28] > 68) { sum = sum + v[28] * 71; } else { sum = sum - 68; }
		if (v[29] > 69) { sum = sum + v[29] * 72; } else { sum = sum - 69; }
		if (v[30] > 70) { sum = sum + v[30] * 73; } else { sum = sum - 70; }
		if (v[31] > 71) { sum = sum + v[31] * 74; } else { sum = sum - 71; }
		if (v[32] > 72) { sum = sum + v[32] * 75; } else { sum = sum - 72; }
		if (v[33] > 73) { sum = sum + v[33] * 76; } else { sum = sum - 73; }
		if (v[34] > 74) { sum = sum + v[34] * 77; } else { sum = sum - 74; }
		if (v[35] > 75) { sum = sum + v[35] * 78; } else { sum = sum - 75; }
		if (v[36] > 76) { sum = sum + v[36] * 79; } else { sum = sum - 76; }
		if (v[37] > 77) { sum = sum + v[37] * 80; } else { sum = sum - 77; }
		if (v[38] > 78) { sum = sum + v[38] * 81; } else { sum = sum - 78; }
		if (v[39] > 79) { sum = sum + v[39] * 82; } else { sum = sum - 79; }
		if (v[0] > 80) { sum = sum + v[0] * 83; } else { sum = sum - 80; }
		if (v[1] > 81) { sum = sum + v[1] * 84; } else { sum = sum - 81; }
		if (v[2] > 82) { sum = sum + v[2] * 85; } else { sum = sum - 82; }
		if (v[3] > 83) { sum = sum + v[3] * 86; } else { sum = sum - 83; }
		if (v[4] > 84) { sum = sum + v[4] * 87; } else { sum = sum - 84; }
		if (v[5] > 85) { sum = sum + v[5] * 88; } else { sum = sum - 85; }
		if (v[6] > 86) { sum = sum + v[6] * 89; } else { sum = sum - 86; }
		if (v[7] > 87) { sum = sum + v[7] * 90; } else { sum = sum - 87; }
		if (v[8] > 88) { sum = sum + v[8] * 91; } else { sum = sum - 88; }
		if (v[9] > 89) { sum = sum + v[9] * 92; } else { sum = sum - 89; }
		if (v[10] > 90) { sum = sum + v[10] * 93; } else { sum = sum - 90; }
		if (v[11] > 91) { sum = sum + v[11] * 94; } else { sum = sum - 91; }
		if (v[12] > 92) { sum = sum + v[12] * 95; } else { sum = sum - 92; }
		if (v[13] > 93) { sum = sum + v[13] * 96; } else { sum = sum - 93; }
		if (v[14] > 94) { sum = sum + v[14] * 97; } else { sum = sum - 94; }
		if (v[15] > 95) { sum = sum + v[15] * 98; } else { sum = sum - 95; }
		if (v[16] > 96) { sum = sum + v[16] * 99; } else { sum = sum - 96; }
		if (v[17] > 97) { sum = sum + v[17] * 100; } else { sum = sum - 97; }
		if (v[18] > 98) { sum = sum + v[18] * 101; } else { sum = sum - 98; }
		if (v[19] > 99) { sum = sum + v[19] * 102; } else { sum = sum - 99; }
		if (v[20] > 100) { sum = sum + v[20] * 103; } else { sum = sum - 100; }
		if (v[21] > 101) { sum = sum + v[21] * 104; } else { sum = sum - 101; }
		if (v[22] > 102) { sum = sum + v[22] * 105; } else { sum = sum - 102; }
		if (v[23] > 103) { sum = sum + v[23] * 106; } else { sum = sum - 103; }
		if (v[24] > 104) { sum = sum + v[24] * 107; } else { sum = sum - 104; }
		if (v[25] > 105) { sum = sum + v[25] * 108; } else { sum = sum - 105; }
		if (v[26] > 106) { sum = sum + v[26] * 109; } else { sum = sum - 106; }
		if (v[27] > 107) { sum = sum + v[27] * 110; } else { sum = sum - 107; }
		if (v[28] > 108) { sum = sum + v[28] * 111; } else { sum = sum - 108; }
		if (v[29] > 109) { sum = sum + v[29] * 112; } else { sum = sum - 109; }
		if (v[30] > 110) { sum = sum + v[30] * 113; } else { sum = sum - 110; }
		if (v[31] > 111) { sum = sum + v[31] * 114; } else { sum = sum - 111; }
		if (v[32] > 112) { sum = sum + v[32] * 115; } else { sum = sum - 112; }
		if (v[33] > 113) { sum = sum + v[33] * 116; } else { sum = sum - 113; }
		if (v[34] > 114) { sum = sum + v[34] * 117; } else { sum = sum - 114; }
		if (v[35] > 115) { sum = sum + v[35] * 118; } else { sum = sum - 115; }
		if (v[36] > 116) { sum = sum + v[36] * 119; } else { sum = sum - 116; }
		if (v[37] > 117) { sum = sum + v[37] * 120; } else { sum = sum - 117; }
		if (v[38] > 118) { sum = sum + v[38] * 121; } else { sum = sum - 118; }
		if (v[39] > 119) { sum = sum + v[39] * 122; } else { sum = sum - 119; }
		if (v[0] > 120) { sum = sum + v[0] * 123; } else { sum = sum - 120; }
		if (v[1] > 121) { sum = sum + v[1] * 124; } else { sum = sum - 121; }
		if (v[2] > 122) { sum = sum + v[2] * 125; } else { sum = sum - 122; }
		if (v[3] > 123) { sum = sum + v[3] * 126; } else { sum = sum - 123; }
		if (v[4] > 124) { sum = sum + v[4] * 127; } else { sum = sum - 124; }
		if (v[5] > 125) { sum = sum + v[5] * 128; } else { sum = sum - 125; }
		if (v[6] > 126) { sum = sum + v[6] * 129; } else { sum = sum - 126; }
		if (v[7] > 127) { sum = sum + v[7] * 130; } else { sum = sum - 127; }
		if (v[8] > 128) { sum = sum + v[8] * 131; } else { sum = sum - 128; }
		if (v[9] > 129) { sum = sum + v[9] * 132; } else { sum = sum - 129; }
		if (v[10] > 130) { sum = sum + v[10] * 133; } else { sum = sum - 130; }
		if (v[11] > 131) { sum = sum + v[11] * 134; } else { sum = sum - 131; }
		if (v[12] > 132) { sum = sum + v[12] * 135; } else { sum = sum - 132; }
		if (v[13] > 133) { sum = sum + v[13] * 136; } else { sum = sum - 133; }
		if (v[14] > 134) { sum = sum + v[14] * 137; } else { sum = sum - 134; }
		if (v[15] > 135) { sum = sum + v[15] * 138; } else { sum = sum - 135; }
		if (v[16] > 136) { sum = sum + v[16] * 139; } else { sum = sum - 136; }
		if (v[17] > 137) { sum = sum + v[17] * 140; } else { sum = sum - 137; }
		if (v[18] > 138) { sum = sum + v[18] * 141; } else { sum = sum - 138; }
		if (v[19] > 139) { sum = sum + v[19] * 142; } else { sum = sum - 139; }
		if (v[20] > 140) { sum = sum + v[20] * 143; } else { sum = sum - 140; }
		if (v[21] > 141) { sum = sum + v[21] * 144; } else { sum = sum - 141; }
		if (v[22] > 142) { sum = sum + v[22] * 145; } else { sum = sum - 142; }
		if (v[23] > 143) { sum = sum + v[23] * 146; } else { sum = sum - 143; }
		if (v[24] > 144) { sum = sum + v[24] * 147; } else { sum = sum - 144; }
		if (v[25] > 145) { sum = sum + v[25] * 148; } else { sum = sum - 145; }
		if (v[26] > 146) { sum = sum + v[26] * 149; } else { sum = sum - 146; }
		if (v[27] > 147) { sum = sum + v[27] * 150; } else { sum = sum - 147; }
		if (v[28] > 148) { sum = sum + v[28] * 151; } else { sum = sum - 148; }
		if (v[29] > 149) { sum = sum + v[29] * 152; } else { sum = sum - 149; }
		if (v[30] > 150) { sum = sum + v[30] * 153; } else { sum = sum - 150; }
		if (v[31] > 151) { sum = sum + v[31] * 154; } else { sum = sum - 151; }
		if (v[32] > 152) { sum = sum + v[32] * 155; } else { sum = sum - 152; }
		if (v[33] > 153) { sum = sum + v[33] * 156; } else { sum = sum - 153; }
		if (v[34] > 154) { sum = sum + v[34] * 157; } else { sum = sum - 154; }
		if (v[35] > 155) { sum = sum + v[35] * 158; } else { sum = sum - 155; }
		if (v[36] > 156) { sum = sum + v[36] * 159; } else { sum = sum - 156; }
		if (v[37] > 157) { sum = sum + v[37] * 160; } else { sum = sum - 157; }
		if (v[38] > 158) { sum = sum + v[38] * 161; } else { sum = sum - 158; }
		if (v[39] > 159) { sum = sum + v[39] * 162; } else { sum = sum - 159; }
		if (v[0] > 160) { sum = sum + v[0] * 163; } else { sum = sum - 160; }
		if (v[1] > 161) { sum = sum + v[1] * 164; } else { sum = sum - 161; }
		if (v[2] > 162) { sum = sum + v[2] * 165; } else { sum = sum - 162; }
		if (v[3] > 163) { sum = sum + v[3] * 166; } else { sum = sum - 163; }
		if (v[4] > 164) { sum = sum + v[4] * 167; } else { sum = sum - 164; }
		if (v[5] > 165) { sum = sum + v[5] * 168; } else { sum = sum - 165; }
		if (v[6] > 166) { sum = sum + v[6] * 169; } else { sum = sum - 166; }
		if (v[7] > 167) { sum = sum + v[7] * 170; } else { sum = sum - 167; }
		if (v[8] > 168) { sum = sum + v[8] * 171; } else { sum = sum - 168; }
		if (v[9] > 169) { sum = sum + v[9] * 172; } else { sum = sum - 169; }
		if (v[10] > 170) { sum = sum + v[10] * 173; } else { sum = sum - 170; }
		if (v[11] > 171) { sum = sum + v[11] * 174; } else { sum = sum - 171; }
		if (v[12] > 172) { sum = sum + v[12] * 175; } else { sum = sum - 172; }
		if (v[13] > 173) { sum = sum + v[13] * 176; } else { sum = sum - 173; }
		if (v[14] > 174) { sum = sum + v[14] * 177; } else { sum = sum - 174; }
		if (v[15] > 175) { sum = sum + v[15] * 178; } else { sum = sum - 175; }
		if (v[16] > 176) { sum = sum + v[16] * 179; } else { sum = sum - 176; }
		if (v[17] > 177) { sum = sum + v[17] * 180; } else { sum = sum - 177; }
		if (v[18] > 178) { sum = sum + v[18] * 181; } else { sum = sum - 178; }
		if (v[19] > 179) { sum = sum + v[19] * 182; } else { sum = sum - 179; }
		if (v[20] > 180) { sum = sum + v[20] * 183; } else { sum = sum - 180; }
		if (v[21] > 181) { sum = sum + v[21] * 184; } else { sum = sum - 181; }
		if (v[22] > 182) { sum = sum + v[22] * 185; } else { sum = sum - 182; }
		if (v[23] > 183) { sum = sum + v[23] * 186; } else { sum = sum - 183; }
		if (v[24] > 184) { sum = sum + v[24] * 187; } else { sum = sum - 184; }
		if (v[25] > 185) { sum = sum + v[25] * 188; } else { sum = sum - 185; }
		if (v[26] > 186) { sum = sum + v[26] * 189; } else { sum = sum - 186; }
		if (v[27] > 187) { sum = sum + v[27] * 190; } else { sum = sum - 187; }
		if (v[28] > 188) { sum = sum + v[28] * 191; } else { sum = sum - 188; }
		if (v[29] > 189) { sum = sum + v[29] * 192; } else { sum = sum - 189; }
		if (v[30] > 190) { sum = sum + v[30] * 193; } else { sum = sum - 190; }
		if (v[31] > 191) { sum = sum + v[31] * 194; } else { sum = sum - 191; }
		if (v[32] > 192) { sum = sum + v[32] * 195; } else { sum = sum - 192; }
		if (v[33] > 193) { sum = sum + v[33] * 196; } else { sum = sum - 193; }
		if (v[34] > 194) { sum = sum + v[34] * 197; } else { sum = sum - 194; }
		if (v[35] > 195) { sum = sum + v[35] * 198; } else { sum = sum - 195; }
		if (v[36] > 196) { sum = sum + v[36] * 199; } else { sum = sum - 196; }
		if (v[37] > 197) { sum = sum + v[37] * 200; } else { sum = sum - 197; }
		if (v[38] > 198) { sum = sum + v[38] * 201; } else { sum = sum - 198; }
		if (v[39] > 199) { sum = sum + v[39] * 202; } else { sum = sum - 199; }
		if (v[0] > 200) { sum = sum + v[0] * 203; } else { sum = sum - 200; }
		if (v[1] > 201) { sum = sum + v[1] * 204; } else { sum = sum - 201; }
		if (v[2] > 202) { sum = sum + v[2] * 205; } else { sum = sum - 202; }
		if (v[3] > 203) { sum = sum + v[3] * 206; } else { sum = sum - 203; }
		if (v[4] > 204) { sum = sum + v[4] * 207; } else { sum = sum - 204; }
		if (v[5] > 205) { sum = sum + v[5] * 208; } else { sum = sum - 205; }
		if (v[6] > 206) { sum = sum + v[6] * 209; } else { sum = sum - 206; }
		if (v[7] > 207) { sum = sum + v[7] * 210; } else { sum = sum - 207; }
		if (v[8] > 208) { sum = sum + v[8] * 211; } else { sum = sum - 208; }
		if (v[9] > 209) { sum = sum + v[9] * 212; } else { sum = sum - 209; }
		if (v[10] > 210) { sum = sum + v[10] * 213; } else { sum = sum - 210; }
		if (v[11] > 211) { sum = sum + v[11] * 214; } else { sum = sum - 211; }
		if (v[12] > 212) { sum = sum + v[12] * 215; } else { sum = sum - 212; }
		if (v[13] > 213) { sum = sum + v[13] * 216; } else { sum = sum - 213; }
		if (v[14] > 214) { sum = sum + v[14] * 217; } else { sum = sum - 214; }
		if (v[15] > 215) { sum = sum + v[15] * 218; } else { sum = sum - 215; }
		if (v[16] > 216) { sum = sum + v[16] * 219; } else { sum = sum - 216; }
		if (v[17] > 217) { sum = sum + v[17] * 220; } else { sum = sum - 217; }
		if (v[18] > 218) { sum = sum + v[18] * 221; } else { sum = sum - 218; }
		if (v[19] > 219) { sum = sum + v[19] * 222; } else { sum = sum - 219; }
	}
	return sum;
}

int
main(void)
{
	struct big s;
	struct big t;
	int v[40];
	int i;
	int (*fp)(int, int);

	table[0] = add;
	table[1] = sub;
	table[2] = mul;
	for (i = 0; i < 3; i++)
		printf("%d ", table[i](12, 4));
	printf("\n");

	fp = mul;
	printf("%d %d\n", fp(6, 7), (*fp)(3, 3));

	s = mkbig(1, 2, 3);
	t = s;			/* whole-structure assignment */
	t.b = 20;
	printf("%d %d %d %d\n", s.a, s.b, t.b, takebig(t));

	printf("%d\n", bigframe(1000));

	for (i = 0; i < 40; i++)
		v[i] = i * 7 - 50;
	printf("%d\n", longfxn(v));
	return 0;
}
