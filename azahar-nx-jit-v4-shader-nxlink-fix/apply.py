#!/usr/bin/env python3
from __future__ import annotations

import difflib
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: Path) -> str:
    if not path.is_file():
        fail(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def patch_shader_cpp(text: str) -> str:
    # The V3 Oaknut patch intentionally makes ptr() the executable alias.
    # Azahar's ARM64 PICA shader compiler still copied bytes through ptr(),
    # causing a write-permission fault at the RX base.
    program_old = "reinterpret_cast<std::byte*>(code_mem->ptr())"
    program_new = "reinterpret_cast<std::byte*>(code_mem->xptr())"
    if program_old in text:
        text = text.replace(program_old, program_new, 1)

    memcpy_old = "std::memcpy(code_mem->ptr(), code_vec.data(), code_vec.size() * sizeof(u32));"
    memcpy_new = "std::memcpy(code_mem->wptr(), code_vec.data(), code_vec.size() * sizeof(u32));"
    if memcpy_old in text:
        text = text.replace(memcpy_old, memcpy_new, 1)

    # On Switch, invalidate_all() records the dirty RW range and protect()
    # performs D-cache clean + I-cache invalidation for the RX alias.
    order_old = "    code_mem->protect();\n    code_mem->invalidate_all();"
    order_new = "    code_mem->invalidate_all();\n    code_mem->protect();"
    if order_old in text:
        text = text.replace(order_old, order_new, 1)

    required = [
        "reinterpret_cast<std::byte*>(code_mem->xptr())",
        "std::memcpy(code_mem->wptr(), code_vec.data(), code_vec.size() * sizeof(u32));",
        "    code_mem->invalidate_all();\n    code_mem->protect();",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        fail("shader_jit_a64_compiler.cpp layout is not compatible with this patch")
    return text


def patch_shader_h(text: str) -> str:
    old = "reinterpret_cast<const std::byte*>(code_mem->ptr())"
    new = "reinterpret_cast<const std::byte*>(code_mem->xptr())"
    if old in text:
        text = text.replace(old, new, 1)
    if new not in text:
        fail("shader_jit_a64_compiler.h: could not select executable alias")
    return text


def patch_nxlink(text: str) -> str:
    # switch_nxlink.cpp is intentionally isolated from Azahar common headers,
    # so including switch.h here does not recreate the global u128 conflict.
    wrapped = 'extern "C" {\n#include <switch/runtime/nxlink.h>\n}'
    direct = '#include <switch/runtime/nxlink.h>'
    if wrapped in text:
        text = text.replace(wrapped, "#include <switch.h>", 1)
    elif direct in text:
        text = text.replace(direct, "#include <switch.h>", 1)
    elif "#include <switch.h>" not in text:
        fail("switch_nxlink.cpp: nxlink/libnx include layout changed")

    atomic_anchor = "std::atomic_int socket_fd{-1};"
    atomic_replacement = (
        "std::atomic_int socket_fd{-1};\n"
        "std::atomic_bool socket_service_owned{false};"
    )
    if "socket_service_owned" not in text:
        if atomic_anchor not in text:
            fail("switch_nxlink.cpp: socket_fd declaration not found")
        text = text.replace(atomic_anchor, atomic_replacement, 1)

    init_old = """    const int fd = nxlinkStdioForDebug();
    if (fd < 0) {
        return false;
    }

    socket_fd.store(fd, std::memory_order_release);"""
    init_new = """    const Result socket_result = socketInitializeDefault();
    if (R_FAILED(socket_result)) {
        return false;
    }
    socket_service_owned.store(true, std::memory_order_release);

    const int fd = nxlinkStdioForDebug();
    if (fd < 0) {
        socket_service_owned.store(false, std::memory_order_release);
        socketExit();
        return false;
    }

    socket_fd.store(fd, std::memory_order_release);"""
    if "socketInitializeDefault()" not in text:
        if init_old not in text:
            fail("switch_nxlink.cpp: Initialize() body changed")
        text = text.replace(init_old, init_new, 1)

    shutdown_old = """    const int fd = socket_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd < 0) {
        return;
    }

    std::fflush(stderr);
    close(fd);"""
    shutdown_new = """    const int fd = socket_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        std::fflush(stderr);
        close(fd);
    }

    if (socket_service_owned.exchange(false, std::memory_order_acq_rel)) {
        socketExit();
    }"""
    if "socket_service_owned.exchange" not in text:
        if shutdown_old not in text:
            fail("switch_nxlink.cpp: Shutdown() body changed")
        text = text.replace(shutdown_old, shutdown_new, 1)

    required = [
        "#include <switch.h>",
        "socketInitializeDefault()",
        "nxlinkStdioForDebug()",
        "socketExit();",
        "socket_service_owned",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        fail("switch_nxlink.cpp validation failed")
    return text


def validate_oaknut(text: str) -> None:
    required = [
        "AZAHAR_SWITCH_DUAL_ALIAS_JIT_V3",
        "std::uint32_t* wptr() const",
        "std::uint32_t* xptr() const",
        "return xptr();",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        fail(
            "Oaknut V3 dual-alias patch is not present. "
            "Apply azahar-nx-dynarmic-libnx-jit-v3 first."
        )


def make_diff(root: Path, before: dict[Path, str], after: dict[Path, str]) -> str:
    chunks: list[str] = []
    for path in after:
        rel = path.relative_to(root).as_posix()
        chunks.extend(
            difflib.unified_diff(
                before[path].splitlines(keepends=True),
                after[path].splitlines(keepends=True),
                fromfile=f"a/{rel}",
                tofile=f"b/{rel}",
            )
        )
    return "".join(chunks)


def main() -> None:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    shader_cpp = root / "src/video_core/shader/shader_jit_a64_compiler.cpp"
    shader_h = root / "src/video_core/shader/shader_jit_a64_compiler.h"
    nxlink_cpp = root / "src/switch/switch_nxlink.cpp"
    oaknut_h = root / "externals/oaknut/include/oaknut/code_block.hpp"

    before = {
        shader_cpp: read(shader_cpp),
        shader_h: read(shader_h),
        nxlink_cpp: read(nxlink_cpp),
    }
    validate_oaknut(read(oaknut_h))

    after = {
        shader_cpp: patch_shader_cpp(before[shader_cpp]),
        shader_h: patch_shader_h(before[shader_h]),
        nxlink_cpp: patch_nxlink(before[nxlink_cpp]),
    }

    if all(before[path] == after[path] for path in after):
        print("Already applied: JIT V4 shader and nxlink corrections are present.")
        return

    # Validate all transformations before touching the repository.
    if "code_mem->ptr()" in after[shader_cpp] or "code_mem->ptr()" in after[shader_h]:
        fail("validation failed: PICA shader JIT still uses ambiguous ptr()")
    if after[shader_cpp].find("code_mem->invalidate_all();") > after[shader_cpp].find(
        "code_mem->protect();"
    ):
        fail("validation failed: shader cache synchronization order is incorrect")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = root / ".azahar-patch-backups" / f"jit-v4-shader-nxlink-{stamp}"
    backup.mkdir(parents=True, exist_ok=False)

    for path, content in before.items():
        target = backup / path.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    for path, content in after.items():
        path.write_text(content, encoding="utf-8")

    diff_path = root / "azahar-nx-jit-v4-shader-nxlink-fix.diff"
    diff_path.write_text(make_diff(root, before, after), encoding="utf-8")

    (backup / "RESTORE.txt").write_text(
        f"Restore with:\n  cp -a '{backup}/.' '{root}/'\n",
        encoding="utf-8",
    )

    print("Applied JIT V4 shader RW/RX and nxlink socket initialization fix.")
    print(f"Backup: {backup}")
    print(f"Diff:   {diff_path}")
    print("Rebuild the existing azahar_switch target; CMake reconfiguration is not required.")


if __name__ == "__main__":
    main()
