#!/usr/bin/env python3
"""Compare defined, tightly packed GFXR raw attachment prefixes, excluding allocation tails.

Layout is established separately from the *actual replay binary*. This script does
not prove replay/original-run equivalence, initialization, or resource generations.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


COLOR_BYTES = {
    "VK_FORMAT_R16G16_UNORM": 4,
    "VK_FORMAT_R16G16B16A16_SFLOAT": 8,
    "VK_FORMAT_A2B10G10R10_UNORM_PACK32": 4,
    "VK_FORMAT_B8G8R8A8_UNORM": 4,
    "VK_FORMAT_B8G8R8A8_SRGB": 4,
    "VK_FORMAT_B10G11R11_UFLOAT_PACK32": 4,
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def attachments(root):
    found = {}
    for path in sorted(root.glob("*.json")):
        document = json.loads(path.read_text())
        if not isinstance(document, list):
            continue
        for draw in document:
            if not isinstance(draw, dict) or "drawIndex" not in draw:
                continue
            for kind in ("colorAttachments", "depthAttachments"):
                for index, item in enumerate(draw.get(kind, [])):
                    key = (draw["drawIndex"], kind, index, item["aspect"],
                           item["mipLevel"], item["arrayLayer"])
                    if key in found:
                        raise ValueError(f"duplicate attachment identity: {key}")
                    found[key] = item
    if not found:
        raise ValueError(f"no draw attachments in {root}")
    return found


def load(root, item, phase, valid_bytes):
    path = root / Path(item[phase + "File"]).name
    data = path.read_bytes()
    if len(data) < valid_bytes:
        raise ValueError(f"short raw image: {path}: {len(data)} < {valid_bytes}")
    prefix = data[:valid_bytes]
    record = {"file": path.name, "bytes": len(data), "sha256": digest(data),
              "logical_bytes": valid_bytes, "allocation_tail_bytes": len(data) - valid_bytes,
              "prefix_sha256": digest(prefix)}
    if item["format"] == "VK_FORMAT_R16G16B16A16_SFLOAT" and item["aspect"] == "COLOR":
        half = np.frombuffer(prefix, dtype="<f2")
        finite = half[np.isfinite(half)]
        record["hdr"] = {"nan": int(np.isnan(half).sum()), "inf": int(np.isinf(half).sum()),
                         "finite_max": float(finite.max()) if finite.size else None}
    return prefix, record


def compare(a, b, dims, pixel_bytes):
    changed = np.any(np.frombuffer(a, dtype=np.uint8).reshape(-1, pixel_bytes) !=
                     np.frombuffer(b, dtype=np.uint8).reshape(-1, pixel_bytes), axis=1)
    indices = np.flatnonzero(changed)
    bbox = None
    if len(indices):
        w, h, _ = dims
        x, y, z = indices % w, (indices // w) % h, indices // (w * h)
        bbox = [int(x.min()), int(y.min()), int(z.min()),
                int(x.max()), int(y.max()), int(z.max())]
    return {"changed_storage_elements": int(changed.sum()), "bbox_xyz_inclusive": bbox}


def audit(baseline, candidate):
    left, right = attachments(baseline), attachments(candidate)
    if left.keys() != right.keys():
        raise ValueError("baseline/candidate attachment identities differ")
    result = []
    for key in sorted(left):
        old, new = left[key], right[key]
        for field in ("imageId", "format", "type", "aspect", "dimensions", "mipLevel", "arrayLayer"):
            if old[field] != new[field]:
                raise ValueError(f"metadata mismatch at {key}: {field}")
        dims = old["dimensions"]
        if len(dims) != 3 or any(not isinstance(v, int) or v < 1 for v in dims):
            raise ValueError(f"invalid dimensions: {dims}")
        aspect = old["aspect"]
        if aspect in ("DEPTH", "STENCIL") and old["format"] == "VK_FORMAT_D24_UNORM_S8_UINT":
            pixel_bytes = 1 if aspect == "STENCIL" else 4
        elif aspect == "COLOR":
            pixel_bytes = COLOR_BYTES[old["format"]]
        else:
            raise ValueError(f"unsupported format/aspect: {old['format']}/{aspect}")
        valid_bytes = dims[0] * dims[1] * dims[2] * pixel_bytes
        images, records = {}, {}
        for label, root, item in (("baseline", baseline, old), ("candidate", candidate, new)):
            for phase in ("before", "after"):
                images[label, phase], records[label + "_" + phase] = load(root, item, phase, valid_bytes)
        row = {"identity": list(key), "imageId": old["imageId"], "format": old["format"],
               "aspect": aspect, "dimensions": dims, "files": records,
               "interpretation": "storage elements only; D24 unused bits not interpreted; no initialized-region proof"}
        for label in ("baseline", "candidate"):
            row[label + "_draw_change"] = compare(images[label, "before"], images[label, "after"], dims, pixel_bytes)
        for phase in ("before", "after"):
            row[phase + "_cross_change"] = compare(images["baseline", phase], images["candidate", phase], dims, pixel_bytes)
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = {"scope": "replay raw attachment logical prefixes; layout must be independently established",
              "baseline": str(args.baseline), "candidate": str(args.candidate),
              "CAPTURE_COMPLETE": "NO: this comparison does not establish original-run equivalence",
              "attachments": audit(args.baseline, args.candidate)}
    with args.output.open("x") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    print(f"audited {len(report['attachments'])} attachment identities; allocation tails excluded")


if __name__ == "__main__":
    main()
