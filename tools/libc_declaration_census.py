"""Inventory ARM root-header declarations without selecting a shared-library ABI."""

import argparse
import collections
import copy
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

from c17.record_tool import dependency_paths, digest
from libc_shared_closure import provider, read_archive

MODES = {"default": [], "iso": ["-D_ANSI_SOURCE=1"],
         "issue7": ["-D_POSIX_C_SOURCE=200809L"],
         "issue8": ["-D_POSIX_C_SOURCE=202405L"]}
FLAGS = ["--target=arm-none-eabi", "-mcpu=cortex-m0plus", "-mthumb",
         "-mfloat-abi=soft", "-std=c17", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-nostdinc"]
NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
HEADER = re.compile(r"[A-Za-z_][A-Za-z0-9_.-]*\.h\Z")
ARCHIVES = {"libc": "libc", "libm": "libm", "libcurses": "libcurses",
            "libtermlib": "libtermcap", "libutil": "libutil", "libbn": "libbn",
            "libvmf": "libvmf", "libtcl": "libtcl", "libreadline": "libreadline",
            "libcalc": "libcalc"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def declarations(tree):
    require(isinstance(tree, dict) and tree.get("kind") == "TranslationUnitDecl"
            and isinstance(tree.get("inner"), list), "malformed compiler AST")
    result = {}
    for node in tree["inner"]:
        require(isinstance(node, dict), "malformed AST declaration")
        kind = {"FunctionDecl": "function", "VarDecl": "object"}.get(node.get("kind"))
        if kind is None or node.get("isImplicit") or node.get("storageClass") == "static":
            continue
        name, linkage = node.get("name"), node.get("mangledName")
        require(isinstance(name, str) and NAME.fullmatch(name), "invalid declaration name")
        require(isinstance(linkage, str) and linkage and "\0" not in linkage,
                "missing declaration linkage: " + name)
        spelling = node.get("type", {}).get("qualType")
        require(isinstance(spelling, str) and spelling, "missing declaration type: " + name)
        children = node.get("inner", [])
        require(isinstance(children, list) and all(isinstance(c, dict) for c in children),
                "malformed declaration children: " + name)
        parameters = [c.get("type", {}).get("qualType") for c in children
                      if c.get("kind") == "ParmVarDecl"]
        require(all(isinstance(p, str) and p for p in parameters),
                "missing parameter type: " + name)
        attributes = {c["kind"] for c in children if c.get("kind", "").endswith("Attr")}
        row = result.setdefault(name, {"kind": kind, "linkage": linkage, "types": [],
                                      "parameters": [], "attributes": []})
        require(row["kind"] == kind and row["linkage"] == linkage,
                "conflicting declaration kind or linkage: " + name)
        if spelling not in row["types"]:
            row["types"].append(spelling)
        if kind == "function" and parameters not in row["parameters"]:
            row["parameters"].append(parameters)
        row["attributes"] = sorted(set(row["attributes"]) | attributes)
    return dict(sorted(result.items()))


def provider_observation(declaration, graphs):
    matches = []
    for archive, graph in sorted(graphs.items()):
        member = provider(graph, declaration["linkage"])
        if member is not None:
            definition = graph["members"][member]["definitions"][declaration["linkage"]]
            matches.append({"archive": archive, "member": member, "kind": definition["kind"],
                            "size": definition["size"], "mutable": definition["mutable"],
                            "binding": definition["binding"]})
    if not matches:
        state = "unprovided"
        action = "Resolve startup/compiler ownership or implement the provider."
    elif any(m["kind"] != declaration["kind"] for m in matches):
        state, action = "kind-mismatch", "Reconcile the declaration and compiled provider kind."
    elif len(matches) > 1:
        state, action = "multiple-providers", "Select and justify the archive provider owner."
    else:
        state, action = "matched", "Review API eligibility, full ABI and semantic witnesses."
    return {"state": state, "providers": matches, "next_action": action}


def reconcile(headers, exposures, joint, installed=None, installed_joint=None):
    installed = headers if installed is None else installed
    installed_joint = joint if installed_joint is None else installed_joint
    require(len(headers) == len(set(headers)) and set(headers) == set(exposures),
            "root-header exposure denominator differs")
    result = copy.deepcopy(joint)
    for rows in [*exposures.values(), installed_joint]:
        for name, row in rows.items():
            target = result.setdefault(name, copy.deepcopy(row))
            require((target["kind"], target["linkage"]) == (row["kind"], row["linkage"]),
                    "joint declaration kind/linkage differs: " + name)
            for field in ("types", "parameters", "attributes"):
                for value in row[field]:
                    if value not in target[field]:
                        target[field].append(value)
    for name, row in result.items():
        row["exposed_by"] = sorted(h for h in headers if name in exposures[h])
        for header in row["exposed_by"]:
            other = exposures[header][name]
            require((other["kind"], other["linkage"]) == (row["kind"], row["linkage"]),
                    "joint declaration kind/linkage differs: " + name)
        row["abi_selection"] = "unreviewed"
        row["exposure"] = (("standalone-only" if row["exposed_by"] else "installed-joint-only")
                           if name not in joint else
                           "joint-only" if not row["exposed_by"] else "both")
        row["installed_exposed_by"] = sorted(set(row["exposed_by"]) & set(installed))
        row["installed_joint_observed"] = name in installed_joint
    return dict(sorted(result.items()))


def verify_profile(headers, profile):
    require(set(profile["headers"]) == set(headers), "header exposure denominator differs")
    installed = profile["installed_headers"]
    require(installed and len(installed) == len(set(installed)) and set(installed) <= set(headers),
            "installed header denominator differs")
    witnesses = [*profile["headers"].values(), profile["joint"], profile["installed_joint"]]
    for witness in witnesses:
        names = witness["symbols"]
        require(isinstance(names, list) and len(names) == len(set(names))
                and all(isinstance(n, str) and NAME.fullmatch(n) for n in names),
                "invalid declaration witness symbols")
    expected = set().union(*(set(w["symbols"]) for w in witnesses))
    rows = profile["declarations"]
    require(set(rows) == expected, "declaration witness denominator differs")
    for name, row in rows.items():
        exposing = sorted(h for h in headers if name in profile["headers"][h]["symbols"])
        exposure = (("standalone-only" if exposing else "installed-joint-only")
                    if name not in profile["joint"]["symbols"] else
                    "joint-only" if not exposing else "both")
        require(row["exposed_by"] == exposing and row["exposure"] == exposure,
                "declaration exposure differs: " + name)
        require(row["installed_exposed_by"] == sorted(set(exposing) & set(installed))
                and row["installed_joint_observed"] ==
                (name in profile["installed_joint"]["symbols"]),
                "installed declaration exposure differs: " + name)
        require(row["abi_selection"] == "unreviewed" and row.get("next_action"),
                "ABI selection lacks unreviewed disposition: " + name)
        observation = row["provider"]
        providers = observation["providers"]
        state = observation["state"]
        mismatch = any(p["kind"] != row["kind"] for p in providers)
        expected_state = ("unprovided" if not providers else "kind-mismatch" if mismatch else
                          "multiple-providers" if len(providers) > 1 else "matched")
        require(state == expected_state, "provider disposition differs: " + name)
    counts = dict(collections.Counter(row["provider"]["state"] for row in rows.values()))
    require(profile["provider_counts"] == counts, "provider counts differ from rows")


def verify_report(report):
    headers = report["headers"]
    require(headers and len(headers) == len(set(headers)), "root-header denominator differs")
    profiles = {f"{mode}.time{width}": [*flags, f"-D_TIME_BITS={width}"]
                for mode, flags in MODES.items() for width in (32, 64)}
    require(set(report["profiles"]) == set(profiles), "profile/time denominator differs")
    require(set(report["archives"]) == set(ARCHIVES.values()), "archive denominator differs")
    for name, profile in report["profiles"].items():
        require(profile["selectors"] == profiles[name], "profile/time selectors differ")
        require(profile["installed_headers"] == report["installed_headers"],
                "installed header denominator differs between profiles")
        verify_profile(headers, profile)


def git(root, *arguments):
    return subprocess.check_output(["git", "-c", "core.fsmonitor=false", "-C", str(root),
                                    *arguments], text=True).strip()


def archive_paths(root):
    environment = {k: os.environ[k] for k in ("PATH", "HOME") if k in os.environ}
    environment.update(LC_ALL="C", TZ="UTC")
    result = subprocess.run(["bmake", "-C", str(root / "lib"), "MACHINE=rp2040",
                             "-V", "${SUBDIR}"], env=environment, capture_output=True,
                            text=True, check=False, timeout=30)
    require(result.returncode == 0 and not result.stderr, "library owner query fails")
    owners = result.stdout.split()
    require(len(owners) == len(set(owners))
            and set(owners) == set(ARCHIVES) | {"startup-arm", "libc_aout"},
            "library owner denominator changes")
    return {name: root / "lib" / (name + ".a") for name in ARCHIVES.values()}


def installed_headers(root, headers):
    environment = {k: os.environ[k] for k in ("PATH", "HOME") if k in os.environ}
    environment.update(LC_ALL="C", TZ="UTC")
    result = subprocess.run(["bmake", "-C", str(root / "include"), "MACHINE=rp2040",
                             "-V", "${STD} ${LINKS}"], env=environment, capture_output=True,
                            text=True, check=False, timeout=30)
    require(result.returncode == 0 and not result.stderr, "installed root-header query fails")
    selected = sorted(set(result.stdout.split()))
    require(selected and set(selected) <= set(headers), "installed header denominator differs")
    return selected


def header_snapshot(root):
    names = git(root, "ls-files", "-z", "*.h").split("\0")
    result = {}
    for name in names:
        if not name:
            continue
        path = root / name
        require(path.resolve().is_relative_to(root), "tracked header leaves repository: " + name)
        result[name] = digest(path)
    require(result, "tracked header denominator is empty")
    return result


def calibrate_compiler(root, work, compiler):
    cases = [("accept", "int declaration_census_control(int);\n"),
             ("reject", '#error "declaration census rejection control"\n')]
    controls = {}
    for label, source in cases:
        result = subprocess.run([compiler, *FLAGS, "-Xclang", "-ast-dump=json",
                                 "-fsyntax-only", "-x", "c", "-"], input=source.encode(),
                                capture_output=True, cwd=root, timeout=30, check=False)
        (work / ("compiler-" + label + ".c")).write_text(source)
        (work / ("compiler-" + label + ".stdout")).write_bytes(result.stdout)
        (work / ("compiler-" + label + ".stderr")).write_bytes(result.stderr)
        if label == "accept":
            require(result.returncode == 0 and not result.stderr,
                    "compiler accepting control fails")
            require(set(declarations(json.loads(result.stdout))) == {"declaration_census_control"},
                    "compiler accepting control loses its declaration")
        else:
            require(result.returncode != 0
                    and b"declaration census rejection control" in result.stderr,
                    "compiler rejection control fails")
        controls[label] = {"exit_code": result.returncode,
                           "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                           "stdout_sha256": hashlib.sha256(result.stdout).hexdigest(),
                           "stderr_sha256": hashlib.sha256(result.stderr).hexdigest()}
    return controls


def compile_headers(root, work, compiler, selectors, headers, label):
    require(headers and all(HEADER.fullmatch(h) and ".." not in h for h in headers),
            "invalid root-header spelling")
    # dependency_paths() resolves symlinks; a root spelled through one (macOS
    # temp directories under /var -> /private/var) must compare in that form.
    root = root.resolve()
    source = "".join(f"#include <{header}>\n" for header in headers)
    dependency = work / (label + ".d")
    arguments = [compiler, *FLAGS, "-I" + str(root / "include"), *selectors,
                 "-MMD", "-MF", str(dependency), "-Xclang", "-ast-dump=json",
                 "-fsyntax-only", "-x", "c", "-"]
    environment = {k: os.environ[k] for k in ("PATH", "HOME") if k in os.environ}
    environment.update(LC_ALL="C", TZ="UTC")
    result = subprocess.run(arguments, input=source.encode(), capture_output=True,
                            cwd=root, env=environment, timeout=30, check=False)
    (work / (label + ".c")).write_text(source)
    (work / (label + ".ast.json.gz")).write_bytes(
        gzip.compress(result.stdout, compresslevel=6, mtime=0))
    (work / (label + ".stderr")).write_bytes(result.stderr)
    require(result.returncode == 0 and not result.stderr,
            f"compiler rejects {label}: " + result.stderr.decode(errors="replace"))
    observed = declarations(json.loads(result.stdout))
    dependencies = {}
    for path in dependency_paths(dependency, root):
        require(path.is_relative_to(root), "compiler dependency leaves repository")
        dependencies[path.relative_to(root).as_posix()] = digest(path)
    require(dependencies, "compiler dependency denominator is empty")
    return observed, {"headers": headers,
                      "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                      "ast_sha256": hashlib.sha256(result.stdout).hexdigest(),
                      "ast_file": label + ".ast.json.gz",
                      "dependencies": dependencies, "symbols": sorted(observed)}


def run(root, work, compiler, archive_paths, archiver):
    controls = calibrate_compiler(root, work, compiler)
    before = header_snapshot(root)
    headers = sorted(Path(name).name for name in before
                     if Path(name).parent == Path("include"))
    require(headers, "root-header denominator is empty")
    installed = installed_headers(root, headers)
    owner_inputs = {name: digest(root / name) for name in
                    ("lib/Makefile", "include/Makefile")}
    require((root / "include/machine").resolve() == root / "sys/arch/rp2040/include",
            "include/machine does not select RP2040")
    revision = git(root, "rev-parse", "HEAD")
    require(not git(root, "diff", "HEAD", "--", "*.h"), "tracked headers have edits")
    graphs = {name: read_archive(path, archiver) for name, path in archive_paths.items()}
    identities = {name: graph["archive_sha256"] for name, graph in graphs.items()}
    profiles = {}
    for mode, feature_flags in MODES.items():
        for width in (32, 64):
            profile = f"{mode}.time{width}"
            selectors = [*feature_flags, f"-D_TIME_BITS={width}"]
            exposures, witnesses = {}, {}
            for header in headers:
                exposures[header], witnesses[header] = compile_headers(
                    root, work, compiler, selectors, [header], profile + "." + header)
            joint, witness = compile_headers(root, work, compiler, selectors, headers,
                                              profile + ".joint")
            installed_joint, installed_witness = compile_headers(
                root, work, compiler, selectors, installed, profile + ".installed-joint")
            rows = reconcile(headers, exposures, joint, installed, installed_joint)
            for row in rows.values():
                row["provider"] = provider_observation(row, graphs)
                row["next_action"] = row["provider"]["next_action"]
                if row["exposure"] != "both":
                    row["next_action"] = ("Reconcile standalone/joint exposure. "
                                          + row["next_action"])
            profiles[profile] = {"selectors": selectors, "headers": witnesses,
                                 "joint": witness, "installed_headers": installed,
                                 "installed_joint": installed_witness, "declarations": rows,
                                 "provider_counts": dict(collections.Counter(
                                     row["provider"]["state"] for row in rows.values()))}
            verify_profile(headers, profiles[profile])
    require(before == header_snapshot(root) and revision == git(root, "rev-parse", "HEAD"),
            "header inputs or revision change during census")
    require(all(digest(archive_paths[name]) == value for name, value in identities.items()),
            "archive bytes change during census")
    require(all(digest(root / name) == value for name, value in owner_inputs.items()),
            "header/library owner inputs change during census")
    for profile in profiles.values():
        for witness in [*profile["headers"].values(), profile["joint"], profile["installed_joint"]]:
            require(all(before.get(name) == value
                        for name, value in witness["dependencies"].items()),
                    "compiler dependencies lack matching tracked header ownership")
    report = {"schema": 1, "source_commit": revision, "headers": headers,
            "compiler_controls": controls,
            "installed_headers": installed, "owner_inputs_sha256": owner_inputs,
            "tools_sha256": {name: digest(Path(__file__).parent / name) for name in
                             ("libc_declaration_census.py", "libc_shared_closure.py",
                              "c17/record_tool.py")},
            "compiler": {"version": subprocess.check_output([compiler, "--version"],
                                                            text=True).splitlines()[0],
                         "sha256": digest(Path(compiler).resolve()), "flags": FLAGS},
            "archiver": {"version": subprocess.check_output([archiver, "--version"],
                                                            text=True).splitlines()[0],
                         "sha256": digest(Path(archiver).resolve())},
            "archives": identities,
            "archive_owners": ARCHIVES,
            "archive_source_binding": "Artifact hashes only; clean build receipts bind source "
                                      "separately.",
            "profiles": profiles,
            "scope": "Tracked root-header declarations and transitive exposure; every ABI "
                     "selection remains unreviewed.",
            "remaining": ["Public subdirectory headers and macro interfaces",
                          "Native a.out libraries, startup objects and compiler providers",
                          "Complete attributes, layouts, native input and semantic witnesses",
                          "Reviewed export selection, stable slots and shared runtime admission"]}
    verify_report(report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--ar", default="arm-none-eabi-ar")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        require(Path(git(root, "rev-parse", "--show-toplevel")).resolve() == root,
                "root must name a repository checkout")
        compiler = shutil.which(args.clang)
        require(compiler, "Clang is required")
        archiver = shutil.which(args.ar)
        require(archiver, "ARM archiver is required")
        parent = root / ".build-logs"
        require(parent.resolve().is_relative_to(root), "evidence directory leaves repository")
        parent.mkdir(exist_ok=True)
        work = parent / ("libc-declaration-census-" + uuid.uuid4().hex)
        work.mkdir()
        report = run(root, work, compiler, archive_paths(root), archiver)
        text = json.dumps(report, indent=2, sort_keys=True) + "\n"
        (work / "receipt.json").write_text(text)
        if args.output:
            output = (root / args.output).resolve()
            require(output.is_relative_to(root), "output leaves repository")
            with output.open("x") as handle:
                handle.write(text)
        print(f"PASS declaration census: {len(report['headers'])} root headers; "
              f"{len(report['profiles'])} profile/time pairs; ABI selection remains unreviewed")
        print("Evidence: " + work.relative_to(root).as_posix())
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL declaration census: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
