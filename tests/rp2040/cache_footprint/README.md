# RP2040 cache footprint

The RP2040 kernel spends cycles on additional linear scans to recover 1,384
bytes of permanent SRAM. The buffer data pool stays at ten 1,024-byte blocks;
only its hash heads shrink. The pathname cache keeps four entries and four
hash heads.

Run the complete host gate from the repository root:

```console
bmake MACHINE=rp2040 check-cache-footprint
```

The gate builds both kernel configurations, checks the linked ARM symbols, and
compiles the real `nchinit()` into a host executable. The host executable checks
every LRU forward and back link, every empty hash sentinel, every representative
hash result, bucket coverage for small and 37-bucket tables, and rejection of a
cache hit whose entry or inode generation changes while `igrab()` sleeps.

| Allocation | Previous RP2040 bytes | New RP2040 bytes | Saved bytes |
| --- | ---: | ---: | ---: |
| `bufhash` | 192 | 48 | 144 |
| `namecache` | 1,352 | 208 | 1,144 |
| `nchash` | 128 | 32 | 96 |
| Total | 1,672 | 288 | 1,384 |

`NBUF` remains 10. `MAXALLOCBUF` permits six blocks to remain owned by one
exec, the packed decoder owns another block, and `rdwri()` can own a filesystem
I/O block while the decoder remains live. A value of four therefore cannot run
a legal packed exec path. Eight only covers that single path and leaves no
concurrency margin when `rdwri()` sleeps. A future reduction needs dedicated
exec scratch storage or buffer reservations before changing `NBUF`.

## Board pressure procedure

The host gate proves layout and data-structure behavior. The RP2040 board must
prove that the smaller caches only increase misses. Flashing and board control
require explicit operator authorization.

After building and flashing an authorized test image, retain the complete
serial transcript and the exact kernel commit. Log in as root, record `df`, and
run the following bounded workload:

```sh
mkdir /tmp/cache-pressure
i=0
while test "$i" -lt 24; do
    mkdir /tmp/cache-pressure/d$i
    touch /tmp/cache-pressure/d$i/file
    mv /tmp/cache-pressure/d$i/file /tmp/cache-pressure/d$i/renamed
    i=`expr "$i" + 1`
done
find /tmp/cache-pressure -print > /tmp/cache-find.out
du /tmp/cache-pressure > /tmp/cache-du.out
dd if=/dev/zero of=/tmp/cache-pressure/large bs=1024 count=96
md5 /tmp/cache-pressure/large
dd if=/tmp/cache-pressure/large of=/dev/null bs=1024
find /etc /bin /sbin /usr -print > /tmp/cache-tree.out &
du /etc /bin /sbin /usr > /tmp/cache-tree-du.out &
tar cf /tmp/cache-tree.tar /etc /bin /sbin &
wait
sync
md5 /tmp/cache-pressure/large
rm -f /tmp/cache-tree.tar /tmp/cache-tree.out /tmp/cache-tree-du.out
rm -f /tmp/cache-find.out /tmp/cache-du.out
rm -rf /tmp/cache-pressure
sync
df
shutdown -r now
```

The reboot must reach multiuser mode after `/etc/rc` reports a successful
`fsck -p`. Failure means a panic, hang, I/O error, checksum mismatch, process
that cannot finish, filesystem repair, or a final free-block count that does
not return to the pre-workload value. Repeat the workload three times because
one pass cannot expose replacement-order and delayed-write interactions.
