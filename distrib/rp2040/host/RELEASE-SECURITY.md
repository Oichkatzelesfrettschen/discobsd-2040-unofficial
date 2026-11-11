# Hybrid release verification

`discobsd-release` requires OpenSSL 3.5 or newer with Ed25519 and ML-DSA-65.
It adds no board code. Both signatures cover the same domain-separated,
fixed-size binary manifest. Each release binds four kernel variants and one
root image: PICO.uf2, PICO_UART.uf2, PICOCALC.uf2, PICOCALC_PSRAM.uf2 and
root.uf2. The manifest carries version, key epoch, release sequence, source
commit and toolchain digest, and raw/payload hashes, sizes and address ranges
for exactly five records in that order. UF2 flags, family, numbering, coverage,
alignment and unused padding are checked; unsupported metadata fails closed.

Assemble those five named files in an artifact directory. Create a new manifest:

```sh
discobsd-release manifest --directory artifacts --manifest release.bin \
  --epoch 1 --sequence 7 --commit COMMIT_HEX --toolchain-sha256 DIGEST_HEX
```

On the controlled signing host, sign with two provisioned private keys:

```sh
discobsd-release sign --manifest release.bin --epoch 1 \
  --ed25519 ed25519-private.pem --mldsa65 mldsa65-private.pem
```

The signer writes release.bin.ed25519 and release.bin.mldsa65 without
overwriting existing files. A partially failed signing operation is not a
valid release. Do not generate production keys in CI. These commands do not
provision an HSM, independently reproduce a build, or establish key custody.

On the consuming host, pin public keys through a trusted installation path;
never trust keys merely because they accompany a download. Create a private
0700 directory for rollback state (on Windows use an owner-only ACL). Verification:

```sh
discobsd-release verify --directory artifacts --manifest release.bin --epoch 1 \
  --ed25519 trusted-ed25519.pem --mldsa65 trusted-mldsa65.pem \
  --state private-state/sequence
```

`verify` does not advance rollback state. On POSIX with the installed
discobsd-flash/picotool tools, replace `verify` with `flash` and add
`--target PICO.uf2` to perform a verified operation. Private copies are
created only after both signatures and all hashes pass. The root is loaded
before the selected kernel; picotool load -v verifies writes before reboot.
Only a successful delegate invocation advances the state atomically. A
failed or interrupted update may leave a mixed root/kernel on flash and
requires recovery; a signed bundle cannot make a two-image flash transactional.

The state lock refuses concurrent operations; stale crash locks require owner
inspection and recovery. Deleting state or compromising the host defeats
rollback protection. Key epochs are explicitly pinned by the operator; use
separate state files when release numbering changes across an epoch. This
version does not automate key rotation or independent trust bootstrap.

The legacy discobsd-flash FILE.uf2 interface remains an explicit unsigned
recovery path. RP2040 ROM also accepts arbitrary UF2/PICOBOOT/SWD updates.
This is authenticated release distribution and verified flashing, not secure
boot. No public key burned into mutable flash changes that limitation.

The convenience console and both user services bind loopback by default.
`discobsd-console up --bind 0.0.0.0` explicitly opts into LAN exposure,
including the token-revealing short link. Prefer SSH forwarding with
mlkem768x25519-sha256 inside an independently provisioned WireGuard tunnel
with a unique random 256-bit PresharedKey. Network deployment and key
distribution are administrative actions not performed by this package.

Run `DISCOBSD_REQUIRE_PQ_TESTS=1 python -m pytest -q` in this directory.
The dedicated hybrid CI installs ML-DSA-capable OpenSSL and requires actual
hybrid tests; ordinary host tests explicitly skip hybrid cases when the
installed OpenSSL lacks the algorithm. A skip is not a passing PQ result.
