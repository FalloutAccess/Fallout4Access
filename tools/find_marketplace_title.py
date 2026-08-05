# Ghidra Jython script. Run inside Ghidra on program "Fallout4 AE laytest"
# (Fallout4.exe 1.11.221, image base 0x140000000).
#
# Purpose: find the in-memory offset of MarketplaceContent's display TITLE / name
# (a char*/BSFixedString member, sibling of description=+0x20 and author=+0x40),
# by locating the class vtable -> ctor/dtor and the BNet deserializer that reads
# the "title"/"displayName"/"name" JSON key.
#
# Why a script: the Ghidra MCP *resource* interface available to the assistant
# cannot do cross-references, byte search, full symbol enumeration, or read vtable
# function pointers (defined arrays collapse in the listing). This script uses the
# Ghidra API directly to do all of that.
#
# How to run: Ghidra GUI -> Window -> Script Manager -> (script directory containing
# this file) -> double-click. Output goes to the console AND to
# <home>/find_marketplace_title.out.txt

import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

OUT = []
def log(s):
    print(s)
    OUT.append(str(s))

prog = currentProgram
fm = prog.getFunctionManager()
ref = prog.getReferenceManager()
listing = prog.getListing()
af = prog.getAddressFactory()
mem = prog.getMemory()
base = prog.getImageBase().getOffset()

def A(off):
    return af.getAddress(hex(off).rstrip("L"))

def rd32(addr):
    return mem.getInt(addr) & 0xffffffff

def rdptr(addr):
    return mem.getLong(addr) & 0xffffffffffffffff

TYPEDESC = A(0x142fedf20)  # .?AVMarketplaceContent@@
td_rva = TYPEDESC.getOffset() - base
log("MarketplaceContent type descriptor: %s (rva 0x%x)" % (TYPEDESC, td_rva))

# --- 1) type descriptor xrefs -> COL(s) -> vtable(s) -------------------------------
td_refs = list(ref.getReferencesTo(TYPEDESC))
log("refs to type descriptor: %d" % len(td_refs))

cols = set()
for r in td_refs:
    frm = r.getFromAddress()
    for delta in (0x0C, 0x00):              # COL.pTypeDescriptor@+0xC, BCD.pTypeDescriptor@+0x0
        col = frm.subtract(delta)
        try:
            if rd32(col.add(0x0C)) == td_rva:   # confirm it's a COL
                cols.add(col.getOffset())
        except:
            pass
log("candidate COLs: %s" % [hex(c) for c in cols])

vtables = set()
for c in cols:
    coladdr = A(c)
    for r in ref.getReferencesTo(coladdr):  # vftable_meta_ptr -> COL
        vtables.add(r.getFromAddress().add(8).getOffset())  # vtable = meta_ptr + 8
log("candidate vtables: %s" % [hex(v) for v in vtables])

# --- 2) decompiler ----------------------------------------------------------------
decomp = DecompInterface()
decomp.openProgram(prog)
mon = ConsoleTaskMonitor()
def decompile(func):
    res = decomp.decompileFunction(func, 60, mon)
    if res and res.decompileCompleted() and res.getDecompiledFunction():
        return res.getDecompiledFunction().getC()
    return None

funcs = set()

# dtor = vtable[0]
for v in vtables:
    try:
        dptr = rdptr(A(v))
        f = fm.getFunctionContaining(A(dptr))
        if f:
            funcs.add(f.getEntryPoint().getOffset())
            log("vtable 0x%x -> dtor %s %s" % (v, f.getEntryPoint(), f.getName()))
    except Exception as e:
        log("dtor read failed for 0x%x: %s" % (v, e))

# ctor(s) = functions that write the vtable address
for v in vtables:
    for r in ref.getReferencesTo(A(v)):
        f = fm.getFunctionContaining(r.getFromAddress())
        if f:
            funcs.add(f.getEntryPoint().getOffset())
            log("vtable 0x%x referenced by %s %s" % (v, f.getEntryPoint(), f.getName()))

# --- 3) deserializer: functions referencing BNet JSON key strings -----------------
KEYS = ["title", "displayName", "name", "description", "author",
        "creatorName", "contentId", "version", "category", "uuid", "formId",
        "isOwned", "installed", "price"]

def string_addrs(text):
    hits = []
    it = listing.getDefinedData(True)
    while it.hasNext():
        d = it.next()
        try:
            val = d.getValue()
        except:
            val = None
        if val is not None and str(val) == text:
            hits.append(d.getAddress())
    return hits

log("\n--- BNet JSON key strings and the functions that use them ---")
for k in KEYS:
    for sa in string_addrs(k):
        users = []
        for r in ref.getReferencesTo(sa):
            f = fm.getFunctionContaining(r.getFromAddress())
            if f:
                users.append("%s@%s" % (f.getName(), f.getEntryPoint()))
                funcs.add(f.getEntryPoint().getOffset())
        log("'%s' @ %s -> %s" % (k, sa, users))

# --- 4) dump decompilations -------------------------------------------------------
log("\n================ DECOMPILATIONS ================")
for off in sorted(funcs):
    f = fm.getFunctionAt(A(off))
    if not f:
        continue
    log("\n----- %s  %s -----" % (f.getName(), f.getEntryPoint()))
    c = decompile(f)
    if c:
        log(c)

outpath = os.path.join(os.path.expanduser("~"), "find_marketplace_title.out.txt")
try:
    fh = open(outpath, "w")
    fh.write("\n".join(OUT))
    fh.close()
    print("\nWrote %s" % outpath)
except Exception as e:
    print("\nCould not write output file (%s); use the console output above." % e)
