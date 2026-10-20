#!/usr/bin/env python3
"""Bounded host-only ARM32 boot investigation using Unicorn.

Calls actual constructors/JNI_OnLoad; stops at unsupported host services rather
than inventing successful results. Not a Switch runtime or a playable game.
Requires unicorn 2.1.4. The donor is not included.
"""
import argparse
from collections import Counter
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import threading
import tempfile
import time
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR, UC_ARM_REG_FPEXC

BASE, HEAP, STACK, THUNKS, RETURN = 0x10000000, 0x30000000, 0x20000000, 0x60000000, 0x70000000
EXPECTED = "0ed42b611415015807f759ec9b5457857143ce39"
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]


class BootProbe:
    def __init__(self, library, data_root):
        self.raw = Path(library).read_bytes()
        if hashlib.sha1(self.raw).hexdigest() != EXPECTED:
            raise ValueError("Unsupported game library")
        self.data_root = Path(data_root).resolve()
        self.libm = ctypes.CDLL("libm.so.6")
        self.sandbox = tempfile.TemporaryDirectory(prefix="deadspacemobile-boot-")
        self.save_root = Path(self.sandbox.name)
        self.files = {}
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.uc.mem_map(HEAP, 128 * 1024**2)
        self.uc.mem_map(STACK, 2 * 1024**2)
        self.uc.mem_map(THUNKS, 1024 * 1024)
        self.uc.mem_map(RETURN, 4096)
        self.uc.mem_map(0x40000000, 256 * 1024**2)
        self.program_break = 0x40000000
        self.mmap_top = 0x50000000
        self.next_heap = HEAP
        self.allocations = {}
        self.callbacks = []
        self.callback_addresses = {}
        self.calls = Counter()
        self.trace = []
        self.classes, self.objects, self.methods = {}, {}, {}
        self.method_info = {}
        self.singletons = {}
        self.local_frames = [set()]
        self.monitors = {}
        self.mutexes, self.guards = {}, set()
        self.mutex_attrs = {}
        self.semaphores = {}
        self.tls = {}
        self.destructors = []
        self.symbols = {}
        self.init_array = []
        self.stage = "load"
        self.error = None
        self.load()
        header = (Path(__file__).parents[2] / "jni/jni.h").read_text()
        body = header.split("struct JNINativeInterface {")[1].split("};")[0]
        names = ["reserved0", "reserved1", "reserved2", "reserved3"] + re.findall(r"\(\*(\w+)\)\s*\(", body)
        self.jni_table = self.alloc(len(names) * 4)
        for i, name in enumerate(names):
            if i >= 4:
                self.put32(self.jni_table + i * 4, self.thunk("JNI:" + name))
        self.env = self.alloc(4)
        self.put32(self.env, self.jni_table)
        vm_table = self.alloc(8 * 4)
        for i, name in enumerate([None, None, None, "DestroyJavaVM", "AttachCurrentThread", "DetachCurrentThread", "GetEnv", "AttachCurrentThreadAsDaemon"]):
            if name:
                self.put32(vm_table + i * 4, self.thunk("VM:" + name))
        self.vm = self.alloc(4)
        self.put32(self.vm, vm_table)
        self.errno = self.alloc(4)
        self.uc.hook_add(UC_HOOK_CODE, self.on_thunk, begin=THUNKS, end=THUNKS + 1024 * 1024 - 1)

    def alloc(self, size):
        size = max(16, (size + 15) & ~15)
        if self.next_heap + size > HEAP + 128 * 1024**2:
            raise MemoryError("Guest heap exhausted")
        p = self.next_heap
        self.next_heap += size
        self.allocations[p] = size
        return p

    def read(self, p, n):
        if n < 0 or n > 64 * 1024**2:
            raise ValueError("Memory operation size bound")
        return bytes(self.uc.mem_read(p, n))

    def u32(self, p):
        return struct.unpack("<I", self.read(p, 4))[0]

    def put32(self, p, value):
        self.uc.mem_write(p, struct.pack("<I", value & 0xffffffff))

    def cstring(self, p):
        if not p:
            raise ValueError("Null string pointer")
        out = bytearray()
        for i in range(1024 * 1024):
            b = self.read(p + i, 1)
            if b == b"\0":
                return bytes(out)
            out += b
        raise ValueError("Unterminated guest string")

    def text(self, p):
        return self.cstring(p).decode("utf-8", "replace")

    def guest_string(self, value):
        value = value.encode() if isinstance(value, str) else value
        p = self.alloc(len(value) + 1)
        self.uc.mem_write(p, value + b"\0")
        return p

    def thunk(self, name):
        if name not in self.callback_addresses:
            addr = THUNKS + len(self.callbacks) * 8
            if addr >= THUNKS + 1024 * 1024:
                raise ValueError("Thunk space exhausted")
            self.callbacks.append(name)
            self.callback_addresses[name] = addr
            self.uc.mem_write(addr, struct.pack("<II", 0xef000001, 0xe12fff1e))
        return self.callback_addresses[name]

    def load(self):
        b = self.raw
        h = struct.unpack_from("<16sHHIIIIIHHHHHH", b)
        phdrs = [struct.unpack_from("<8I", b, h[5] + i * h[9]) for i in range(h[10])]
        sections = [struct.unpack_from("<10I", b, h[6] + i * h[11]) for i in range(h[12])]
        end = max(p[2] + p[5] for p in phdrs if p[0] == 1)
        self.uc.mem_map(BASE, (end + 4095) & ~4095)
        for p in phdrs:
            if p[0] == 1:
                self.uc.mem_write(BASE + p[2], b[p[1]:p[1] + p[4]])
        tables = {}
        for i, s in enumerate(sections):
            if s[1] != 11:
                continue
            strings = sections[s[6]]
            data = b[strings[4]:strings[4] + strings[5]]
            entries = []
            for off in range(s[4], s[4] + s[5], 16):
                n, value, size, info, other, index = struct.unpack_from("<IIIBBH", b, off)
                name = data[n:].split(b"\0")[0].decode("ascii")
                entries.append((name, value, index, info & 15))
                if index and name:
                    self.symbols[name] = value if index == 0xfff1 else BASE + value
            tables[i] = entries
        for s in sections:
            if s[1] != 9:
                continue
            for off in range(s[4], s[4] + s[5], 8):
                target, info = struct.unpack_from("<II", b, off)
                kind, symbol = info & 255, info >> 8
                if not kind:
                    continue
                p = BASE + target
                addend = self.u32(p)
                if kind == 23:
                    value = BASE + addend
                else:
                    name, value, index, typ = tables[s[6]][symbol]
                    if index:
                        value = value if index == 0xfff1 else BASE + value
                    elif typ == 1 or name in ["_ctype_", "_tolower_tab_", "_toupper_tab_", "__sF", "__stack_chk_guard", "__page_size", "__dso_handle", "tzname"]:
                        value = self.symbols.get(name) or self.alloc(4096)
                        if name == "__stack_chk_guard":
                            self.put32(value, 0xa5a55a5a)
                        if name == "__page_size":
                            self.put32(value, 4096)
                        if name in ["_tolower_tab_", "_toupper_tab_"] and name not in self.symbols:
                            table = self.alloc(257 * 2)
                            vals = [-1] + [i + 32 if name == "_tolower_tab_" and 65 <= i <= 90 else i - 32 if name == "_toupper_tab_" and 97 <= i <= 122 else i for i in range(256)]
                            self.uc.mem_write(table, struct.pack("<257h", *vals))
                            self.put32(value, table)
                        if name == "_ctype_" and name not in self.symbols:
                            table = self.alloc(257)
                            vals = [0]
                            for c in range(256):
                                flag = (1 if 65 <= c <= 90 else 0) | (2 if 97 <= c <= 122 else 0) | (4 if 48 <= c <= 57 else 0)
                                flag |= 8 if c in [9, 10, 11, 12, 13, 32] else 0
                                flag |= 32 if c < 32 or c == 127 else 0
                                flag |= 64 if 48 <= c <= 57 or 65 <= c <= 70 or 97 <= c <= 102 else 0
                                flag |= 128 if c == 32 else 0
                                flag |= 16 if 33 <= c <= 126 and not (flag & 7) else 0
                                vals.append(flag)
                            self.uc.mem_write(table, bytes(vals))
                            self.put32(value, table)
                        self.symbols[name] = value
                    else:
                        value = self.thunk(name)
                    if kind == 2:
                        value += addend
                    elif kind == 3:
                        value += addend - p
                    elif kind not in [21, 22]:
                        raise ValueError("Unsupported relocation " + str(kind))
                self.put32(p, value)
        # The 2011 linker labels .init_array PROGBITS, so use dynamic tags.
        for s in sections:
            if s[1] == 6:
                tags = dict(struct.unpack_from("<II", b, off) for off in range(s[4], s[4] + s[5], 8))
                if 25 in tags:
                    self.init_array.extend(self.u32(BASE + tags[25] + n) for n in range(0, tags.get(27, 0), 4))

    def arg(self, n):
        return self.uc.reg_read(REGS[n]) if n < 4 else self.u32(self.uc.reg_read(UC_ARM_REG_SP) + (n - 4) * 4)

    def on_thunk(self, uc, address, size, _):
        try:
            index = (address - THUNKS) // 8
            name = self.callbacks[index]
            self.calls[name] += 1
            if self.calls[name] <= 3:
                self.trace.append(f"{self.stage}: {name}")
            result = self.dispatch(name)
            if isinstance(result, tuple) and result[0] == "forward":
                _, target, args = result
                for i, value in enumerate(args):
                    if i < 4:
                        uc.reg_write(REGS[i], value)
                    else:
                        self.put32(uc.reg_read(UC_ARM_REG_SP) + (i - 4) * 4, value)
                uc.reg_write(UC_ARM_REG_PC, target)
                return
            if result is not None:
                if isinstance(result, tuple) and result[0] == "wide":
                    uc.reg_write(UC_ARM_REG_R1, (result[1] >> 32) & 0xffffffff)
                    result = result[1]
                uc.reg_write(UC_ARM_REG_R0, int(result) & 0xffffffff)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        except Exception as e:
            self.error = f"{type(e).__name__}: {e}; pc=0x{address:08x}, lr=0x{uc.reg_read(UC_ARM_REG_LR):08x}"
            uc.emu_stop()

    def dispatch(self, name):
        a = [self.arg(i) for i in range(4)]
        if name == "stat":
            path = self.resolve_path(self.text(a[0]))
            self.trace.append("stat " + str(path.relative_to(self.data_root) if path.is_relative_to(self.data_root) else path.name))
            try:
                st = path.stat()
            except OSError as e:
                self.put32(self.errno, e.errno)
                return -1
            raw = bytearray(104)
            for offset, fmt, value in [(0,"Q",st.st_dev),(12,"I",st.st_ino & 0xffffffff),(16,"I",st.st_mode),(20,"I",st.st_nlink),(24,"I",st.st_uid),(28,"I",st.st_gid),(32,"Q",st.st_rdev),(48,"q",st.st_size),(56,"I",st.st_blksize),(64,"Q",st.st_blocks),(96,"Q",st.st_ino)]:
                struct.pack_into("<" + fmt, raw, offset, value)
            for offset, value in [(72,st.st_atime_ns),(80,st.st_mtime_ns),(88,st.st_ctime_ns)]:
                struct.pack_into("<II", raw, offset, (value // 10**9) & 0xffffffff, value % 10**9)
            self.uc.mem_write(a[1], bytes(raw))
            return 0
        math_names = ["acos", "asin", "atan", "atan2", "ceil", "cos", "exp", "floor", "fmod", "log", "log10", "pow", "sin", "sqrt"]
        base = name[:-1] if name.endswith("f") else name
        if base in math_names:
            single = name.endswith("f")
            fmt, ctype, words = ("<f", ctypes.c_float, 1) if single else ("<d", ctypes.c_double, 2)
            count = 2 if base in ["atan2", "fmod", "pow"] else 1
            values = [struct.unpack(fmt, struct.pack("<" + "I" * words, *a[i * words:(i + 1) * words]))[0] for i in range(count)]
            fn = getattr(self.libm, name)
            fn.argtypes, fn.restype = [ctype] * count, ctype
            bits = int.from_bytes(struct.pack(fmt, fn(*values)), "little")
            return bits if single else ("wide", bits)
        if name == "VM:GetEnv" or name in ["VM:AttachCurrentThread", "VM:AttachCurrentThreadAsDaemon"]:
            self.put32(a[1], self.env)
            return 0
        if name.startswith("JNI:"):
            op = name[4:]
            if op in ["MonitorEnter", "MonitorExit"]:
                if a[1] not in self.objects:
                    raise ValueError("JNI monitor on unknown object")
                lock = self.monitors.setdefault(a[1], threading.RLock())
                if op == "MonitorEnter":
                    if not lock.acquire(blocking=False):
                        raise NotImplementedError("Contended JNI monitor requires guest scheduler")
                else:
                    lock.release()
                return 0
            if op in ["PushLocalFrame", "EnsureLocalCapacity"]:
                if a[1] & 0x80000000 or a[1] > 65536:
                    return 0xffffffff
                if op == "PushLocalFrame":
                    self.local_frames.append(set())
                return 0
            if op == "PopLocalFrame":
                if len(self.local_frames) == 1:
                    raise ValueError("JNI local frame underflow")
                self.local_frames.pop()
                if a[1]:
                    self.local_frames[-1].add(a[1])
                return a[1]
            if op == "FindClass":
                key = self.text(a[1])
                if key not in self.classes:
                    self.classes[key] = self.alloc(4)
                    self.objects[self.classes[key]] = {"class": key}
                self.trace.append("FindClass " + key)
                return self.classes[key]
            if op in ["GetMethodID", "GetStaticMethodID"]:
                key = (a[1], self.text(a[2]), self.text(a[3]))
                if key not in self.methods:
                    self.methods[key] = self.alloc(4)
                    self.method_info[self.methods[key]] = key
                self.trace.append(op + " " + str(key))
                return self.methods[key]
            if op in ["NewGlobalRef", "NewLocalRef"]:
                if op == "NewLocalRef" and a[1]:
                    self.local_frames[-1].add(a[1])
                return a[1]
            if op == "GetObjectClass":
                cls = self.objects[a[1]]["class"]
                if cls not in self.classes:
                    self.classes[cls] = self.alloc(4)
                    self.objects[self.classes[cls]] = {"class": cls}
                return self.classes[cls]
            if op in ["DeleteGlobalRef", "DeleteLocalRef", "ExceptionClear"]:
                return None
            if op in ["ExceptionCheck", "ExceptionOccurred"]:
                return 0
            if op == "GetVersion":
                return 0x10006
            if op == "GetJavaVM":
                self.put32(a[1], self.vm)
                return 0
            if op == "IsSameObject":
                return int(a[1] == a[2])
            array_types = {"Boolean": 1, "Byte": 1, "Char": 2, "Short": 2, "Int": 4, "Long": 8, "Float": 4, "Double": 8}
            for typ, stride in array_types.items():
                if op == "New" + typ + "Array":
                    if a[1] > 8 * 1024**2:
                        raise ValueError("Java array size exceeds bound")
                    ref, data = self.alloc(4), self.alloc(a[1] * stride)
                    self.objects[ref] = {"class": "[" + typ, "data": data, "length": a[1], "stride": stride}
                    return ref
                if op == "Get" + typ + "ArrayElements":
                    obj = self.objects[a[1]]
                    if a[2]:
                        self.uc.mem_write(a[2], b"\0")
                    return obj["data"]
                if op == "Release" + typ + "ArrayElements":
                    if self.objects[a[1]]["data"] != a[2]:
                        raise ValueError("ReleaseArrayElements pointer mismatch")
                    return None
                if op in ["Set" + typ + "ArrayRegion", "Get" + typ + "ArrayRegion"]:
                    obj = self.objects[a[1]]
                    if a[2] + a[3] > obj["length"]:
                        raise ValueError("JNI array region outside array")
                    buffer = self.arg(4)
                    data = obj["data"] + a[2] * stride
                    dst, src = (data, buffer) if op.startswith("Set") else (buffer, data)
                    self.uc.mem_write(dst, self.read(src, a[3] * stride))
                    return None
            if op == "GetArrayLength":
                return self.objects[a[1]]["length"]
            if op == "GetObjectArrayElement":
                return self.objects[a[1]]["elements"][a[2]]
            if op in ["NewStringUTF", "NewString"]:
                text = self.text(a[1]) if op == "NewStringUTF" else self.read(a[1], a[2] * 2).decode("utf-16-le")
                return self.java_string(text)
            if op in ["GetStringUTFLength", "GetStringLength"]:
                text = self.objects[a[1]]["text"]
                return len(text.encode("utf-8")) if op == "GetStringUTFLength" else len(text.encode("utf-16-le")) // 2
            if op in ["GetStringUTFChars", "GetStringChars"]:
                obj = self.objects[a[1]]
                key = "utf8" if op == "GetStringUTFChars" else "utf16"
                if key not in obj:
                    raw = obj["text"].encode("utf-8" if key == "utf8" else "utf-16-le") + (b"\0" if key == "utf8" else b"\0\0")
                    obj[key] = self.alloc(len(raw))
                    self.uc.mem_write(obj[key], raw)
                if a[2]:
                    self.uc.mem_write(a[2], b"\1")
                return obj[key]
            if op in ["ReleaseStringUTFChars", "ReleaseStringChars"]:
                return None
            if op in ["NewObject", "NewObjectV", "NewObjectA"]:
                cls = self.objects[a[1]]["class"]
                ref = self.alloc(4)
                self.objects[ref] = {"class": cls}
                return ref
            if op.startswith("Call") and "Method" in op:
                clsref, method, sig = self.method_info[a[2]]
                cls = self.objects[clsref]["class"]
                values = self.method_args(sig, a[3], op.endswith("A")) if op.endswith(("V", "A")) else [a[3]]
                self.trace.append("Java call " + cls + "." + method + sig)
                if cls == "com/ea/blast/MainActivity" and method == "GetInstance":
                    return self.singleton(cls)
                if cls == "com/ea/blast/MainActivity" and method == "getAssets":
                    return self.singleton("android/content/res/AssetManager")
                if cls == "android/content/res/AssetManager" and method in ["open", "openFd", "list"]:
                    name = self.objects[values[0]]["text"]
                    name = name.removeprefix("appbundle:/").removeprefix("appbundle:")
                    root = (self.data_root / "assets").resolve()
                    path = (root / name).resolve()
                    if not path.is_relative_to(root):
                        raise ValueError("Asset path outside data root")
                    self.trace.append("Asset " + method + " " + name)
                    ref = self.alloc(4)
                    if method == "list":
                        elements = [self.java_string(p.name) for p in sorted(path.iterdir())]
                        self.objects[ref] = {"class": "[Ljava/lang/String;", "length": len(elements), "elements": elements}
                    else:
                        raw = path.read_bytes()  # Missing resources stop the investigation.
                        self.objects[ref] = {"class": "java/io/InputStream" if method == "open" else "android/content/res/AssetFileDescriptor", "bytes": raw, "position": 0, "closed": False}
                    return ref
                if cls in ["java/io/InputStream", "android/content/res/AssetFileDescriptor"]:
                    obj = self.objects[a[1]]
                    if obj["closed"]:
                        raise ValueError("Use of closed asset stream")
                    if method == "close":
                        obj["closed"] = True
                        return None
                    if method == "getLength":
                        return ("wide", len(obj["bytes"]))
                    if method == "skip":
                        count = min(max(0, values[0] if values[0] < 2**63 else values[0] - 2**64), len(obj["bytes"]) - obj["position"])
                        obj["position"] += count
                        return ("wide", count)
                    if method == "read" and sig == "([BII)I":
                        array, offset, length = values
                        target = self.objects[array]
                        if offset > target["length"] or length > target["length"] - offset:
                            raise ValueError("Asset read outside Java byte array")
                        raw = obj["bytes"][obj["position"]:obj["position"] + length]
                        if raw:
                            self.uc.mem_write(target["data"] + offset, raw)
                            obj["position"] += len(raw)
                        return len(raw) if raw or length == 0 else -1
                if cls == "com/ea/EAIO/EAIO" and method == "Startup":
                    return ("forward", self.symbols["Java_com_ea_EAIO_EAIO_Startup"], [self.env, 0, values[0]])
                if cls == "com/ea/blast/SystemAndroidDelegate":
                    props = dict(re.findall(r'X\((\w+),\s*"([^"]*)"\)', (Path(__file__).parents[2] / "jni/classes/ea_SystemAndroidDelegate.h").read_text()))
                    props["GetDeviceModel"] = "Host ARM32 boot probe"
                    if method in props:
                        return self.java_string(props[method])
                if cls == "com/ea/blast/GetAppDataDirectoryDelegate" and method in ["GetAppDataDirectory", "GetExternalStorageDirectory"]:
                    return self.java_string(str(self.data_root))
                if cls == "com/ea/blast/DisplayAndroidDelegate":
                    display = {"GetDefaultWidth": 480, "GetDefaultHeight": 640, "GetStdOrientation": 0}
                    if method in display:
                        return display[method]
                    if method in ["GetDpiX", "GetDpiY"]:
                        return struct.unpack("<I", struct.pack("<f", 229))[0]
                    if method == "SetStdOrientation":
                        self.objects[a[1]]["orientation_requested"] = values[0]
                        return None
                if cls == "com/ea/blast/PowerManagerAndroid" and method == "ApplyKeepAwake":
                    self.objects[a[1]]["keep_awake_requested"] = bool(values[0])
                    self.trace.append("Host investigation records keep-awake request only")
                    return None
                if cls == "com/eamobile/Query":
                    if method == "isContentReady":
                        return int(all((self.data_root / p).is_file() for p in ["assets/EAMCore.ini", "assets/published/data/achievements.sb"]))
                    if method == "getVersion":
                        return self.java_string("1.1.33")
                    if method == "getTotalMemory":
                        return ("wide", 256)
                if cls == "com/ea/blast/TouchSurfaceAndroid" and method == "IsTouchScreenMultiTouch":
                    return 0  # The host investigation injects no multi-touch events.
                if cls == "com/ea/blast/DeviceOrientationHandlerAndroidDelegate":
                    if method in ["OnLifeCycleFocusGained", "OnLifeCycleFocusLost"]:
                        self.objects[a[1]]["focused"] = method.endswith("Gained")
                        return None
                    if method == "SetEnabled":
                        self.objects[a[1]]["enabled"] = bool(values[0])
                        return None
                if cls in ["com/ea/blast/AccelerometerAndroidDelegate", "com/ea/blast/GyroscopeAndroidDelegate"]:
                    if method in ["SetEnabled", "SetUpdateFrequency"]:
                        self.objects[a[1]][method] = values[0]
                        self.trace.append("Host sensor configuration only; no samples injected")
                        return None
                raise NotImplementedError("Java " + cls + "." + method + sig)
            raise NotImplementedError(name)
        if name in ["malloc", "calloc"]:
            return self.alloc(a[0] if name == "malloc" else a[0] * a[1])
        if name == "realloc":
            if not a[0]:
                return self.alloc(a[1])
            n = self.alloc(a[1])
            self.uc.mem_write(n, self.read(a[0], min(self.allocations[a[0]], a[1])))
            return n
        if name == "free":
            if a[0] and a[0] not in self.allocations:
                raise ValueError("free of unknown guest allocation")
            return None
        if name in ["memcpy", "memmove"]:
            self.uc.mem_write(a[0], self.read(a[1], a[2]))
            return a[0]
        if name == "memset":
            if a[2] > 64 * 1024**2:
                raise ValueError("memset exceeds bound")
            self.uc.mem_write(a[0], bytes([a[1] & 255]) * a[2])
            return a[0]
        if name == "memcmp":
            x, y = self.read(a[0], a[2]), self.read(a[1], a[2])
            return (x > y) - (x < y)
        if name == "strlen":
            return len(self.cstring(a[0]))
        if name in ["strcmp", "strncmp"]:
            x, y = self.cstring(a[0]), self.cstring(a[1])
            if name == "strncmp":
                x, y = x[:a[2]], y[:a[2]]
            return (x > y) - (x < y)
        if name in ["strcpy", "strncpy", "strcat"]:
            x = self.cstring(a[1])
            dst = a[0] + (len(self.cstring(a[0])) if name == "strcat" else 0)
            x = x[:a[2]].ljust(a[2], b"\0") if name == "strncpy" else x + b"\0"
            self.uc.mem_write(dst, x)
            return a[0]
        if name == "strdup":
            return self.guest_string(self.cstring(a[0]))
        if name == "atoi":
            text = self.text(a[0]).lstrip()
            match = re.match(r"[+-]?\d+", text)
            return int(match.group()) if match else 0
        if name in ["strchr", "strrchr", "strstr"]:
            x = self.cstring(a[0]) + b"\0"
            needle = self.cstring(a[1]) if name == "strstr" else bytes([a[1] & 255])
            pos = x.rfind(needle) if name == "strrchr" else x.find(needle)
            return a[0] + pos if pos >= 0 else 0
        if name == "__aeabi_atexit":
            self.destructors.append(tuple(a[:3]))
            return 0
        if name == "__cxa_guard_acquire":
            return int(self.read(a[0], 1) == b"\0")
        if name == "__cxa_guard_release":
            self.uc.mem_write(a[0], b"\1")
            return None
        if name == "getenv":
            return 0  # no environment variables configured in this host probe
        if name == "sbrk":
            increment = a[0] if a[0] < 0x80000000 else a[0] - 2**32
            old = self.program_break
            new = old + increment
            if new < 0x40000000 or new > self.mmap_top:
                self.put32(self.errno, 12)
                return 0xffffffff
            self.program_break = new
            return old
        if name == "mmap":
            if a[0] or not (a[3] & 0x20):
                raise NotImplementedError("non-anonymous or fixed guest mmap")
            size = (a[1] + 4095) & ~4095
            if self.mmap_top - size < self.program_break:
                self.put32(self.errno, 12)
                return 0xffffffff
            self.mmap_top -= size
            return self.mmap_top
        if name == "munmap":
            raise NotImplementedError("guest munmap reclamation")
        if name == "__errno":
            return self.errno
        if name == "clock":
            return int(time.process_time() * 1000000)
        if name == "gettimeofday":
            now = time.time_ns()
            if a[0]:
                self.uc.mem_write(a[0], struct.pack("<ii", now // 10**9, now % 10**9 // 1000))
            if a[1]:
                self.uc.mem_write(a[1], b"\0" * 8)
            return 0
        if name == "clock_gettime":
            clocks = {0: time.time_ns, 1: time.monotonic_ns, 2: time.process_time_ns}
            if a[0] not in clocks:
                raise NotImplementedError("guest clock id " + str(a[0]))
            now = clocks[a[0]]()
            self.uc.mem_write(a[1], struct.pack("<ii", now // 10**9, now % 10**9))
            return 0
        if name == "time":
            seconds = int(time.time())
            if a[0]:
                self.put32(a[0], seconds)
            return seconds
        if name == "pthread_mutex_init":
            attr = self.mutex_attrs.get(a[1], {"type": 0, "pshared": 0})
            if attr["pshared"]:
                raise NotImplementedError("process-shared guest mutex")
            self.mutexes[a[0]] = threading.RLock() if attr["type"] == 1 else threading.Lock()
            return 0
        if name in ["pthread_mutex_lock", "pthread_mutex_trylock"]:
            lock = self.mutexes.setdefault(a[0], threading.RLock())
            if lock.acquire(blocking=False):
                return 0
            if name == "pthread_mutex_trylock":
                return 16
            raise NotImplementedError("contended guest mutex wait")
        if name == "pthread_mutex_unlock":
            self.mutexes[a[0]].release()
            return 0
        if name == "pthread_mutex_destroy":
            self.mutexes.pop(a[0], None)
            return 0
        if name == "pthread_mutexattr_init":
            self.mutex_attrs[a[0]] = {"type": 0, "pshared": 0}
            return 0
        if name in ["pthread_mutexattr_settype", "pthread_mutexattr_setpshared"]:
            key = "type" if name.endswith("settype") else "pshared"
            if a[1] not in ([0, 1, 2] if key == "type" else [0]):
                raise NotImplementedError("unsupported mutex attribute")
            self.mutex_attrs[a[0]][key] = a[1]
            return 0
        if name == "pthread_mutexattr_destroy":
            del self.mutex_attrs[a[0]]
            return 0
        if name == "pthread_self":
            return 1
        if name == "sem_init":
            if a[1] or a[2] > 0x7fffffff:
                raise NotImplementedError("unsupported semaphore attributes")
            self.semaphores[a[0]] = a[2]
            return 0
        if name == "sem_post":
            self.semaphores[a[0]] += 1
            return 0
        if name in ["sem_wait", "sem_trywait", "sem_timedwait"]:
            if self.semaphores[a[0]]:
                self.semaphores[a[0]] -= 1
                return 0
            if name == "sem_trywait":
                self.put32(self.errno, 11)
                return 0xffffffff
            raise NotImplementedError("blocking semaphore wait needs guest thread scheduler")
        if name == "sem_getvalue":
            self.put32(a[1], self.semaphores[a[0]])
            return 0
        if name == "sem_destroy":
            del self.semaphores[a[0]]
            return 0
        if name == "pthread_key_create":
            self.put32(a[0], len(self.tls) + 1)
            self.tls[len(self.tls) + 1] = 0
            return 0
        if name == "pthread_getspecific":
            return self.tls.get(a[0], 0)
        if name == "pthread_setspecific":
            self.tls[a[0]] = a[1]
            return 0
        if name == "__atomic_inc" or name == "__atomic_dec":
            old = self.u32(a[0])
            self.put32(a[0], old + (1 if name.endswith("inc") else -1))
            return old
        if name == "__atomic_cmpxchg":
            old = self.u32(a[2])
            if old == a[0]:
                self.put32(a[2], a[1])
                return 0
            return 1
        if name == "__atomic_swap":
            old = self.u32(a[1])
            self.put32(a[1], a[0])
            return old
        raise NotImplementedError(name + " args=" + str([hex(v) for v in a]))

    def singleton(self, cls):
        if cls not in self.singletons:
            ref = self.alloc(4)
            self.objects[ref] = {"class": cls}
            self.singletons[cls] = ref
        return self.singletons[cls]

    def resolve_path(self, name, writing=False):
        if name.startswith("appbundle:"):
            path = self.data_root / "assets" / name[len("appbundle:"):].lstrip("/")
        elif name.startswith(str(self.data_root)):
            path = Path(name)
        elif not name.startswith("/"):
            path = self.data_root / name
        else:
            raise ValueError("Guest path outside game data: " + name)
        path = path.resolve()
        if not path.is_relative_to(self.data_root):
            raise ValueError("Guest path escapes game data")
        relative = path.relative_to(self.data_root)
        # Keep all experimental saves outside the supplied data tree.
        if relative.parts and relative.parts[0] in ["files", "save", "saves", "documents"]:
            return self.save_root / relative
        if relative.parts[:2] == ("published", "var") or relative.parts[:3] == ("assets", "published", "var"):
            return self.save_root / "published" / "var" / Path(*relative.parts[relative.parts.index("var") + 1:])
        if relative.parts and relative.parts[0] == "published":
            path = self.data_root / "assets" / relative
        if writing:
            raise ValueError("Write to immutable donor data: " + str(relative))
        return path

    def java_string(self, text):
        ref = self.alloc(4)
        self.objects[ref] = {"class": "java/lang/String", "text": text}
        return ref

    def method_args(self, signature, pointer, array=False):
        types = re.findall(r"\[*L[^;]*;|\[*[ZBCSIJFD]", signature.split(")")[0][1:])
        values = []
        for typ in types:
            wide = typ in ["J", "D"] or (typ == "F" and not array)
            if not array and wide:
                pointer = (pointer + 7) & ~7
            values.append(struct.unpack("<Q" if wide else "<I", self.read(pointer, 8 if wide else 4))[0])
            pointer += 8 if array or wide else 4
        return values

    def call(self, address, args=(), label="guest"):
        self.stage = label
        self.error = None
        self.uc.reg_write(UC_ARM_REG_CPSR, 0x10)
        self.uc.reg_write(UC_ARM_REG_SP, STACK + 2 * 1024**2 - 256)
        self.uc.reg_write(UC_ARM_REG_LR, RETURN)
        for i in range(4):
            self.uc.reg_write(REGS[i], args[i] if i < len(args) else 0)
        for i, value in enumerate(args[4:]):
            self.put32(self.uc.reg_read(UC_ARM_REG_SP) + i * 4, value)
        try:
            self.uc.emu_start(address, RETURN, timeout=15_000_000, count=10_000_000)
        except Exception as e:
            raise RuntimeError(f"{label}: {e}; pc=0x{self.uc.reg_read(UC_ARM_REG_PC):08x}") from e
        if self.error:
            raise RuntimeError(self.error)
        if self.uc.reg_read(UC_ARM_REG_PC) != RETURN:
            raise RuntimeError(label + ": instruction/time budget exhausted")
        return self.uc.reg_read(UC_ARM_REG_R0)

    def run(self, stage, frames=1):
        completed = 0
        for i, fn in enumerate(self.init_array):
            if fn not in [0, 0xffffffff]:
                self.call(fn, label=f"constructor[{i}]")
                completed += 1
        self.trace.append(f"Constructors returned: {completed}")
        version = self.call(self.symbols["JNI_OnLoad"], (self.vm, 0), "JNI_OnLoad")
        self.trace.append(f"JNI_OnLoad returned: 0x{version:08x}")
        if version != 0x10004:
            raise RuntimeError("Unexpected JNI version")
        if stage != "onload":
            track = self.alloc(4)
            self.objects[track] = {"class": "android/media/AudioTrack", "state": "initialized"}
            self.call(self.symbols["Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init"], (self.env, 0x42424242, track, 1024 * 1024, 2, 44100), "EAAudioCore_Init")
            self.trace.append("EAAudioCore_Init returned")
        if stage in ["create", "surface", "frame"]:
            self.call(self.symbols["Java_com_ea_blast_MainActivity_NativeOnCreate"], label="NativeOnCreate")
            self.trace.append("NativeOnCreate returned")
        if stage in ["surface", "frame"]:
            self.call(self.symbols["Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceCreated"], label="NativeOnSurfaceCreated")
            self.trace.append("NativeOnSurfaceCreated returned")
        if stage == "frame":
            self.call(self.symbols["Java_com_ea_blast_KeyboardAndroid_NativeOnVisibilityChanged"], (self.env, 0x42424242, 600, 1), "NativeOnVisibilityChanged")
            for frame in range(frames):
                self.call(self.symbols["Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame"], label=f"NativeOnDrawFrame[{frame}]")
                self.trace.append(f"NativeOnDrawFrame[{frame}] returned")
        return {"constructors": completed, "jni_version": hex(version), "stage": stage, "frames_returned": frames if stage == "frame" else 0}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("library", type=Path)
    p.add_argument("data_root", type=Path)
    p.add_argument("--stage", choices=["onload", "audio", "create", "surface", "frame"], default="onload")
    p.add_argument("--output", type=Path)
    p.add_argument("--frames", type=int, default=1)
    args = p.parse_args()
    if not 1 <= args.frames <= 1000:
        p.error("--frames must be between 1 and 1000")
    probe = BootProbe(args.library, args.data_root)
    try:
        result = {"status": "passed", "result": probe.run(args.stage, args.frames)}
    except Exception as e:
        result = {"status": "blocked", "error": str(e), "stage": probe.stage}
    result.update({"trace": probe.trace, "calls": dict(probe.calls), "host_only": True,
                   "switch_tested": False, "game_playable": False,
                   "limitations": ["single-thread host boot investigation", "guest heap is an arena; free does not recycle", "JNI references are retained; local/global reference reclamation not implemented", "virtual handset profile; keep-awake and sensor configuration recorded only", "content gate checks resource sentinels, not campaign completeness", "no GL success stubs; stops before rendering", "not a production JNI implementation"]})
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text)
    return 0 if result["status"] == "passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
