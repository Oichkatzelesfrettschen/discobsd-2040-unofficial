#if INVENTORY_FIRST
int conditional_first(void) {
#else
int conditional_second(void) {
#endif
	return 1;
}

int conditional_after(void) { return 1; }
