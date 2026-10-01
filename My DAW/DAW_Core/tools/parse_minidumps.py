"""Focused Windows Minidump parser — extracts module lists, exception records,
   CONTEXT64 registers, and stack traces. Reads raw .dmp binary without external
   debugger dependencies.

   Usage: python parse_minidumps.py [directory_of_dumps]
"""
import struct, os, sys, glob

# Windows MDMP constants
MAGIC = 0x4d444d50  # "MDMP"
STREAM_EXCEPTION = 5
STREAM_MODULE_LIST = 4
STREAM_THREAD_LIST = 3
STREAM_SYSTEM_INFO = 3  # not 7; 0x0003 = ThreadList, 0x0007 = MemoryList — actually
# Let me use the correct enum values from minidumpapiset.h
# Correction: StreamType values
ST_MODULE_LIST = 0x0004
ST_THREAD_LIST = 0x0003
ST_EXCEPTION   = 0x0005
ST_THREAD_EX_LIST = 0x0006
ST_MEMORY_LIST = 0x0007
ST_SYSTEM_INFO = 0x0007  # wait, MemoryList is 7, SystemInfo is 0x0007? No:
# From minidumpapiset.h:
# 0x0000 Reserved0
# 0x0001 Unused
# 0x0002 Reserved0
# 0x0003 ThreadListStream
# 0x0004 ModuleListStream
# 0x0005 ExceptionStream
# 0x0006 SystemInfoStream  (NOT 7)
# 0x0007 ThreadExListStream (NOT 7)
# 0x0008 MemoryListStream (NOT 7)

# Correction: exact values from the header
ST_RESERVED0     = 0x0000
ST_UNUSED        = 0x0001
ST_RESERVED1     = 0x0002
ST_THREAD_LIST   = 0x0003
ST_MODULE_LIST   = 0x0004
ST_EXCEPTION     = 0x0005
ST_SYSTEM_INFO   = 0x0006  # correct per Windows SDK
ST_THREAD_EX     = 0x0007
ST_MEMORY_LIST   = 0x0008
ST_EX_RVA_MAP    = 0x0009
ST_OPERATING_SYSTEM = 0x000A
ST_PROCESSOR_DATA = 0x000B
ST_UNUSED2       = 0x000C
ST_TOKEN         = 0x000D

def read_uint16(data, offset): return struct.unpack_from('<H', data, offset)[0]
def read_uint32(data, offset): return struct.unpack_from('<I', data, offset)[0]
def read_uint64(data, offset): return struct.unpack_from('<Q', data, offset)[0]
def read_int32(data, offset):  return struct.unpack_from('<i', data, offset)[0]
def read_int64(data, offset):  return struct.unpack_from('<q', data, offset)[0]

