#!/usr/bin/env python3
"""
Migration helper for the distributed primes server data files.

  python3 migrate.py block-size OLD NEW      re-slice progress.json for a new BLOCK_SIZE
  python3 migrate.py rename-client OLD NEW   rename a client id in clients.json and progress.json

Options (before the subcommand):
  --data-dir DIR   folder holding clients.json / progress.json (default: data)
  --dry-run        show what would change without writing anything

STOP THE SERVER FIRST. It rewrites both files every few seconds and again on
shutdown, which would overwrite a migration done while it is running.
A timestamped .bak copy of every file is made before it is changed.
"""

import argparse
import json
import shutil
import sys
import time
from bisect import bisect_right
from pathlib import Path


def load(path):
    with open(path) as f:
        return json.load(f)


def save(path, data):
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w") as f:
        json.dump(data, f, indent=4)
    tmp.replace(path)


def backup(path):
    dest = path.with_name(f"{path.name}.{time.strftime('%Y%m%d-%H%M%S')}.bak")
    shutil.copy2(path, dest)
    return dest


def merge(intervals):
    """Merge half-open [start, end) intervals."""
    out = []
    for s, e in sorted(intervals):
        if out and s <= out[-1][1]:
            out[-1][1] = max(out[-1][1], e)
        else:
            out.append([s, e])
    return out


def reslice_entry(entry, old, new):
    """Convert one algorithm's progress entry from block size `old` to `new`.

    A new block counts as completed only if every pattern in it was covered by
    completed old blocks. Every other block below the old frontier of assigned
    work is put back as a released open block (client "", assigned_at 0), so the
    server's assign_block hands it out again, lowest first. Blocks that were
    open at the time lose their owner; their clients will get a 409 on
    completion and simply ask for new work.
    """
    low = entry.get("completed_below", 0)
    intervals = []
    if low > 0:
        intervals.append((0, low * old))
    for b in entry.get("completed", []):
        intervals.append((b * old, (b + 1) * old))

    merged = merge(intervals)
    starts = [s for s, _ in merged]

    def covered(a, b):
        i = bisect_right(starts, a) - 1
        return i >= 0 and merged[i][0] <= a and merged[i][1] >= b

    next_old = entry.get("next_block", 0)
    next_new = -(-(next_old * old) // new)  # ceil

    completed, released = [], {}
    first_gap = None
    for n in range(next_new):
        if covered(n * new, (n + 1) * new):
            completed.append(n)
        else:
            if first_gap is None:
                first_gap = n
            released[str(n)] = {"client": "", "assigned_at": 0}

    low_new = next_new if first_gap is None else first_gap
    completed = [n for n in completed if n >= low_new]

    stats = {
        "old_completed_below": low,
        "old_next_block": next_old,
        "old_open": len(entry.get("open", {})),
        "new_completed_below": low_new,
        "new_next_block": next_new,
        "new_released": len(released),
        "new_completed_above_gap": len(completed),
    }

    entry["completed_below"] = low_new
    entry["next_block"] = next_new
    entry["open"] = released
    entry["completed"] = completed
    return stats


def cmd_block_size(args, data_dir):
    if args.old < 1 or args.new < 1:
        sys.exit("Block sizes must be positive.")
    if args.old == args.new:
        sys.exit("OLD and NEW are the same, nothing to do.")
    if args.new % 40 != 0:
        print(f"Warning: {args.new} is not a multiple of 40, so a rectangle's fills "
              "can straddle two blocks.")

    path = data_dir / "progress.json"
    if not path.exists():
        sys.exit(f"{path} not found.")

    progress = load(path)
    for alg, entry in progress.items():
        if not isinstance(entry, dict):
            continue
        s = reslice_entry(entry, args.old, args.new)
        print(f"[{alg}]")
        for k, v in s.items():
            print(f"  {k}: {v}")

    if args.dry_run:
        print("\nDry run: nothing written.")
        return

    print(f"\nBackup: {backup(path)}")
    save(path, progress)
    print(f"Wrote {path}")
    print(f"Now set BLOCK_SIZE = {args.new} in data_manager.h, rebuild the server, "
          "then restart it and your clients.")


def cmd_rename(args, data_dir):
    if args.old == args.new:
        sys.exit("OLD and NEW are the same, nothing to do.")

    clients_path = data_dir / "clients.json"
    progress_path = data_dir / "progress.json"
    changed = []

    clients = None
    if clients_path.exists():
        clients = load(clients_path)
        if args.old not in clients:
            print(f"Note: '{args.old}' is not in clients.json.")
        elif args.new in clients and not args.force:
            sys.exit(f"'{args.new}' already exists in clients.json (use --force to overwrite it).")
        else:
            clients[args.new] = clients.pop(args.old)
            changed.append((clients_path, clients, "client entry renamed"))

    if progress_path.exists():
        progress = load(progress_path)
        open_n = found_n = 0
        for entry in progress.values():
            if not isinstance(entry, dict):
                continue
            for blk in entry.get("open", {}).values():
                if blk.get("client") == args.old:
                    blk["client"] = args.new
                    open_n += 1
            for f in entry.get("found", []):
                if f.get("client") == args.old:
                    f["client"] = args.new
                    found_n += 1
        print(f"progress.json: {open_n} open block(s), {found_n} found prime(s) updated")
        if open_n or found_n:
            changed.append((progress_path, progress, "client references updated"))

    if not changed:
        print("Nothing to change.")
        return
    if args.dry_run:
        print("Dry run: nothing written.")
        return

    for path, data, what in changed:
        print(f"Backup: {backup(path)}")
        save(path, data)
        print(f"Wrote {path} ({what})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-dir", default="data")
    ap.add_argument("--dry-run", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("block-size", help="re-slice progress.json for a new BLOCK_SIZE")
    b.add_argument("old", type=int)
    b.add_argument("new", type=int)

    r = sub.add_parser("rename-client", help="rename a client id everywhere")
    r.add_argument("old")
    r.add_argument("new")
    r.add_argument("--force", action="store_true")

    args = ap.parse_args()
    data_dir = Path(args.data_dir)

    if args.cmd == "block-size":
        cmd_block_size(args, data_dir)
    else:
        cmd_rename(args, data_dir)


if __name__ == "__main__":
    main()