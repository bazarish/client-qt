#!/usr/bin/env python3
# Bazarish project (c) 2026
# Lays out the portable folder: the program, the Qt plugins and QML modules it uses, and every
# DLL any of them imports, taken from the toolchain and the sysroot.
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
kImport = re.compile(r"^\s*DLL Name:\s*(\S+)", re.M)
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


def importsOf(objdump, binary):
    listing = subprocess.run([objdump, "-p", binary], check=True, capture_output=True, text=True)
    return kImport.findall(listing.stdout)


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
            for dll in importsOf(objdump, binary):
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
    copyImports(os.path.join(llvmMingw, "bin", "llvm-objdump"), folder,
        [os.path.join(llvmMingw, triple, "bin"), os.path.join(sysroot, "bin")])
    with open(os.path.join(folder, "qt.conf"), "w", encoding="utf-8") as conf:
        conf.write(kQtConf)


main()
