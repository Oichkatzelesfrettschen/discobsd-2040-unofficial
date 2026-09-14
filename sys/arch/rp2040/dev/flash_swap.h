/*
 * Raw swap programming at the RP2040 flash erase/program geometries.
 */
#ifndef _RP2040_DEV_FLASH_SWAP_H_
#define _RP2040_DEV_FLASH_SWAP_H_

struct flash_swap_ops {
	int	(*read)(unsigned int, unsigned char *, unsigned int);
	int	(*erase)(unsigned int, unsigned int);
	int	(*program)(unsigned int, const unsigned char *, unsigned int);
};

int flash_swap_append(const struct flash_swap_ops *, unsigned int,
    const unsigned char *, unsigned int, unsigned char *);
int flash_swap_rewrite(const struct flash_swap_ops *, unsigned int,
    const unsigned char *, unsigned int, unsigned int, unsigned char *);

#endif /* !_RP2040_DEV_FLASH_SWAP_H_ */
