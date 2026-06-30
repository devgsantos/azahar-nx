#!/usr/bin/env python3
from __future__ import annotations

import difflib
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path


EXPECTED_DYNARMIC = "e77b1ba0b7da7cbe93021b01a663acfe7c4dd516"
RUN_MARKER = "AZAHAR_SWITCH_HOST_TICK_BUDGET_V6"
POST_CALLBACK_MARKER = "AZAHAR_SWITCH_POST_HOST_CALLBACK_BUDGET_V6"
RUNTIME_MARKER = (
    "diagnostics_version=6 host_tick_budget_argument=enabled "
    "post_callback_budget_accounting=enabled"
)


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: Path) -> str:
    if not path.is_file():
        fail(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def git_output(repo: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=repo,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return result.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def patch_address_space_h(source: str) -> str:
    if RUN_MARKER in source:
        return source

    pattern = re.compile(
        r"(?P<indent>[ \t]*)using RunCodeFuncType = HaltReason \(\*\)"
        r"\(CodePtr entry_point, void\* jit_state, volatile u32\* halt_reason\);"
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        fail(
            "address_space.h: expected one RunCodeFuncType declaration, "
            f"found {len(matches)}"
        )

    match = matches[0]
    indent = match.group("indent")
    new = (
        f"{indent}// {RUN_MARKER}\n"
        "#if defined(__SWITCH__)\n"
        f"{indent}// Horizon receives the cycle budget from ordinary C++ code. This avoids\n"
        f"{indent}// entering a generated virtual-callback trampoline before every block.\n"
        f"{indent}using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, void* jit_state,\n"
        f"{indent}                                           volatile u32* halt_reason,\n"
        f"{indent}                                           u64 ticks_to_run);\n"
        "#else\n"
        f"{indent}using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, void* jit_state,\n"
        f"{indent}                                           volatile u32* halt_reason);\n"
        "#endif"
    )
    return source[: match.start()] + new + source[match.end() :]

def patch_a32_core_h(source: str) -> str:
    if RUN_MARKER in source:
        return source

    run_pattern = re.compile(
        r"(?P<indent>[ \t]*)return process\.prelude_info\.run_code"
        r"\(entry_point, &thread_ctx, halt_reason\);"
    )
    step_pattern = re.compile(
        r"(?P<indent>[ \t]*)return process\.prelude_info\.step_code"
        r"\(entry_point, &thread_ctx, halt_reason\);"
    )

    run_matches = list(run_pattern.finditer(source))
    step_matches = list(step_pattern.finditer(source))
    if len(run_matches) != 1:
        fail(f"a32_core.h: expected one run_code invocation, found {len(run_matches)}")
    if len(step_matches) != 1:
        fail(f"a32_core.h: expected one step_code invocation, found {len(step_matches)}")

    run_match = run_matches[0]
    indent = run_match.group("indent")
    run_new = (
        "#if defined(__SWITCH__)\n"
        f"{indent}// {RUN_MARKER}\n"
        f"{indent}// Resolve the virtual timing callback in C++ and pass its result in X3.\n"
        f"{indent}// Generated code no longer tail-branches through X16 merely to obtain\n"
        f"{indent}// the initial cycle budget.\n"
        f"{indent}const u64 ticks_to_run = process.conf.enable_cycle_counting\n"
        f"{indent}                             ? process.conf.callbacks->GetTicksRemaining()\n"
        f"{indent}                             : 0;\n"
        f"{indent}return process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason,\n"
        f"{indent}                                     ticks_to_run);\n"
        "#else\n"
        f"{indent}return process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason);\n"
        "#endif"
    )
    source = source[: run_match.start()] + run_new + source[run_match.end() :]

    # Re-search after the first replacement so offsets remain valid.
    step_matches = list(step_pattern.finditer(source))
    if len(step_matches) != 1:
        fail(
            "a32_core.h: step_code invocation changed unexpectedly after run patch"
        )
    step_match = step_matches[0]
    indent = step_match.group("indent")
    step_new = (
        "#if defined(__SWITCH__)\n"
        f"{indent}// The generated single-step prelude retains its fixed one-cycle budget;\n"
        f"{indent}// the fourth argument only keeps the Switch function signature ABI-correct.\n"
        f"{indent}return process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason, 1);\n"
        "#else\n"
        f"{indent}return process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason);\n"
        "#endif"
    )
    return source[: step_match.start()] + step_new + source[step_match.end() :]

def patch_a32_address_space_cpp(source: str) -> str:
    if RUN_MARKER in source:
        return source

    pattern = re.compile(
        r"(?P<indent>[ \t]*)if \(conf\.enable_cycle_counting\) \{\n"
        r"(?P=indent)    code\.BL\(prelude_info\.get_ticks_remaining\);\n"
        r"(?P=indent)    code\.MOV\(Xticks, X0\);\n"
        r"(?P=indent)    code\.STR\(Xticks, SP, offsetof\(StackLayout, cycles_to_run\)\);\n"
        r"(?P=indent)\}"
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        fail(
            "a32_address_space.cpp: expected one run-entry GetTicksRemaining block, "
            f"found {len(matches)}"
        )

    match = matches[0]
    indent = match.group("indent")
    new = (
        f"{indent}if (conf.enable_cycle_counting) {{\n"
        "#if defined(__SWITCH__)\n"
        f"{indent}    // {RUN_MARKER}\n"
        f"{indent}    // A32Core passes the budget as the fourth AAPCS64 argument (X3).\n"
        f"{indent}    // Copy it before entering guest code; do not invoke the generated\n"
        f"{indent}    // GetTicksRemaining trampoline on Horizon.\n"
        f"{indent}    code.MOV(Xticks, X3);\n"
        "#else\n"
        f"{indent}    code.BL(prelude_info.get_ticks_remaining);\n"
        f"{indent}    code.MOV(Xticks, X0);\n"
        "#endif\n"
        f"{indent}    code.STR(Xticks, SP, offsetof(StackLayout, cycles_to_run));\n"
        f"{indent}}}"
    )
    return source[: match.start()] + new + source[match.end() :]

def patch_emit_arm64_a32_cpp(source: str) -> str:
    if POST_CALLBACK_MARKER in source:
        return source

    pattern = re.compile(
        r"(?P<indent>[ \t]*)code\.MOV\(X0, 0\);\n"
        r"(?P=indent)code\.STR\(X0, SP, offsetof\(StackLayout, cycles_to_run\)\);\n"
        r"(?P=indent)code\.MOV\(Xticks, X0\);"
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 2:
        fail(
            "emit_arm64_a32.cpp: expected two V5 post-callback budget sequences, "
            f"found {len(matches)}. Apply the V5 patch first or inspect local changes."
        )

    def replacement(match: re.Match[str]) -> str:
        indent = match.group("indent")
        return (
            f"{indent}// {POST_CALLBACK_MARKER}\n"
            f"{indent}// Keep the original cycles_to_run stack value. Setting only Xticks\n"
            f"{indent}// to zero makes return_from_run_code report the full remaining budget\n"
            f"{indent}// through AddTicks instead of incorrectly reporting zero elapsed cycles.\n"
            f"{indent}code.MOV(Xticks, 0);"
        )

    source = pattern.sub(replacement, source)

    # Preserve the prior marker as history but make the active policy unambiguous.
    source = source.replace(
        "AZAHAR_SWITCH_POST_HOST_CALLBACK_FORCE_EXIT_V5",
        "AZAHAR_SWITCH_POST_HOST_CALLBACK_FORCE_EXIT_V5_SUPERSEDED",
    )
    return source

def patch_switch_jit_cpp(source: str) -> str:
    if RUNTIME_MARKER in source:
        return source

    pattern = re.compile(r'AppendJitLog\("diagnostics_version=[^"]*"\);')
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        fail(
            "switch_jit.cpp: expected exactly one diagnostics_version marker, "
            f"found {len(matches)}"
        )

    replacement = f'AppendJitLog("{RUNTIME_MARKER}");'
    match = matches[0]
    return source[: match.start()] + replacement + source[match.end() :]


def make_diff(repo: Path, before: dict[Path, str], after: dict[Path, str]) -> str:
    chunks: list[str] = []
    for path, new_content in after.items():
        old_content = before[path]
        if old_content == new_content:
            continue
        rel = path.relative_to(repo).as_posix()
        chunks.extend(
            difflib.unified_diff(
                old_content.splitlines(keepends=True),
                new_content.splitlines(keepends=True),
                fromfile=f"a/{rel}",
                tofile=f"b/{rel}",
            )
        )
    return "".join(chunks)


def main() -> None:
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    dynarmic = repo / "externals/dynarmic"
    paths = {
        "address_space_h": dynarmic / "src/dynarmic/backend/arm64/address_space.h",
        "a32_core_h": dynarmic / "src/dynarmic/backend/arm64/a32_core.h",
        "a32_address_space_cpp": (
            dynarmic / "src/dynarmic/backend/arm64/a32_address_space.cpp"
        ),
        "emit_arm64_a32_cpp": (
            dynarmic / "src/dynarmic/backend/arm64/emit_arm64_a32.cpp"
        ),
        "switch_jit_cpp": repo / "src/switch/switch_jit.cpp",
    }

    before = {path: read(path) for path in paths.values()}
    after = {
        paths["address_space_h"]: patch_address_space_h(
            before[paths["address_space_h"]]
        ),
        paths["a32_core_h"]: patch_a32_core_h(before[paths["a32_core_h"]]),
        paths["a32_address_space_cpp"]: patch_a32_address_space_cpp(
            before[paths["a32_address_space_cpp"]]
        ),
        paths["emit_arm64_a32_cpp"]: patch_emit_arm64_a32_cpp(
            before[paths["emit_arm64_a32_cpp"]]
        ),
        paths["switch_jit_cpp"]: patch_switch_jit_cpp(
            before[paths["switch_jit_cpp"]]
        ),
    }

    if all(before[path] == after[path] for path in after):
        print("Already applied: Azahar NX JIT V6 host tick-budget patch.")
        return

    # Validate the complete policy before modifying the repository.
    combined = "\n".join(after.values())
    if combined.count(RUN_MARKER) < 3:
        fail("validation failed: V6 host-budget marker missing from required files")
    if combined.count(POST_CALLBACK_MARKER) != 2:
        fail("validation failed: expected two V6 post-callback budget markers")
    if RUNTIME_MARKER not in after[paths["switch_jit_cpp"]]:
        fail("validation failed: V6 runtime marker missing")
    if (
        "code.BL(prelude_info.get_ticks_remaining);"
        not in after[paths["a32_address_space_cpp"]]
    ):
        fail("validation failed: non-Switch fallback was removed")
    if (
        "process.conf.callbacks->GetTicksRemaining()"
        not in after[paths["a32_core_h"]]
    ):
        fail("validation failed: C++ tick-budget resolution missing")

    dynarmic_head = git_output(dynarmic, "rev-parse", "HEAD")
    if dynarmic_head and dynarmic_head != EXPECTED_DYNARMIC:
        print(
            "WARNING: Dynarmic HEAD differs from the pinned revision:\n"
            f"  expected {EXPECTED_DYNARMIC}\n"
            f"  actual   {dynarmic_head}\n"
            "The patch was applied by validated source patterns."
        )

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = repo / ".azahar-patch-backups" / f"jit-v6-host-tick-budget-{stamp}"
    backup.mkdir(parents=True, exist_ok=False)

    for path, content in before.items():
        destination = backup / path.relative_to(repo)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8")

    for path, content in after.items():
        path.write_text(content, encoding="utf-8")

    diff_path = repo / "azahar-nx-jit-v6-host-tick-budget.diff"
    diff_path.write_text(make_diff(repo, before, after), encoding="utf-8")

    print("Applied Azahar NX JIT V6 host tick-budget patch.")
    print(f"Backup: {backup}")
    print(f"Diff:   {diff_path}")
    print()
    print("Modified files:")
    for path in after:
        if before[path] != after[path]:
            print(f"  {path.relative_to(repo)}")
    print()
    print("Clean rebuild required:")
    print(
        "  cmake --build build-switch --target azahar_switch "
        "--clean-first -- -j2"
    )
    print()
    print("NOTE: externals/dynarmic is a Git submodule. Commit these changes in a")
    print("Dynarmic fork before updating the parent repository submodule pointer.")


if __name__ == "__main__":
    main()
