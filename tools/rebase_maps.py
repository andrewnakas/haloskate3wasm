"""Copy a halo-cleanroom maps.json, giving every entry an absolute base on that site."""
import json, sys
m = json.load(open(sys.argv[1]))
for f in m["files"]:
    f.setdefault("base", "/halo-cleanroom/clean/")
json.dump(m, open(sys.argv[2], "w"), indent=1)
