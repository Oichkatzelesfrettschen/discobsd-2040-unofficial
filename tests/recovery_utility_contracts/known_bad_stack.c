int
main(void)
{
	volatile unsigned char oversized_frame[1024];

	oversized_frame[0] = 0;
	return oversized_frame[0];
}
