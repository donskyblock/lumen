#!/usr/bin/env python3
"""Build installable packages + index.json for the extension marketplace.

    python3 tools/package_extensions.py extensions/ dist/

Publish the contents of dist/ (e.g. in a GitHub repo or release) and point
Lumen at <url>/index.json in Settings -> Extensions. Download URLs in the
index are relative to the index file.
"""
import hashlib
import json
import os
import sys
import tarfile


def main(src, dst):
    os.makedirs(dst, exist_ok=True)
    items = []
    for name in sorted(os.listdir(src)):
        d = os.path.join(src, name)
        manifest = os.path.join(d, "plugin.json")
        if not os.path.isfile(manifest):
            continue
        m = json.load(open(manifest))
        pkg = "%s-%s.tar.gz" % (m["id"], m["version"])
        with tarfile.open(os.path.join(dst, pkg), "w:gz") as t:
            t.add(d, arcname=m["id"], filter=lambda ti: (setattr(ti, "uid", 0) or setattr(ti, "gid", 0) or ti))
        sha = hashlib.sha256(open(os.path.join(dst, pkg), "rb").read()).hexdigest()
        items.append({k: m[k] for k in ("id", "name", "version", "description", "capabilities") if k in m} |
                     {"author": m.get("author", ""), "setup": m.get("setup", ""),
                      "download": {"url": pkg, "sha256": sha}})
        print("packaged", pkg, sha[:12])
    json.dump({"version": 1, "extensions": items}, open(os.path.join(dst, "index.json"), "w"), indent=2)
    print("wrote", os.path.join(dst, "index.json"))


if __name__ == "__main__":
    main(*(sys.argv[1:3] if len(sys.argv) >= 3 else ["extensions", "dist"]))
