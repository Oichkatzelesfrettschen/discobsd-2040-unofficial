/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * /dev/audio on the PicoCalc: unsigned 8-bit mono samples at 8,000 per
 * second, played on both amplifier channels (picopcm.c). The ring and the
 * sample scaling are here so that a host program checks them.
 *
 * The ring has one writer, the process in pcmwrite(), which moves head
 * alone, and one reader, the sample interrupt, which moves tail alone.
 * Both indices are single bytes over a 256-byte array, so each wraps by
 * itself and a store of either is one instruction; head equal to tail is
 * empty and the ring holds at most 255 samples, 31.9 ms.
 */

#ifndef _PICOPCM_H
#define	_PICOPCM_H

#define	PCM_RATE	8000		/* Samples per second. */
#define	PCM_RING	256		/* Bytes; the indices rely on it. */
#define	PCM_LEVELS	250		/* Duty steps of the 500 kHz carrier. */
#define	PCM_SILENCE	0x80		/* The sample at rest: half duty. */
#define	PCM_CHANNELS	1		/* Channels in the stream written. */
#define	PCM_BITS	8		/* Bits per sample. */

struct pcm_ring {
	volatile unsigned char	head;	/* Next slot the writer fills. */
	volatile unsigned char	tail;	/* Next slot the interrupt plays. */
	unsigned char		data[PCM_RING];
};

/* Samples queued. */
static inline unsigned int
pcm_fill(const struct pcm_ring *r)
{
	return (unsigned char)(r->head - r->tail);
}

/* Samples the writer may add. */
static inline unsigned int
pcm_space(const struct pcm_ring *r)
{
	return PCM_RING - 1 - pcm_fill(r);
}

/* Queue one sample; the caller found space. The slot is filled first. */
static inline void
pcm_put(struct pcm_ring *r, unsigned char sample)
{
	r->data[r->head] = sample;
	r->head = (unsigned char)(r->head + 1);
}

/* The next sample to play, or PCM_SILENCE from an empty ring. */
static inline unsigned char
pcm_get(struct pcm_ring *r)
{
	unsigned char sample;

	if (r->head == r->tail)
		return PCM_SILENCE;
	sample = r->data[r->tail];
	r->tail = (unsigned char)(r->tail + 1);
	return sample;
}

/* The compare value for a sample: 0-255 onto 0 to PCM_LEVELS - 1. */
static inline unsigned int
pcm_level(unsigned char sample)
{
	return sample * PCM_LEVELS >> 8;
}

struct uio;

int	pcmopen(dev_t dev, int flag, int mode);
int	pcmclose(dev_t dev, int flag, int mode);
int	pcmread(dev_t dev, struct uio *uio, int flag);
int	pcmwrite(dev_t dev, struct uio *uio, int flag);
unsigned int	pcm_queued(void);

/* picoaudio.c: nonzero while /dev/audio owns the slice; BEL is absorbed. */
extern int picoaudio_pcm;
void	picoaudio_mute(void);

#endif /* !_PICOPCM_H */
