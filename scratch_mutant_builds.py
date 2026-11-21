import importlib.util
import os
import shutil
import sys
import tempfile

src = os.getcwd()
spec = importlib.util.spec_from_file_location("check", os.path.join(src, "tests", "check.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
panel = c.load_panel(src)
cc = os.environ.get("HOSTCC", "cc")
bad = 0
total = 0
with tempfile.TemporaryDirectory() as work:
    for kind, muts, srcs in (
        ("i86", c.MUTANTS, None),
        ("sstrun", c.MUTANTS, ["tests/sstrun.c", "cpu.c", "mem.c"]),
        ("panel", panel.MUTANTS, c.SOURCES + ["tests/panel.c", "-DGFX_MODEL"]),
    ):
        for number, (name, old, new) in enumerate(muts, 1):
            tree = os.path.join(work, "mutant")
            shutil.rmtree(tree, ignore_errors=True)
            os.mkdir(tree)
            os.mkdir(os.path.join(tree, "tests"))
            c.mutate(src, tree, number, name, old, new)
            total += 1
            try:
                c.build(tree, os.path.join(work, "out"), cc, srcs)
            except c.Failure as failure:
                bad += 1
                print("BUILD FAIL %s %d %s %r: %s" % (kind, number, name, old[:70], str(failure)[:300]))
print("mutant builds: %d tried, %d failed" % (total, bad))
sys.exit(1 if bad else 0)
