"""Build the shipped swap-tier C into one host shared object and load it.

The objects are the tree's own sources, compiled unmodified:

- sys/arch/rp2040/rp2040/swapram_pool.c  (the pool allocator)
- sys/arch/rp2040/rp2040/swapram.c       (admission, put, commit, swapin)
- sys/arch/rp2040/dev/flash_swap.c       (raw-swap append and rewrite)
- sys/kern/subr_rmap.c                   (the swap map ring allocator)
- the heatshrink encoder and the port's hsx decoder

swapram.c and subr_rmap.c see the stand-in kernel headers of
sys/arch/rp2040/test/swapram/shim, as that directory's evacuation test
compiles them; glue.c supplies the remaining kernel symbols. cffi loads the
result in ABI mode (cffi.FFI.dlopen), and the load checks every structure
size the cdef assumes against the compiled one.

A mutant run passes replacement source paths in ``overrides``; the object
name carries a hash of every input, so a mutant never reuses a clean build.
"""

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

import cffi

HERE = Path(__file__).resolve().parent
TOP = HERE.parents[2]
ARCH = TOP / "sys" / "arch" / "rp2040"
SHIM = ARCH / "test" / "swapram" / "shim"
HS = ARCH / "heatshrink"

SOURCES = {
    "pool": ARCH / "rp2040" / "swapram_pool.c",
    "swapram": ARCH / "rp2040" / "swapram.c",
    "flash_swap": ARCH / "dev" / "flash_swap.c",
    "rmap": TOP / "sys" / "kern" / "subr_rmap.c",
    "encoder": HS / "heatshrink_encoder.c",
    "decoder": ARCH / "rp2040" / "hsx_decoder.c",
    "glue": HERE / "glue.c",
}

BASE = ["-std=gnu17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror"]

CDEF = """
struct swapram_seg { unsigned int s_off; unsigned int s_size; };
struct swapram_pool {
    unsigned char *p_base;
    unsigned int p_size;
    unsigned int p_nseg;
    unsigned int p_used;
    struct swapram_seg *p_seg;
};
void swapram_pool_init(struct swapram_pool *, unsigned char *, unsigned int,
    struct swapram_seg *, unsigned int);
int swapram_pool_alloc(struct swapram_pool *, unsigned int, unsigned int *);
int swapram_pool_free(struct swapram_pool *, unsigned int, unsigned int);
int swapram_pool_trim(struct swapram_pool *, unsigned int, unsigned int,
    unsigned int);
unsigned int swapram_pool_avail(struct swapram_pool *);
unsigned int swapram_pool_largest(struct swapram_pool *);

struct flash_swap_ops {
    int (*read)(unsigned int, unsigned char *, unsigned int);
    int (*erase)(unsigned int, unsigned int);
    int (*program)(unsigned int, const unsigned char *, unsigned int);
};
int flash_swap_append(const struct flash_swap_ops *, unsigned int,
    const unsigned char *, unsigned int, unsigned char *);
int flash_swap_rewrite(const struct flash_swap_ops *, unsigned int,
    const unsigned char *, unsigned int, unsigned int, unsigned char *);

unsigned int glue_layout(int);
const char *glue_panic_message(void);
int glue_map_reset(size_t, size_t, size_t);
long glue_map_alloc(size_t, size_t, size_t, size_t[3]);
int glue_map_free(size_t, size_t);
int glue_map_runs(size_t *, size_t *, int);
size_t glue_map_cursor(void);
int glue_map_take_tail(size_t, size_t, size_t);
int glue_swapram_admit(int, unsigned char *, size_t, unsigned char *, size_t,
    unsigned char *);
int glue_swapram_restore(int, unsigned char *, unsigned char *,
    unsigned char *);
long glue_swapram_largest(void);
long glue_swapram_spool(unsigned int);
int glue_swapram_spool_free(unsigned int, unsigned int);
long glue_hs_count(const unsigned char *, size_t);
"""

GLUE_PANIC = -1000


class Unavailable(RuntimeError):
    """A host tool the build needs is missing; the gate reports a skip."""


