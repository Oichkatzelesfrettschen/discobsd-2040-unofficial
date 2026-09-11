/* String handling against the tree's libc. */
int printf(char *fmt, ...);
int strlen(char *);
char *strcpy(char *, char *);
char *strcat(char *, char *);
int strcmp(char *, char *);
void *memset(void *, int, unsigned);

int
main(void)
{
	char buf[64];
	char fill[8];
	int i;

	strcpy(buf, "hello");
	strcat(buf, ", ");
	strcat(buf, "world");
	printf("%s %d\n", buf, strlen(buf));

	printf("%d %d %d\n", strcmp("abc", "abc") == 0,
	    strcmp("abc", "abd") < 0, strcmp("b", "a") > 0);

	memset(fill, '*', 7);
	fill[7] = 0;
	printf("%s\n", fill);

	for (i = 0; buf[i] != 0; i++)
		if (buf[i] >= 'a' && buf[i] <= 'z')
			buf[i] = buf[i] - 'a' + 'A';
	printf("%s\n", buf);
	return 0;
}
