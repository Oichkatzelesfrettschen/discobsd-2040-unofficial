extern unsigned char _ctype_[];
extern int _doscan(void);
extern int scanf(const char *, ...);
extern int sscanf(const char *, const char *, ...);

unsigned char *bad_ctype_reference = _ctype_;
int (*bad_doscan_reference)(void) = _doscan;
int (*bad_scanf_reference)(const char *, ...) = scanf;
int (*bad_sscanf_reference)(const char *, const char *, ...) = sscanf;
