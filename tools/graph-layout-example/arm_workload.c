/* Host-emulated harness for the production HSX decoder; never flashed. */
#include <machine/hsx_decoder.h>

unsigned
workload(const unsigned char *packed, unsigned packed_size,
    unsigned char *output, unsigned output_size)
{
    struct hsx_dec decoder;
    hsx_dec_init(&decoder, 3);
    hsx_dec_input(&decoder, packed, packed_size);
    return hsx_dec_run(&decoder, output, ~0u, output_size);
}
