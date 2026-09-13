#define DIV_QUOTIENT_OFFSET 0x70

unsigned int
divider_address(void)
{
	return 0xd0000000U + DIV_QUOTIENT_OFFSET;
}
