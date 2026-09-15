# RP2040 cache footprint

The RP2040 kernel spends cycles on smaller caches and bounded linear lookup and
limits the shipped image to its one UFS root to recover 8,720 bytes from the
named buffer, inode, mount, and pathname allocations.
The exec argument spool reduces the buffer data pool from ten to four
1,024-byte blocks. The pathname cache keeps four entries and replaces its hash
and LRU structures with a second-chance clock.

Run the complete host gate from the repository root:

```console
bmake MACHINE=rp2040 check-cache-footprint
```

The gate builds both kernel configurations, checks the linked ARM symbols, and
compiles the real `nchinit()` into host executables. The ARM symbol gate checks
the target structure widths. The host executables directly execute linear
empty-slot reuse, the second-chance clock sweep and replacement order,
`nchinval()`, 16-bit inode-generation wrap, and compact persistent-flag
rejection. They also check legacy hash configurations, configuration bounds,
and rejection of a cache hit whose entry or inode generation changes while
`igrab()` sleeps.

| Mechanism | Saved allocation bytes |
| --- | ---: |
| Exec spool and `NBUF=4` | 6,408 |
| One mount record | 1,220 |
| Synthesized root names and omitted quota pointer | 184 |
| Linear inode lookup | 320 |
| Compact inode fields | 192 |
| Derived inode filesystem identity | 192 |
| Linear buffer lookup | 104 |
| Linear pathname lookup and clock replacement | 100 |
| Total | 8,720 |

The earlier RP2040 cache-count sizing from 16 buffer hash heads, 26 pathname
entries, and 16 pathname hash heads to four of each saved another 1,384 bytes.
The 8,720-byte total above measures only the mechanisms implemented by the
exec-spool, single-mount, and linear-layout work.

`NBUF` is four. The exec argument spool owns one staging block instead of
retaining six argument blocks. A packed decoder and `rdwri()` can each own one
additional block while the fourth block preserves one sleep margin. The host
exec-spool gate verifies serialization, replay, SwapRAM coexistence, raw-swap
fallback, SwapRAM-to-flash migration, cleanup, script argument ordering,
`NCARGS`, and records that cross block boundaries.

`NMOUNT` is one in both shipped RP2040 configurations. The onboard image has
one UFS filesystem on `fl0a`; `fl1` is raw swap rather than a mountable
filesystem. Root remount, `df`, `statfs`, mount listing, and `umount -a` retain
the root record. An additional mount exhausts the existing table and returns
the existing table-full result. `SINGLE_UFS_ROOT` derives each inode's device
and filesystem from that record, synthesizes `"root"` and `"/"` for `statfs`,
and omits the unused quota pointer and mount-name arrays. `iget()` rejects an
identity outside the root record before populating an inode.

`LINEAR_INODE_CACHE` removes two hash pointers from each of the 24 cached
inodes and removes the 16 hash heads. `ifind()` and `iget()` scan the bounded
24-entry table instead. The free-list pointers remain because inode allocation
and release use them independently of lookup.

`COMPACT_INODE_FIELDS` stores `i_flag`, `i_count`, `i_id`, and `i_flags` as
four adjacent 16-bit fields. Compile-time assertions bind the transient and
persistent flag masks and the fixed-table reference bound to that width. The
generation wrap check observes the narrowed `i_id`, so the pathname cache is
invalidated when its stored 16-bit generation wraps rather than after a
32-bit wrap.

`LINEAR_BUFFER_CACHE` scans four buffer headers by device and disk block. The
free queues retain their independent doubly linked pointers, while each buffer
loses its two hash pointers and the four hash heads disappear.

`LINEAR_NAME_CACHE` scans four pathname entries and uses one second-chance bit
per entry. The clock preserves recently hit names without hash links, a doubly
linked LRU, or global LRU pointers. Inode pointer and generation snapshots
still detect an entry recycled while `igrab()` sleeps.

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