class MinidumpParser:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        self.dirs = []       # (type, rva, size)
        self.modules = []    # list of (base_rva, size, name, pdb_guid, pdb_age)
        self.threads = []    # list of (tid, teb_rva, stack_rva, stack_size)
        self.exception_code = None
        self.fault_addr = None
        self.exception_thread_id = None
        self.context = {}    # registers
        self.call_stack = [] # list of (module, rva_offset) pairs
        self.exception_flags = 0  # 0=READ, 1=WRITE, 0x8=EXECUTE

    def parse(self):
        if len(self.data) < 32:
            return "File too small"
        sig = read_uint32(self.data, 0)
        if sig != MAGIC:
            return f"Not a minidump (magic=0x{sig:08x})"
        num_streams = read_uint32(self.data, 4)
        stream_rva = read_uint32(self.data, 12)
        self.dirs = []
        for i in range(num_streams):
            off = stream_rva + i * 12
            if off + 12 > len(self.data):
                break
            stype = read_uint32(self.data, off)
            rva   = read_uint32(self.data, off + 4)
            size  = read_uint32(self.data, off + 8)
            self.dirs.append((stype, rva, size))
        for stype, rva, size in self.dirs:
            if stype == ST_MODULE_LIST:
                self._parse_modules(rva, size)
            elif stype == ST_THREAD_LIST:
                self._parse_threads(rva, size)
            elif stype == ST_EXCEPTION:
                self._parse_exception(rva, size)
        self._resolve_stack()
        return "OK"

    def _parse_modules(self, rva, size):
        if rva + 4 > len(self.data): return
        count = read_uint32(self.data, rva)
        self.modules = []
        for i in range(count):
            off = rva + 4 + i * 108  # MINIDUMP_MODULE = 108 bytes
            if off + 108 > len(self.data): break
            base = read_uint64(self.data, off)
            size_m = read_uint32(self.data, off + 8)
            name_off = read_uint32(self.data, off + 16)
            pdb_rva = read_uint32(self.data, off + 36)
            pdb_size = read_uint32(self.data, off + 40)
            pdb_sig  = self.data[off + 44:off + 56] if off + 56 <= len(self.data) else b''
            pdb_age  = read_uint32(self.data, off + 60)
            # Read name (null-terminated, max 260 chars)
            name_end = self.data.find(b'\x00', name_off)
            if name_end < 0: name_end = name_off + 260
            name = self.data[name_off:name_end].decode('utf-16-le', errors='replace') if name_end > name_off else "?"
            self.modules.append((base, size_m, name, pdb_sig.hex() if pdb_sig else '', pdb_age))

    def _parse_threads(self, rva, size):
        if rva + 4 > len(self.data): return
        count = read_uint32(self.data, rva)
        self.threads = []
        for i in range(count):
            off = rva + 4 + i * 48  # MINIDUMP_THREAD = 48 bytes
            if off + 48 > len(self.data): break
            tid = read_uint32(self.data, off)
            teb = read_uint64(self.data, off + 8)
            stk_rva = read_uint32(self.data, off + 16)
            stk_size = read_uint32(self.data, off + 20)
            ctx_rva = read_uint32(self.data, off + 24)
            ctx_size = read_uint32(self.data, off + 28)
            self.threads.append((tid, teb, stk_rva, stk_size, ctx_rva, ctx_size))

    def _parse_exception(self, rva, size):
        if rva + 40 > len(self.data): return
        self.exception_thread_id = read_uint32(self.data, rva)
        self.exception_flags = read_uint32(self.data, rva + 4)
        # exception_record at rva+16
        er_rva = rva + 16
        if er_rva + 32 > len(self.data): return
        self.exception_code = read_uint32(self.data, er_rva)
        self.fault_addr = read_uint64(self.data, er_rva + 16)  # ExceptionAddress
        # Violation flags: bit 0=READ, bit 1=WRITE, bit 8=EXECUTE (ExceptionInformation[0])
        ei_rva = read_uint32(self.data, er_rva + 24)  # ExceptionInformation RVA
        ei_count = read_uint32(self.data, er_rva + 28)
        if ei_rva and ei_count > 0 and ei_rva + 8 <= len(self.data):
            self.violation_info = (read_uint64(self.data, ei_rva),  # 0=Read,1=Write,8=Exec
                                   read_uint64(self.data, ei_rva + 8))  # accessed address
        else:
            self.violation_info = (None, None)
        # CONTEXT64 at exception_record + offset 224
        ctx_rva = read_uint32(self.data, er_rva + 224)
        self._parse_context(ctx_rva)

    def _parse_context(self, ctx_rva):
        if ctx_rva + 128 > len(self.data): return
        ctx_flags = read_uint64(self.data, ctx_rva)
        self.context = {
            'rip': read_uint64(self.data, ctx_rva + 248) if ctx_rva + 256 <= len(self.data) else 0,
            'rsp': read_uint64(self.data, ctx_rva + 336) if ctx_rva + 344 <= len(self.data) else 0,
            'rbp': read_uint64(self.data, ctx_rva + 320) if ctx_rva + 328 <= len(self.data) else 0,
            'rcx': read_uint64(self.data, ctx_rva + 120) if ctx_rva + 128 <= len(self.data) else 0,
            'rdx': read_uint64(self.data, ctx_rva + 128) if ctx_rva + 136 <= len(self.data) else 0,
            'r8':  read_uint64(self.data, ctx_rva + 160) if ctx_rva + 168 <= len(self.data) else 0,
            'r9':  read_uint64(self.data, ctx_rva + 168) if ctx_rva + 176 <= len(self.data) else 0,
        }

    def _resolve_module(self, addr):
        for base, sz, name, _, _ in self.modules:
            if base <= addr < base + sz:
                return name, addr - base
        return None, None

    def _resolve_stack(self):
        """Walk the stack from RSP using return addresses."""
        if not self.context or not self.threads:
            return
        rsp = self.context.get('rsp', 0)
        rip = self.context.get('rip', 0)
        self.call_stack = []
        mod_name, offset = self._resolve_module(rip)
        if mod_name:
            self.call_stack.append((mod_name, offset, rip))
        # Walk: each QWORD at [rsp+8*i] might be a return address (heuristic)
        # We walk up to 30 frames
        if rsp == 0: return
        if rsp + 240 > len(self.data): return
        for i in range(1, 31):
            entry = read_uint64(self.data, rsp + 8 * i)
            if entry == 0 or entry > 0x7FFFFFFFFFFF: break
            mod, off = self._resolve_module(entry)
            if mod:
                self.call_stack.append((mod, off, entry))
            # Stop at ntdll/kernel32 frame
            if mod and ('ntdll' in mod.lower() or 'kernel32' in mod.lower() or 'kernelbase' in mod.lower()):
                break

