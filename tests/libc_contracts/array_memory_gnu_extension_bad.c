/* The strict C17 Cortex-M0+ gate must reject GNU typeof syntax. */
int
array_memory_gnu_extension_bad(typeof(1) value)
{
	return value;
}
