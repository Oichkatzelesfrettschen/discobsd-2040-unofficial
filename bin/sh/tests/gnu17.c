/* The maintained target shell uses the production GNU17 language contract. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L
#error "bin/sh requires GNU17 or strict C17"
#endif

int
main(void)
{
	return 0;
}
