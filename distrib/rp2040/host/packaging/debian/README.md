# Debian packaging for discobsd-host

`dpkg-buildpackage` reads `debian/` from the root of the source package, and
the source package root here is `distrib/rp2040/host` -- the directory that
holds `pyproject.toml`. This directory is the `debian/` tree, kept one level
down under `packaging/` so the checked-in host tree stays a plain Python
package.

Build it by copying this directory into place first:

    cd distrib/rp2040/host
    cp -a packaging/debian debian
    dpkg-buildpackage -us -uc -b
    rm -rf debian

The `.github/workflows/host.yml` `deb` job runs exactly that sequence. The
copy is build output, so `distrib/rp2040/host/.gitignore` excludes
`/distrib/rp2040/host/debian/`; a commit never carries the copy, only this
original.

Build dependencies on Ubuntu 24.04:

    sudo apt-get install build-essential debhelper dh-python python3-all \
        python3-setuptools python3-pytest python3-serial \
        pybuild-plugin-pyproject devscripts

The package is native (`debian/source/format` reads `3.0 (native)`), so the
version in `debian/changelog` carries no Debian revision: `1.0.0`, never
`1.0.0-1`.

What the binary package installs beyond the Python modules and the four
console scripts, through `debian/discobsd-host.install`:

- `/usr/lib/udev/rules.d/71-discobsd-rp2040.rules`
- `/usr/lib/systemd/user/discobsd-web.service`
- `/usr/lib/systemd/user/discobsd-link.service`
- `/usr/bin/discobsd-connect`

`dh_auto_test` runs the pytest suite against the staged build tree, so the
package is tested from the bytes it ships.
