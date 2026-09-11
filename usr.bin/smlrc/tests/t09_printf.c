/* Integer formatting through the tree's libc printf, which is the path
   every stacked-argument and variadic call in generated code depends on. */
int printf(char *fmt, ...);
int sprintf(char *, char *, ...);

int
main(void)
{
	char buf[64];
	int n;

	printf("%d|%5d|%-5d|%05d|\n", 42, 42, 42, 42);
	printf("%d %d\n", 0, -1);
	printf("%d %d\n", 2147483647, -2147483647 - 1);
	printf("%u %u\n", 0u, 4294967295u);
	printf("%x %X %o\n", 48879, 48879, 511);
	printf("%c%c%c\n", 'o', 'k', '!');
	printf("%s|%10s|%-10s|\n", "str", "str", "str");
	printf("%d%%\n", 50);

	n = sprintf(buf, "%d,%d,%d", 1, 22, 333);
	printf("%s %d\n", buf, n);
	return 0;
}
