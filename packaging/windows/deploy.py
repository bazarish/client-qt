#!/usr/bin/env python3
# Bazarish project (c) 2026
# Lays out the portable folder: the program, the Qt plugins and QML modules it uses, and every
# DLL any of them imports, taken from the sysroot before the toolchain: MSYS2 builds import
# _Unwind_* from its libc++.dll, which llvm-mingw's does not export.
import json
import os
import re
import shutil
import subprocess
import sys

kUsage = "usage: deploy.py <folder> <program> <qml imports json> <llvm-mingw> <sysroot> <triple>"
kPlugins = {
    "platforms": ["qwindows.dll"],
    "imageformats": None,
    "iconengines": None,
    "multimedia": None,
    "tls": ["qschannelbackend.dll", "qcertonlybackend.dll"],
    "styles": None,
    "networkinformation": ["qnetworklistmanager.dll"],
}
kNotShipped = (".debug", ".pdb", ".a")
kQtConf = "[Paths]\nPrefix = .\nPlugins = .\nImports = .\nQml2Imports = .\n"
kImportedDll = re.compile(r"^\s*DLL Name:\s*(\S+)$")
kImportedName = re.compile(r"^\s+\d+\s+(\S+)$")
kExportedName = re.compile(r"^\s+\d+\s+0x[0-9a-f]+\s+(\S+)")
# What every Windows the client runs on carries itself.
kSystem = re.compile(
    r"^(api-ms-win-.*|ext-ms-.*|advapi32|authz|avrt|bcrypt|bcryptprimitives|cfgmgr32|combase|"
    r"comctl32|comdlg32|credui|crypt32|cryptnet|d2d1|d3d9|d3d11|d3d12|d3dcompiler_47|dbghelp|"
    r"dcomp|dnsapi|dwmapi|dwrite|dxgi|dxva2|evr|gdi32|gdiplus|hid|imm32|iphlpapi|kernel32|ksuser|"
    r"mf|mfcore|mfplat|mfreadwrite|mfuuid|mmdevapi|mpr|mscms|msimg32|msvcrt|ncrypt|netapi32|"
    r"normaliz|ntdll|ole32|oleacc|oleaut32|opengl32|powrprof|propsys|psapi|rpcrt4|runtimeobject|"
    r"secur32|setupapi|shcore|shell32|shlwapi|strmiids|ucrtbase|uiautomationcore|user32|userenv|"
    r"usp10|uxtheme|version|windowscodecs|winhttp|wininet|winmm|winspool\.drv|wintrust|wldap32|"
    r"ws2_32|wsock32|wtsapi32)(\.dll)?$", re.I)


def tablesOf(objdump, binary):
    listing = subprocess.run([objdump, "-p", binary], check=True, capture_output=True, text=True)
    imports, exports, dll, section = {}, set(), None, None
    for line in listing.stdout.splitlines():
        if line.startswith("The Import Tables:"):
            section = "imports"
        elif line.startswith("Export Table:"):
            section = "exports"
        elif section == "imports" and (found := kImportedDll.match(line)):
            dll = found.group(1)
            imports[dll] = set()
        elif section == "imports" and dll and (found := kImportedName.match(line)):
            imports[dll].add(found.group(1))
        elif section == "exports" and (found := kExportedName.match(line)):
            exports.add(found.group(1))
    return imports, exports


def binaries(folder):
    for path, _, files in os.walk(folder):
        for name in files:
            if name.lower().endswith((".exe", ".dll")):
                yield os.path.join(path, name)


def copyPlugins(qtShare, folder):
    for kind, names in kPlugins.items():
        source = os.path.join(qtShare, "plugins", kind)
        os.makedirs(os.path.join(folder, kind))
        for name in names or sorted(os.listdir(source)):
            shutil.copy2(os.path.join(source, name), os.path.join(folder, kind))


def copyQml(qtShare, folder, imports):
    root = os.path.join(qtShare, "qml")
    wanted = sorted({e["path"] for e in imports if e.get("type") == "module" and "path" in e})
    for module in wanted:
        for path, dirs, files in os.walk(module):
            dirs[:] = [d for d in dirs if os.path.join(path, d) in wanted
                or not os.path.exists(os.path.join(path, d, "qmldir"))]
            target = os.path.join(folder, os.path.relpath(path, root))
            os.makedirs(target, exist_ok=True)
            for name in files:
                if not name.endswith(kNotShipped):
                    shutil.copy2(os.path.join(path, name), target)


def copyImports(objdump, folder, sources):
    have = {name.lower() for name in os.listdir(folder)}
    unresolved = set()
    added = True
    while added:
        added = False
        unresolved.clear()
        for binary in list(binaries(folder)):
            for dll in tablesOf(objdump, binary)[0]:
                if dll.lower() in have or kSystem.match(dll):
                    continue
                found = next((d for d in sources if os.path.exists(os.path.join(d, dll))), None)
                if found is None:
                    unresolved.add(f"{dll} (by {os.path.relpath(binary, folder)})")
                    continue
                shutil.copy2(os.path.join(found, dll), folder)
                have.add(dll.lower())
                added = True
    if unresolved:
        sys.exit("deploy.py: imports found nowhere: " + ", ".join(sorted(unresolved)))


def checkSymbols(objdump, folder):
    tables = {os.path.basename(b).lower(): (b, *tablesOf(objdump, b)) for b in binaries(folder)}
    missing = []
    for binary, imports, _ in tables.values():
        for dll, names in imports.items():
            if dll.lower() in tables:
                lacking = names - tables[dll.lower()][2]
                missing += [f"{name} (by {os.path.relpath(binary, folder)} from {dll})"
                    for name in sorted(lacking)]
    if missing:
        sys.exit("deploy.py: imports no shipped DLL exports: " + ", ".join(missing))


def main():
    if len(sys.argv) != 7:
        sys.exit(kUsage)
    folder, program, importsFile, llvmMingw, sysroot, triple = sys.argv[1:]
    with open(importsFile, encoding="utf-8") as listed:
        imports = json.load(listed)
    qtShare = os.path.join(sysroot, "share", "qt6")
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    shutil.copy2(program, folder)
    copyPlugins(qtShare, folder)
    copyQml(qtShare, folder, imports)
    objdump = os.path.join(llvmMingw, "bin", "llvm-objdump")
    copyImports(objdump, folder,
        [os.path.join(sysroot, "bin"), os.path.join(llvmMingw, triple, "bin")])
    checkSymbols(objdump, folder)
    with open(os.path.join(folder, "qt.conf"), "w", encoding="utf-8") as conf:
        conf.write(kQtConf)


main()
