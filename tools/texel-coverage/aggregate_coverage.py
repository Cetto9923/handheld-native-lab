#!/usr/bin/env python3
"""Aggregate the typed-storage coverage scan into the report's statistics.

Reads the analyzer's per-module JSONL and produces:
  * the hit counts and the refusal-reason breakdown,
  * the per-binding table of every descriptor the typed rewrite cannot cover,
  * the per-module verdict, following the engine's own variant rule: the
    engine refuses the whole typed variant when any single binding fails, so a
    module is only covered when every formatless-write binding is covered.

Usage:
  aggregate_coverage.py --jsonl <modules.jsonl> --out <output-dir>
"""
import argparse
import json
import os
from collections import Counter, OrderedDict

STORAGE_IMAGE = 3
STORAGE_TEXEL_BUFFER = 5

REASONS = (
    "accepted",
    "no-storage-image-for-binding",
    "image-type-shared-by-multiple-descriptors",
    "sampled-type-class-mismatch",
    "formatless-read-on-same-image",
    "unknown-sampled-type",
    "malformed-module",
    "invalid-target-format",
    "already-typed",
    "typed-with-other-format",
)


def load(path):
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                yield json.loads(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jsonl", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    total = parsed = unparsed = 0
    stage_counts = Counter()
    modules_with_storage = 0
    modules_kept_words = 0
    modules_with_fless_write = 0
    modules_with_fless_read = 0
    modules_covered = 0
    modules_uncovered = 0

    storage_bindings = 0
    fless_write_bindings = 0
    fless_read_bindings = 0
    count_not_1 = 0
    no_image_bindings = 0
    module_formatless = 0
    module_typed = 0

    reason_counts = Counter()
    accepted_sets = Counter()
    uncovered_bindings = []
    module_verdicts = []
    mismatch_count = 0

    for record in load(args.jsonl):
        total += 1
        if not record.get("parsed"):
            unparsed += 1
            continue
        parsed += 1
        stage_counts[record["stage"]] += 1
        if record.get("module_words_kept"):
            modules_kept_words += 1
        bindings = record.get("bindings") or []
        if bindings:
            modules_with_storage += 1

        # The engine only attempts the typed form for a formatless write.
        targets = []
        for binding in bindings:
            storage_bindings += 1
            if binding["count"] != 1:
                count_not_1 += 1
            if binding["formatless_write_flag"]:
                fless_write_bindings += 1
            if binding["formatless_read_flag"]:
                fless_read_bindings += 1
            if not binding["module_has_image"]:
                no_image_bindings += 1
            if binding["declared_format"] == 0:
                module_formatless += 1
            else:
                module_typed += 1
            for verdict in binding["rewrite"]:
                if not verdict["verdict_agrees"]:
                    mismatch_count += 1
            if binding["formatless_write_flag"] and binding["declared_format"] == 0:
                targets.append(binding)

        module_has_write = any(b["formatless_write_flag"] for b in bindings)
        module_has_read = any(b["formatless_read_flag"] for b in bindings)
        if module_has_write:
            modules_with_fless_write += 1
        if module_has_read:
            modules_with_fless_read += 1

        # A formatless read without a formatless write is never typed by the
        # engine, so it stays formatless and still needs the read feature.
        read_only = [
            b for b in bindings
            if b["formatless_read_flag"] and not b["formatless_write_flag"]
        ]

        module_ok = True
        module_reasons = set()
        for binding in targets:
            accepted = [v["vk_format"] for v in binding["rewrite"] if v["result"] == 1]
            refused = [v for v in binding["rewrite"] if v["result"] != 1]
            if accepted:
                accepted_sets[tuple(accepted)] += 1
            else:
                module_ok = False
                reasons = sorted({v["reason"] for v in refused})
                module_reasons.update(reasons)
                for verdict in refused:
                    reason_counts[verdict["reason"]] += 1
                uncovered_bindings.append({
                    "key": record["key"],
                    "stage": record["stage"],
                    "binding": binding["binding"],
                    "descriptor_type": binding["descriptor_type"],
                    "count": binding["count"],
                    "package_scalar": binding["package_scalar"],
                    "sampled_class": binding["sampled_class"],
                    "image_type_sharers": binding.get("image_type_sharers", 0),
                    "image_writes": binding["image_writes"],
                    "image_reads": binding["image_reads"],
                    "reasons": reasons,
                })
        for binding in read_only:
            module_ok = False
            module_reasons.add("formatless-read-without-write")

        if not targets and not read_only:
            continue
        if module_ok:
            modules_covered += 1
        else:
            modules_uncovered += 1
        module_verdicts.append({
            "key": record["key"],
            "stage": record["stage"],
            "verdict": "covered" if module_ok else "uncovered",
            "reasons": sorted(module_reasons),
            "targets": len(targets),
            "read_only": len(read_only),
        })

    stats = OrderedDict([
        ("modules_total", total),
        ("modules_parsed", parsed),
        ("modules_unparsed", unparsed),
        ("modules_by_stage", dict(sorted(stage_counts.items()))),
        ("modules_with_storage_binding", modules_with_storage),
        ("modules_with_formatless_write", modules_with_fless_write),
        ("modules_with_formatless_read", modules_with_fless_read),
        ("modules_keeping_module_words", modules_kept_words),
        ("storage_bindings", storage_bindings),
        ("formatless_write_bindings", fless_write_bindings),
        ("formatless_read_bindings", fless_read_bindings),
        ("count_not_1_bindings", count_not_1),
        ("bindings_without_module_image", no_image_bindings),
        ("bindings_module_formatless", module_formatless),
        ("bindings_module_already_typed", module_typed),
        ("modules_covered", modules_covered),
        ("modules_uncovered", modules_uncovered),
        ("uncovered_bindings", len(uncovered_bindings)),
        ("diagnostic_verdict_mismatches", mismatch_count),
    ])
    stats["refusal_reason_counts"] = OrderedDict(
        (reason, reason_counts.get(reason, 0))
        for reason in REASONS if reason_counts.get(reason, 0)
    )
    stats["accepted_format_set_counts"] = {
        ",".join(str(f) for f in key): value
        for key, value in sorted(accepted_sets.items())
    }

    with open(os.path.join(args.out, "coverage_stats.json"), "w") as handle:
        json.dump(stats, handle, indent=2)
    with open(os.path.join(args.out, "uncovered_bindings.json"), "w") as handle:
        json.dump(uncovered_bindings, handle, indent=2)
    with open(os.path.join(args.out, "module_verdicts.json"), "w") as handle:
        json.dump(module_verdicts, handle, indent=2)
    with open(os.path.join(args.out, "uncovered_bindings.tsv"), "w") as handle:
        handle.write("key\tstage\tbinding\tdescriptor_type\tcount\t"
                     "package_scalar\tsampled_class\timage_type_sharers\t"
                     "image_writes\timage_reads\treasons\n")
        for entry in uncovered_bindings:
            handle.write("\t".join([
                entry["key"], str(entry["stage"]), str(entry["binding"]),
                str(entry["descriptor_type"]), str(entry["count"]),
                str(entry["package_scalar"]), str(entry["sampled_class"]),
                str(entry["image_type_sharers"]), str(entry["image_writes"]),
                str(entry["image_reads"]), "|".join(entry["reasons"]),
            ]) + "\n")
    with open(os.path.join(args.out, "module_verdicts.tsv"), "w") as handle:
        handle.write("key\tstage\tverdict\ttargets\tread_only\treasons\n")
        for entry in module_verdicts:
            handle.write("\t".join([
                entry["key"], str(entry["stage"]), entry["verdict"],
                str(entry["targets"]), str(entry["read_only"]),
                "|".join(entry["reasons"]),
            ]) + "\n")

    print(json.dumps(stats, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