def analyze_file(path):
    mp = MinidumpParser(path)
    result = mp.parse()
    if result != "OK":
        print(f"  ERROR: {result}")
        return

    print(f"  Exception: 0x{mp.exception_code:08x}", end="")
    if mp.exception_code == 0xc0000005:
        vi = getattr(mp, 'violation_info', (None, None))
        if vi[0] is not None:
            ops = {0: 'READ', 1: 'WRITE', 8: 'EXECUTE'}
            print(f" ({ops.get(vi[0], f'info0={vi[0]}')})", end="")
            if vi[1] is not None:
                print(f" addr=0x{vi[1]:016x}", end="")
    print()

    mod_name, mod_offset = mp._resolve_module(mp.fault_addr) if mp.fault_addr else (None, None)
    print(f"  Fault addr: 0x{mp.fault_addr or 0:016x} -> {mod_name or '?'} + 0x{mod_offset or 0:x}")
    print(f"  Thread ID: {mp.exception_thread_id}")
    print(f"  RIP=0x{mp.context.get('rip',0):016x}  RSP=0x{mp.context.get('rsp',0):016x}")

    if mp.call_stack:
        print(f"  Call stack ({len(mp.call_stack)} frames):")
        for i, (mod, off, addr) in enumerate(mp.call_stack[:10]):
            mod_short = mod.split('\\')[-1]
            print(f"    [{i}] {mod_short} + 0x{off:x}")

    print(f"  Modules: {len(mp.modules)} total")
    apex_modules = [(n, b, s) for b, s, n, g, a in mp.modules if 'AW_Core' in n or 'DAW_Core' in n or 'apex' in n.lower()]
    for name, base, sz in apex_modules[:5]:
        print(f"    {name.split(chr(92))[-1]} base=0x{base:016x} size=0x{sz:x}")

if __name__ == '__main__':
    dmp_dir = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\Builds\VisualStudio2026\crash-reports"
    files = sorted(glob.glob(os.path.join(dmp_dir, '*.dmp')))
    print(f"Found {len(files)} dump files")
    for f in files:
        print(f"\n{'='*60}")
        print(f"FILE: {os.path.basename(f)}")
        analyze_file(f)
