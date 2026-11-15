"""Swap traces: the JSON-lines schema and the compile-workload generator.

A trace is a sequence of swap transitions, one JSON object per line. A
board capture can replace a generated trace as long as it carries the same
fields; the model never looks at how a trace was made.

Record fields:

  op        "meta", "out", "in", "exit" or "mark"
  seq       position in the trace
  phase     "prefix" (before the first counter snapshot) or "measure"
  compile   index of the compile the record belongs to, or null
  pid, name the process and its program
  -- "out" only: one swap-out attempt, as swap_image_reserve() (fork:
     true, pid is the child whose image is a copy of the parent) or
     swap_exchange_switch() (incoming names the process swapped in by the
     same transition, or null) sees it
  data, stack, u      raw bytes of the data past clean text, the stack and
                      the u area (USIZE)
  cdata, cstack, cu   heatshrink output bytes for each of the three
  zero_sectors        4 KiB sectors of the raw flash image that are all zero
  fork, incoming
  -- "in": the process comes back into an empty window
  -- "exit": a swapped-out process exits and releases its image
  -- "mark": what == "snapshot" separates prefix from measure

The generator follows the processes the board runs (etc/rc.rp2040,
etc/daemons, etc/ttys, distrib/rp2040/cc) under the kernel's rules: a fork
reserves an image for the child (kern_fork.c newproc), the parent's wait
then swaps it out with the child as the incoming image (vm_sched.c sched),
and a process that exits leaves the window empty for the next swap-in.
Sizes are parameters; calibrate.py fits them to the board's counters.
"""

import json
import random
from dataclasses import asdict, dataclass, field, replace

USIZE = 3072
SCHEMA = 1

OUT_FIELDS = ("data", "stack", "u", "cdata", "cstack", "cu", "zero_sectors")


@dataclass(frozen=True)
class Image:
    """One program's swap image: raw and compressed sizes in bytes."""
    data: int
    stack: int
    cdata: int
    cstack: int
    cu: int
    zero_sectors: int = 0


@dataclass(frozen=True)
class Workload:
    """The compile workload of docs/research/trim-survey/board/swap/admission.

    init, rc, update and login are the boot-time processes; shell is the
    interactive login shell and driver the cc script's shell (both bin/sh);
    compiler is smlrc and command the assembler or linker, which swap out
    only when update's timer displaces them while they run.
    """
    init: Image
    rc: Image
    update: Image
    shell: Image
    driver: Image
    compiler: Image
    compiles: int = 24
    rc_commands: int = 6           # fsck, hostname, umount, mount, rm, date
    driver_commands: int = 4       # smlrc, as, ld, rm (distrib/rp2040/cc)
    update_every: int = 11         # empty-window moments between update runs
    update_phase: int = 3
    update_before_child: int = 0   # spawns between update runs ahead of a child
    command: Image = None          # as and ld, when update displaces them
    displacements: tuple = ((0, 0),)  # (compile, pass) update displaces
    jitter: int = 0                # +- bytes on compressed data per compile
    seed: int = 1
    driver_swaps: bool = True      # False: the cc driver never swaps out


@dataclass
class _Emitter:
    workload: Workload
    events: list = field(default_factory=list)
    phase: str = "prefix"
    compile: object = None
    next_pid: int = 2
    empties: int = 0
    spawns: int = 0
    rng: random.Random = None
    offsets: dict = field(default_factory=dict)

    def __post_init__(self):
        self.rng = random.Random(self.workload.seed)

    def pid(self):
        self.next_pid += 1
        return self.next_pid

    def _sizes(self, image):
        """Raw and compressed sizes of image as it stands in this compile.

        A program's data varies between compiles, never between the fork
        copy and the parent swapped out after it, which hold the same
        bytes: the offset on the compressed data is drawn once per program
        and compile and shared by every swap-out of that program in it.
        """
        j = self.workload.jitter
        key = (image, self.compile)
        if key not in self.offsets:
            self.offsets[key] = self.rng.randint(-j, j) if j else 0
        return {"data": image.data, "stack": image.stack, "u": USIZE,
                "cdata": max(0, image.cdata + self.offsets[key]),
                "cstack": image.cstack, "cu": image.cu,
                "zero_sectors": image.zero_sectors}

    def emit(self, op, **fields):
        record = {"op": op, "seq": len(self.events), "phase": self.phase,
                  "compile": self.compile}
        record.update(fields)
        self.events.append(record)

    def out(self, pid, name, image, fork=False, incoming=None):
        self.emit("out", pid=pid, name=name, fork=fork, incoming=incoming,
                  **self._sizes(image))

    def window_empty(self, parent, parent_name, parent_image, update):
        """A child exited. update runs on its schedule, else parent returns."""
        self.empties += 1
        w = self.workload
        if update is not None and \
                (self.empties + w.update_phase) % w.update_every == 0:
            self.emit("in", pid=update, name="update")
            self.out(update, "update", w.update, incoming=parent)
        else:
            self.emit("in", pid=parent, name=parent_name)
        del parent_image

    def spawn(self, parent, parent_name, parent_image, update, run=None):
        """parent forks a child, waits, and the child runs and exits."""
        child = self.pid()
        self.out(child, parent_name, parent_image, fork=True)
        self.spawns += 1
        w = self.workload
        if update is not None and self.phase == "measure" and \
                w.update_before_child and \
                (self.spawns + w.update_phase) % w.update_before_child == 0:
            # sched swaps in the process out longest: update, whose timer
            # has fired, before the child created a moment ago.
            self.out(parent, parent_name, parent_image, incoming=update)
            self.out(update, "update", w.update, incoming=child)
        else:
            self.out(parent, parent_name, parent_image, incoming=child)
        if run is not None:
            run(child)
        self.window_empty(parent, parent_name, parent_image, update)
        return child


