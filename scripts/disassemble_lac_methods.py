#!/usr/bin/env python3
import sys
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ELF_PATH = sys.argv[1]
OUT_PATH = sys.argv[2]
TARGETS = {
    "PlayerKillFeed.CheckChat": 0x1079F60,
    "PlayerKillFeed.CmdSendChat": 0x1079890,
    "PlayerKillFeed.RpcGetChat": 0x1079958,
    "PlayersManager.GetPlayer": 0xF8DFC8,
    "PlayerControl.CmdUpdateInventory": 0xF32168,
    "PlayerControl.RpcUpdateInventory": 0xF3221C,
    "PlayerControl.InventoryAdd": 0xF341E8,
    "PlayerControl.InventoryDelete": 0xF34608,
    "PlayerControl.InventoryGet": 0xF34A60,
}
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
with open(ELF_PATH, "rb") as f:
    elf = ELFFile(f)
    loads = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    with open(OUT_PATH, "w") as out:
        for name, rva in TARGETS.items():
            seg = next((s for s in loads if s["p_vaddr"] <= rva < s["p_vaddr"] + s["p_memsz"]), None)
            out.write(f"\n===== {name} RVA=0x{rva:X} =====\n")
            if not seg:
                out.write("RVA not mapped by PT_LOAD segment\n")
                continue
            delta = rva - seg["p_vaddr"]
            raw = seg.data()[delta:delta + 0x300]
            count = 0
            for ins in md.disasm(raw, rva):
                out.write(f"0x{ins.address:08X}: {ins.mnemonic:8} {ins.op_str}\n")
                count += 1
                if count >= 120:
                    break
            if not count:
                out.write("No instructions decoded\n")