def _flags(name, include):
    kernel = [
        "-Wno-deprecated-non-prototype",
        "-DSWAPRAM_KB=16",
        "-I" + str(SHIM),
        "-idirafter",
        str(include),
        "-I" + str(HS),
    ]
    if name == "flash_swap":
        return BASE + ["-I" + str(TOP / "sys" / "arch")]
    if name in ("swapram", "rmap"):
        return BASE + kernel + ["-Wno-format"]
    if name == "encoder":
        return BASE + kernel + ["-Wno-implicit-fallthrough"]
    return BASE + kernel


def build(overrides=None, build_dir=None):
    """Compile every source and link; return the shared object path."""
    sources = dict(SOURCES)
    sources.update({k: Path(v) for k, v in (overrides or {}).items()})
    cc = os.environ.get("CC", "cc")
    if shutil.which(cc) is None:
        raise Unavailable(f"C compiler {cc!r} not found on PATH")
    root = Path(build_dir or os.environ.get("SWAP_TIER_MODEL_BUILD", HERE / "build"))
    include = root / "include"
    include.mkdir(parents=True, exist_ok=True)
    machine = include / "machine"
    if not machine.exists():
        machine.symlink_to(ARCH / "include")
    digest = hashlib.sha256(cc.encode())
    for name in sorted(sources):
        digest.update(name.encode())
        digest.update(sources[name].read_bytes())
        digest.update(" ".join(_flags(name, include)).encode())
    headers = (
        sorted((ARCH / "include").glob("*.h"))
        + sorted((ARCH / "dev").glob("*.h"))
        + sorted(HS.glob("*.h"))
        + sorted(SHIM.rglob("*.h"))
        + [TOP / "sys" / "sys" / "storage_stats.h"]
    )
    for header in headers:
        digest.update(header.read_bytes())
    tag = digest.hexdigest()[:16]
    target = root / f"swaptier-{tag}.so"
    if target.exists():
        return target
    objects = []
    for name, path in sorted(sources.items()):
        obj = root / f"{name}-{tag}.o"
        cmd = [cc, *_flags(name, include), "-c", "-o", str(obj), str(path)]
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"compile {name} failed:\n{result.stderr}")
        objects.append(str(obj))
    result = subprocess.run(
        [cc, "-shared", "-o", str(target), *objects], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError(f"link failed:\n{result.stderr}")
    return target


_LOADED = {}


def env_overrides():
    """Replacement sources named by SWAP_TIER_MODEL_OVERRIDES (a JSON object
    of source key to path), which mutants.py sets for a mutant run."""
    text = os.environ.get("SWAP_TIER_MODEL_OVERRIDES", "")
    if not text:
        return {}
    value = json.loads(text)
    if not isinstance(value, dict) or not set(value) <= set(SOURCES):
        raise ValueError("SWAP_TIER_MODEL_OVERRIDES names an unknown source")
    return value


def load(overrides=None):
    """Return (ffi, lib) for the given overrides, building when needed."""
    if overrides is None:
        overrides = env_overrides()
    key = tuple(sorted((k, str(v)) for k, v in (overrides or {}).items()))
    if key in _LOADED:
        return _LOADED[key]
    path = build(overrides)
    ffi = cffi.FFI()
    ffi.cdef(CDEF)
    lib = ffi.dlopen(str(path))
    expected = {
        0: ffi.sizeof("struct swapram_pool"),
        1: ffi.sizeof("struct swapram_seg"),
        3: 2 * ffi.sizeof("size_t"),
    }
    for what, size in expected.items():
        if lib.glue_layout(what) != size:
            raise RuntimeError(f"layout {what}: C {lib.glue_layout(what)} against cdef {size}")
    _LOADED[key] = (ffi, lib)
    return ffi, lib


def constants(lib):
    """The compiled constants the model uses, read from the object."""
    names = [
        "pool_bytes",
        "seg_bytes",
        "map_bytes",
        "mapent_bytes",
        "grain",
        "swapram_kb",
        "nproc",
        "usize",
        "align",
        "map_slots",
        "bsize",
    ]
    return {name: lib.glue_layout(i) for i, name in enumerate(names)}
