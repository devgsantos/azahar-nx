# Azahar NX JIT V4 correction

This patch is intended to be applied after:

1. `azahar-nx-dynarmic-libnx-jit-v3`
2. `azahar-nx-nxlink-live-logs-v2`
3. `azahar-nx-nxlink-c-linkage-fix`

It fixes two issues confirmed by the latest exception dump:

- The ARM64 PICA shader JIT copied generated code through `CodeBlock::ptr()`. Under V3, `ptr()` is the RX alias, so the copy caused a level-3 write-permission fault. The compiler now writes through `wptr()` and executes through `xptr()`.
- The Switch network backend is a stub and does not initialize BSD sockets. The nxlink helper now calls `socketInitializeDefault()` before `nxlinkStdioForDebug()` and calls `socketExit()` during cleanup.

## Apply

```bash
cd ~/Documentos/Repos/azahar
unzip azahar-nx-jit-v4-shader-nxlink-fix.zip
python3 azahar-nx-jit-v4-shader-nxlink-fix/apply.py .
```

## Build

```bash
cmake --build build-switch --target azahar_switch -- -j2
```

## Launch

Open hbmenu in application mode, enable NetLoader, then:

```bash
/opt/devkitpro/tools/bin/nxlink \
  -a 192.168.68.107 \
  -s build-switch/bin/Release/azahar.nro
```

Expected early log:

```text
nxlink live stderr initialization result=connected
[Azahar.NxLink] connected fd=...
```

The previous exception should no longer report `FAR` equal to the RX base of a 4096-byte JIT allocation.
