#!/usr/bin/env python3
# Bazarish project (c) 2026
# Fetches the MSYS2 packages the client links with, and everything they depend on.
import hashlib
import os
import re
import sys
import urllib.request

kUsage = "usage: packages.py <repository url> <unpacked db> <package prefix> <download dir> <roots>"
kField = re.compile(r"%(\w+)%\n((?:.+\n)*)")
kVersionBound = re.compile(r"[<>=]")
kChunkBytes = 1 << 20


def readDatabase(folder):
    packages = {}
    provided = {}
    for entry in os.listdir(folder):
        with open(os.path.join(folder, entry, "desc"), encoding="utf-8") as desc:
            fields = {m.group(1): m.group(2).split() for m in kField.finditer(desc.read())}
        name = fields["NAME"][0]
        packages[name] = fields
        for provision in fields.get("PROVIDES", []):
            provided[kVersionBound.split(provision)[0]] = name
    return packages, provided


def closure(packages, provided, prefix, roots):
    wanted = [prefix + root for root in roots]
    taken = set()
    while wanted:
        name = kVersionBound.split(wanted.pop())[0]
        name = name if name in packages else provided.get(name, name)
        if name in taken:
            continue
        if name not in packages:
            sys.exit(f"packages.py: no package {name} in the database")
        taken.add(name)
        wanted.extend(packages[name].get("DEPENDS", []))
    return sorted(taken)


def sha256Of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as data:
        for chunk in iter(lambda: data.read(kChunkBytes), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    if len(sys.argv) != 6:
        sys.exit(kUsage)
    repository, database, prefix, downloads, roots = sys.argv[1:]
    packages, provided = readDatabase(database)
    os.makedirs(downloads, exist_ok=True)
    for name in closure(packages, provided, prefix, roots.split(",")):
        file = packages[name]["FILENAME"][0]
        expected = packages[name]["SHA256SUM"][0]
        path = os.path.join(downloads, file)
        if not os.path.exists(path) or sha256Of(path) != expected:
            urllib.request.urlretrieve(f"{repository}/{file}", path)
        if sha256Of(path) != expected:
            sys.exit(f"packages.py: {file} does not match the database")
        print(path)


main()
