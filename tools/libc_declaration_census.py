"""Inventory ARM source and installed-header declarations for shared-libc review."""

import argparse
import collections
import copy
import csv
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
HEADER = re.compile(r"(?:[A-Za-z_][A-Za-z0-9_.-]*/)*[A-Za-z_][A-Za-z0-9_.-]*\.[hi]\Z")
ARCHIVES = {"libc": "libc", "libm": "libm", "libcurses": "libcurses",
            "libtermlib": "libtermcap", "libutil": "libutil", "libbn": "libbn",
            "libvmf": "libvmf", "libtcl": "libtcl", "libreadline": "libreadline",
            "libcalc": "libcalc", "libwin": "libwin"}


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
        if report.get("schema") in (2, 3):
            installed = report["staged_headers"]
            require(set(report["installed_headers"]) == {h for h in installed if "/" not in h},
                    "staged root-header denominator differs")
            verify_staged_profile(profile["staged"], installed)
            if report["schema"] == 3:
                expected = sorted(set(report["issue8_headers"]).intersection(installed),
                                  key=lambda h: ("/" in h, h))
                joint = profile["staged"]["issue8_joint"]
                require(expected and joint["headers"] == expected and joint["outcome"] == "parsed"
                        and joint["exit_code"] == 0
                        and joint["symbols"] == sorted(joint["declarations"]),
                        "installed Issue 8 declaration witness differs")


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


def staged_header_owners(root):
    environment = {k: os.environ[k] for k in ("PATH", "HOME") if k in os.environ}
    environment.update(LC_ALL="C", TZ="UTC")
    variables = ("STD", "LINKS", "SUBDIRS", "NETDIRS", "MACHDEP")
    command = ["bmake", "-C", str(root / "include"), "MACHINE=rp2040"]
    for name in variables:
        command.extend(("-V", "${" + name + "}"))
    result = subprocess.run(command, env=environment, capture_output=True,
                            text=True, check=False, timeout=30)
    values = result.stdout.splitlines()
    require(result.returncode == 0 and not result.stderr and len(values) == len(variables),
            "staged include installer query fails")
    standard, links, subdirs, netdirs, machine = (value.split() for value in values)
    require(machine == ["rp2040"] and standard, "staged include installer selection differs")
    owners = {}
    for name in standard:
        require(HEADER.fullmatch(name) and "/" not in name, "invalid root install name: " + name)
        owners[name] = "include/" + name
    aliases = {}
    for name in links:
        require(HEADER.fullmatch(name) and "/" not in name, "invalid root alias name: " + name)
        aliases[name] = "sys/" + name
        owners[name] = "sys/sys/" + name
    for directory in [*subdirs, *netdirs, *machine, "sys"]:
        require(NAME.fullmatch(directory), "invalid staged include directory")
        owner = {"rp2040": "sys/arch/rp2040/include", "sys": "sys/sys"}.get(
            directory, "include/" + directory)
        suffixes = ("*.h", "*.i") if directory in subdirs else ("*.h",)
        paths = [path for suffix in suffixes for path in (root / owner).glob(suffix)]
        require(paths, "include installer has no headers in " + owner)
        for path in paths:
            owners[directory + "/" + path.name] = path.relative_to(root).as_posix()

    result = subprocess.run(
        ["bmake", "-C", str(root / "lib/libreadline"), "MACHINE=rp2040", "-V", "${HDRS}"],
        env=environment, capture_output=True, text=True, check=False, timeout=30)
    readline = result.stdout.split()
    require(result.returncode == 0 and not result.stderr and readline,
            "readline header installer query fails")
    for name in readline:
        require(HEADER.fullmatch(name) and Path(name).parent == Path("readline"),
                "invalid readline install name")
        owners[name] = "lib/libreadline/" + name
    return owners, aliases


