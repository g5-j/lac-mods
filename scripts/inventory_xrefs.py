#!/usr/bin/env python3
"""Find all ARM64 callers of important LAC inventory/chat methods."""
import re, sys
from collections import defaultdict
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

if len(sys.argv) != 4:
    raise SystemExit("usage: inventory_xrefs.py libil2cpp.so dump.cs output.txt")
elf_path, dump_path, out_path = sys.argv[1:]

lines = open(dump_path, encoding="utf-8", errors="replace").read().splitlines()
methods = []
for i, line in enumerate(lines):
    m = re.search(r"// RVA: 0x([0-9A-Fa-f]+)", line)
    if not m:
        continue
    rva = int(m.group(1), 16)
    signature = " ".join(x.strip() for x in lines[i+1:i+4] if x.strip())
    methods.append((rva, signature))
methods.sort()
unique = {}
for rva, sig in methods:
    unique.setdefault(rva, sig)
rvas = sorted(unique)
def owner(addr):
    lo, hi = 0, len(rvas)
    while lo < hi:
        mid = (lo + hi)//2
        if rvas[mid] <= addr: lo = mid+1
        else: hi = mid
    if lo == 0: return None
    start = rvas[lo-1]
    end = rvas[lo] if lo < len(rvas) else start + 0x10000
    if addr >= end: return None
    return start, unique[start]

targets = {
    0xF341E8: "PlayerControl.InventoryAdd",
    0xF34608: "PlayerControl.InventoryDelete",
    0xF34A60: "PlayerControl.InventoryGet",
    0xF32168: "PlayerControl.CmdUpdateInventory",
    0xF3221C: "PlayerControl.RpcUpdateInventory",
    0xF322D0: "PlayerControl.RpcUpdateInventoryIndividual",
    0x1079890: "PlayerKillFeed.CmdSendChat",
    0x1079958: "PlayerKillFeed.RpcGetChat",
    0x1079F60: "PlayerKillFeed.CheckChat",
}
calls = defaultdict(list)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
with open(elf_path, "rb") as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD" or not (seg["p_flags"] & 1):
            continue
        base = seg["p_vaddr"]
        for ins in md.disasm(seg.data(), base):
            if ins.mnemonic != "bl":
                continue
            m = re.search(r"#0x([0-9a-fA-F]+)", ins.op_str)
            if not m:
                continue
            dst = int(m.group(1), 16)
            if dst in targets:
                src = owner(ins.address)
                calls[dst].append((ins.address, src))

with open(out_path, "w", encoding="utf-8") as out:
    for dst, name in targets.items():
        out.write(f"\n===== CALLERS OF {name} (0x{dst:X}) =====\n")
        seen = set()
        for site, src in sorted(calls.get(dst, [])):
            if src:
                key = src[0]
                if key in seen: continue
                seen.add(key)
                out.write(f"CALLSITE=0x{site:X} METHOD_RVA=0x{src[0]:X} {src[1]}\n")
            else:
                out.write(f"CALLSITE=0x{site:X} METHOD=(unmapped)\n")
        out.write(f"CALLSITE_COUNT={len(calls.get(dst, []))}\n")
