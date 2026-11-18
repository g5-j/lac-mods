#!/usr/bin/env python3
"""Disassemble exact IL2CPP method ranges using Il2CppDumper's RVA table."""
import re
import sys
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64

if len(sys.argv) != 4:
    raise SystemExit("usage: disassemble_lac_methods.py libil2cpp.so dump.cs output.txt")

ELF_PATH, DUMP_PATH, OUT_PATH = sys.argv[1:]
TARGETS = {
    "PlayerKillFeed.CheckChat": "CheckChat(string _chat)",
    "PlayerKillFeed.CmdSendChat": "CmdSendChat(string _prefix, string _chat)",
    "PlayerKillFeed.RpcGetChat": "RpcGetChat(string _chat, int _range, uint _pmID)",
    "PlayersManager.GetPlayer": "GetPlayer(string _playerID)",
    "PlayerControl.CmdUpdateInventory": "CmdUpdateInventory(string _data)",
    "PlayerControl.RpcUpdateInventory": "RpcUpdateInventory(string _data)",
    "PlayerControl.InventoryAdd": "InventoryAdd(string _item, string _value)",
    "PlayerControl.InventoryDelete": "InventoryDelete(string _item, string _value)",
    "PlayerControl.InventoryGet": "InventoryGet(string _item)",
}

text = open(DUMP_PATH, encoding="utf-8", errors="replace").read()
lines = text.splitlines()
all_rvas = []
target_rvas = {}
for i, line in enumerate(lines):
    m = re.search(r"// RVA: 0x([0-9A-Fa-f]+)", line)
    if not m:
        continue
    rva = int(m.group(1), 16)
    all_rvas.append(rva)
    for name, signature in TARGETS.items():
        if signature in "\n".join(lines[i + 1:i + 4]):
            target_rvas[name] = rva

missing = [name for name in TARGETS if name not in target_rvas]
if missing:
    raise SystemExit("Missing target RVA(s) in dump.cs: " + ", ".join(missing))

sorted_rvas = sorted(set(all_rvas))
next_rva = {}
for rva in sorted_rvas:
    next_rva[rva] = next((x for x in sorted_rvas if x > rva), None)

md = Cs(CS_ARCH_ARM64)
md.detail = True
with open(ELF_PATH, "rb") as f, open(OUT_PATH, "w", encoding="utf-8") as out:
    elf = ELFFile(f)
    loads = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    for name, rva in target_rvas.items():
        end = next_rva.get(rva)
        out.write(f"\n===== {name} RVA=0x{rva:X} END=0x{end:X} =====\n" if end else f"\n===== {name} RVA=0x{rva:X} =====\n")
        seg = next((s for s in loads if s["p_vaddr"] <= rva < s["p_vaddr"] + s["p_filesz"]), None)
        if not seg:
            out.write("RVA not mapped by file-backed PT_LOAD segment\n")
            continue
        delta = rva - seg["p_vaddr"]
        limit = (end - rva) if end else 0x4000
        raw = seg.data()[delta:delta + limit]
        count = 0
        calls = set()
        for ins in md.disasm(raw, rva):
            out.write(f"0x{ins.address:08X}: {ins.mnemonic:8} {ins.op_str}\n")
            if ins.mnemonic in ("bl", "blr") and ins.op_str.startswith("0x"):
                calls.add(ins.op_str)
            count += 1
        out.write(f"INSTRUCTION_COUNT={count}\n")
        out.write("DIRECT_CALL_TARGETS=" + (", ".join(sorted(calls)) if calls else "(none)") + "\n")