def staged_header_snapshot(staged, owners, aliases, hashes):
    require(staged.is_dir() and staged == staged.resolve(),
            "staged include directory is missing or follows a symlink")
    observed = set()
    expected_links = {**aliases, "machine": "rp2040"}
    actual_links = {}
    for path in staged.rglob("*"):
        name = path.relative_to(staged).as_posix()
        if path.is_symlink():
            actual_links[name] = os.readlink(path)
            require(expected_links.get(name) == actual_links[name],
                    "unexpected installed symlink: " + name)
        if path.is_file():
            observed.add(name)
        else:
            require(path.is_dir(), "unexpected installed include entry: " + name)
    require(actual_links == expected_links, "installed header aliases differ")
    missing = sorted(owners.keys() - observed)
    extra = sorted(observed - owners.keys())
    require(not missing and not extra,
            f"installed header paths differ: missing={missing}, extra={extra}")
    rows = {}
    for name, source in sorted(owners.items()):
        path = staged / name
        require(path.resolve().is_relative_to(staged), "installed header leaves staging: " + name)
        content = digest(path)
        require(hashes.get(source) == content, "installed header bytes differ: " + name)
        rows[name] = {"source": source, "sha256": content, "alias_of": aliases.get(name)}
        if name.startswith("rp2040/"):
            rows["machine/" + path.name] = {**rows[name], "alias_of": name}
    return rows


def header_snapshot(root):
    names = git(root, "ls-files", "-z", "*.h", "*.i").split("\0")
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


