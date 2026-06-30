# Azahar NX JIT V6 — host-provided tick budget

This patch targets the current Azahar NX main branch with the earlier dual-alias
JIT and V5 post-SVC changes already applied locally.

## Diagnosis

The V5 adjustment successfully moved execution past `CallSVC(0x21)`:

```text
CallSVC leave
after jit->Run ... pc=0x00106d2c
RunLoop iteration 2 completed
```

The next `jit->Run()` then faults before executing the guest block. The exception
shows:

```text
PC = X16 = FAR = RX base of JIT range 1
active run entry = JIT range 6, offset 0x1888
```

In the pinned Dynarmic AArch64 prelude, every `Run()` invokes
`GetTicksRemaining` through a generated trampoline before branching to the guest
block. That trampoline loads an indirect target into X16.

V6 removes that generated timing callback on Nintendo Switch:

1. `A32Core::Run()` calls `GetTicksRemaining()` in ordinary C++.
2. The result is passed as the fourth AAPCS64 argument, in X3.
3. The generated `run_code` prelude copies X3 into Dynarmic's `Xticks`.
4. Other platforms retain the original generated callback path.

V6 also corrects the V5 accounting bug. V5 set both `cycles_to_run` and `Xticks`
to zero after SVC, causing `AddTicks(0)`. V6 preserves the original stack budget
and sets only `Xticks` to zero, so Dynarmic consumes the remaining scheduling
budget before returning.

## Apply

```bash
cd ~/Documentos/Repos/azahar

rm -rf azahar-nx-jit-v6-host-tick-budget
unzip azahar-nx-jit-v6-host-tick-budget.zip

python3 azahar-nx-jit-v6-host-tick-budget/apply.py .
```

## Clean build

```bash
cmake --build build-switch \
  --target azahar_switch \
  --clean-first \
  -- -j2
```

## Launch

```bash
/opt/devkitpro/tools/bin/nxlink \
  -a 192.168.68.107 \
  -s build-switch/bin/Release/azahar.nro
```

Required startup marker:

```text
[Switch.JIT] diagnostics_version=6 host_tick_budget_argument=enabled post_callback_budget_accounting=enabled
```

Expected progression:

```text
CallSVC leave swi=0x00000021
after jit->Run ... pc=0x00106d2c
RunLoop iteration 2 completed
RunLoop iteration 3 ...
after jit->Run
```

## Files modified

```text
externals/dynarmic/src/dynarmic/backend/arm64/address_space.h
externals/dynarmic/src/dynarmic/backend/arm64/a32_core.h
externals/dynarmic/src/dynarmic/backend/arm64/a32_address_space.cpp
externals/dynarmic/src/dynarmic/backend/arm64/emit_arm64_a32.cpp
src/switch/switch_jit.cpp
```

## Git submodule warning

The functional files are inside `externals/dynarmic`, which is a Git submodule.
A parent-repository commit cannot store dirty submodule source files. Commit the
changes to a Dynarmic fork, then update the parent repository's submodule pointer.