def generate(workload):
    """Return the trace records for a workload: boot prefix, the snapshot
    mark, the compiles and the closing sysctl, as the capture ran them."""
    w = workload
    e = _Emitter(w)
    e.emit("meta", schema=SCHEMA, compiles=w.compiles,
           workload=asdict(w) | {"init": asdict(w.init)})
    init = 1
    # init runs /etc/rc under sh; rc starts its six commands.
    e.spawn(init, "init", w.init, None, run=lambda rc: [
        e.spawn(rc, "rc", w.rc, None) for _ in range(w.rc_commands)])
    # init forks update (etc/daemons) and the console getty, then waits.
    update = e.pid()
    e.out(update, "init", w.init, fork=True)
    getty = e.pid()
    e.out(getty, "init", w.init, fork=True)
    e.out(init, "init", w.init, incoming=update)
    # update runs once and sleeps; getty execs login, which execs sh.
    e.out(update, "update", w.update, incoming=getty)
    shell = getty
    # hostname; sysctl kern.version; df /, then sync, then the snapshot.
    for _ in range(4):
        e.spawn(shell, "sh", w.shell, update)
    child = e.pid()
    e.out(child, "sh", w.shell, fork=True)
    e.out(shell, "sh", w.shell, incoming=child)
    e.emit("mark", what="snapshot")
    e.phase = "measure"
    e.window_empty(shell, "sh", w.shell, update)

    for k in range(w.compiles):
        e.compile = k

        def driver_run(driver, k=k):
            names = ("smlrc", "as", "ld", "rm")
            for j in range(w.driver_commands):
                displaced = (k, j) in w.displacements

                def displace(cpid, j=j):
                    # update's timer fires while the pass runs, and sched
                    # swaps the pass out for it (vm_sched.c: in core at
                    # least a second, update out at least two).
                    image = w.compiler if j == 0 else w.command
                    e.out(cpid, names[j], image, incoming=update)
                    e.out(update, "update", w.update, incoming=cpid)
                if w.driver_swaps:
                    e.spawn(driver, "cc", w.driver, update,
                            run=displace if displaced else None)
                elif displaced:
                    displace(e.pid())
        e.spawn(shell, "sh", w.shell, update, run=driver_run)   # cc
        e.spawn(shell, "sh", w.shell, update)                   # sync
    e.compile = None
    child = e.pid()
    e.out(child, "sh", w.shell, fork=True)
    e.out(shell, "sh", w.shell, incoming=child)
    e.emit("mark", what="snapshot")
    return e.events


def validate(records):
    """Raise ValueError unless records follow the schema above."""
    if not records or records[0].get("op") != "meta" or \
            records[0].get("schema") != SCHEMA:
        raise ValueError("a trace starts with a schema 1 meta record")
    for i, r in enumerate(records):
        if r.get("seq") != i or r.get("phase") not in ("prefix", "measure"):
            raise ValueError(f"record {i}: seq or phase")
        op = r.get("op")
        if op == "out":
            for name in OUT_FIELDS + ("pid",):
                if not isinstance(r.get(name), int) or r[name] < 0:
                    raise ValueError(f"record {i}: {name}")
            if r["u"] != USIZE:
                raise ValueError(f"record {i}: u is USIZE")
            if r["fork"] and r.get("incoming") is not None:
                raise ValueError(f"record {i}: a fork copy has no incoming")
        elif op in ("in", "exit"):
            if not isinstance(r.get("pid"), int):
                raise ValueError(f"record {i}: pid")
        elif op not in ("meta", "mark"):
            raise ValueError(f"record {i}: op {op!r}")


def dump(records, path):
    with open(path, "w", encoding="utf-8") as out:
        for r in records:
            out.write(json.dumps(r, sort_keys=True) + "\n")


def load(path):
    with open(path, encoding="utf-8") as src:
        records = [json.loads(line) for line in src if line.strip()]
    validate(records)
    return records


def with_images(workload, **images):
    return replace(workload, **images)