def compile_headers(root, work, compiler, selectors, headers, label, *,
                    include_dir=None, record_rejection=False):
    require(headers and all(HEADER.fullmatch(h) and ".." not in h for h in headers),
            "invalid header spelling")
    # dependency_paths() resolves symlinks; a root spelled through one (macOS
    # temp directories under /var -> /private/var) must compare in that form.
    root = root.resolve()
    source = "".join(f"#include <{header}>\n" for header in headers)
    dependency = work / (label + ".d")
    include = root / "include" if include_dir is None else include_dir
    arguments = [compiler, *FLAGS, "-I" + str(include), *selectors,
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
    diagnostics = result.stderr.decode(errors="replace")
    parsed = result.returncode == 0 and not diagnostics
    located_error = rf"^(?:{re.escape(str(include))}/[^:\n]+|<stdin>):\d+:\d+: (?:fatal )?error:"
    require(parsed or (record_rejection and result.returncode == 1
                       and re.search(located_error, diagnostics, re.MULTILINE)),
            f"compiler rejects {label}: " + diagnostics)
    ast = json.loads(result.stdout)
    require(isinstance(ast, dict) and ast.get("kind") == "TranslationUnitDecl",
            "malformed compiler AST: " + label)
    observed = declarations(ast) if parsed else {}
    dependencies = {}
    for path in dependency_paths(dependency, root) if parsed or dependency.exists() else ():
        require(path.is_relative_to(root), "compiler dependency leaves repository")
        dependencies[path.relative_to(root).as_posix()] = digest(path)
    require(not parsed or dependencies, "compiler dependency denominator is empty")
    return observed, {"headers": headers,
                      "outcome": "parsed" if parsed else "rejected",
                      "exit_code": result.returncode,
                      "diagnostics_file": label + ".stderr",
                      "diagnostics_sha256": hashlib.sha256(result.stderr).hexdigest(),
                      "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                      "ast_sha256": hashlib.sha256(result.stdout).hexdigest(),
                      "ast_file": label + ".ast.json.gz",
                      "dependencies": dependencies, "symbols": sorted(observed)}


def verify_staged_profile(profile, installed):
    standalone = profile["standalone"]
    require(set(standalone) == set(installed), "staged header exposure denominator differs")
    for name, row in standalone.items():
        require(row["headers"] == [name], "staged header witness differs: " + name)
    joint = profile["joint"]
    accepted = sorted((name for name, row in standalone.items() if row["outcome"] == "parsed"),
                      key=lambda name: ("/" in name, name))
    require(accepted and joint["headers"] == accepted, "staged joint header denominator differs")
    for row in [*standalone.values(), joint]:
        require(row["outcome"] in {"parsed", "rejected"}, "invalid staged parse outcome")
        if row["outcome"] == "rejected":
            require(row["exit_code"] == 1 and not row["symbols"] and not row["declarations"],
                    "rejected staged parse contributes declarations")
        else:
            require(row["exit_code"] == 0 and row["symbols"] == sorted(row["declarations"]),
                    "staged declaration witness denominator differs")


def issue8_header_names(ledger):
    names = set()
    with ledger.open() as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            name = row["name"]
            if row["kind"] != "base-definition" or not name.endswith(".h"):
                continue
            name = re.sub(r"^(arpa|net|netinet|sys)_", r"\1/", name)
            require(HEADER.fullmatch(name) and name not in names,
                    "invalid Issue 8 header ledger entry: " + name)
            names.add(name)
    require(names, "Issue 8 header ledger is empty")
    return names


def observe_staged_profile(root, work, compiler, selectors, label, staged, installed, graphs,
                           issue8_headers):
    standalone = {}
    for number, name in enumerate(sorted(installed)):
        observed, witness = compile_headers(
            root, work, compiler, selectors, [name], f"{label}.staged-{number}",
            include_dir=staged, record_rejection="/" in name)
        for row in observed.values():
            row["provider"] = provider_observation(row, graphs)
        witness["declarations"] = observed
        standalone[name] = witness

    accepted = sorted((name for name, row in standalone.items() if row["outcome"] == "parsed"),
                      key=lambda name: ("/" in name, name))
    observed, joint = compile_headers(
        root, work, compiler, selectors, accepted, label + ".staged-joint",
        include_dir=staged, record_rejection=True)
    for row in observed.values():
        row["provider"] = provider_observation(row, graphs)
    joint["declarations"] = observed
    selected = sorted(issue8_headers.intersection(installed), key=lambda h: ("/" in h, h))
    require(selected, "installed Issue 8 header set is empty")
    observed, user_joint = compile_headers(
        root, work, compiler, selectors, selected, label + ".issue8-headers",
        include_dir=staged)
    for row in observed.values():
        row["provider"] = provider_observation(row, graphs)
    user_joint["declarations"] = observed
    result = {"standalone": standalone, "joint": joint, "issue8_joint": user_joint}
    verify_staged_profile(result, installed)
    for witness in [*standalone.values(), joint, user_joint]:
        for name, value in witness["dependencies"].items():
            path = root / name
            require(path.is_relative_to(staged), "compiler dependency leaves staged headers")
            header = path.relative_to(staged).as_posix()
            require(installed.get(header, {}).get("sha256") == value,
                    "compiler dependency lacks staged header ownership: " + header)
    return result


def run(root, work, compiler, archive_paths, archiver, staged):
    controls = calibrate_compiler(root, work, compiler)
    before = header_snapshot(root)
    headers = sorted(Path(name).name for name in before
                     if Path(name).parent == Path("include"))
    require(headers, "root-header denominator is empty")
    installed = installed_headers(root, headers)
    ledger = "docs/research/dpucp-2024-requirements.tsv"
    owner_inputs = {name: digest(root / name) for name in
                    ("lib/Makefile", "include/Makefile", "lib/libreadline/Makefile",
                     "share/mk/sys.mk", ledger)}
    issue8_headers = issue8_header_names(root / ledger)
    owners, aliases = staged_header_owners(root)
    staged_headers = staged_header_snapshot(staged, owners, aliases, before)
    require(set(installed) == {name for name in staged_headers if "/" not in name},
            "source and staged root-header selection differs")
    require((root / "include/machine").resolve() == root / "sys/arch/rp2040/include",
            "include/machine does not select RP2040")
    revision = git(root, "rev-parse", "HEAD")
    require(not git(root, "diff", "HEAD", "--", "*.h", "*.i"), "tracked headers have edits")
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
            profiles[profile]["staged"] = observe_staged_profile(
                root, work, compiler, selectors, profile, staged, staged_headers, graphs,
                issue8_headers)
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
    require(staged_headers == staged_header_snapshot(staged, owners, aliases, before),
            "staged header inputs change during census")
    report = {"schema": 3, "issue8_headers": sorted(issue8_headers),
              "source_commit": revision, "headers": headers,
              "staged_headers": staged_headers,
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
            "scope": "Source root-header and staged include-tree declarations; "
                     "every ABI selection remains unreviewed.",
            "remaining": ["Kernel/user declaration domains, context-requiring headers and macros",
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
    parser.add_argument("--destdir", type=Path, default=Path("distrib/obj/destdir.rp2040"))
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
        destdir = (root / args.destdir).resolve()
        staged = destdir / "usr/include"
        require(staged.is_relative_to(root) and staged.resolve().is_relative_to(root),
                "staged include directory leaves repository")
        report = run(root, work, compiler, archive_paths(root), archiver, staged)
        text = json.dumps(report, indent=2, sort_keys=True) + "\n"
        (work / "receipt.json").write_text(text)
        if args.output:
            output = (root / args.output).resolve()
            require(output.is_relative_to(root), "output leaves repository")
            with output.open("x") as handle:
                handle.write(text)
        print(f"PASS declaration census: {len(report['headers'])} root headers; "
              f"{len(report['staged_headers'])} installed spellings; "
              f"{len(report['profiles'])} profile/time pairs; ABI selection remains unreviewed")
        print("Evidence: " + work.relative_to(root).as_posix())
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL declaration census: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
