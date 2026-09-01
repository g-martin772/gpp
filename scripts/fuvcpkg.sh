#!/usr/bin/env bash
set -euo pipefail
# find a suitable system libvulkan target
SYS_TARGET=""
for p in /usr/lib /usr/lib64 /lib /lib64; do
  if [ -e "$p/libvulkan.so.1" ]; then SYS_TARGET=$(readlink -f "$p/libvulkan.so.1") && break; fi
  if [ -e "$p/libvulkan.so" ]; then SYS_TARGET=$(readlink -f "$p/libvulkan.so") && break; fi
done
if [ -z "$SYS_TARGET" ]; then echo "ERROR: no system libvulkan found under /usr/lib or /lib." >&2; exit 1; fi

echo "Using system Vulkan target: $SYS_TARGET"

for dir in build/vcpkg_installed/x64-linux/debug/lib build/vcpkg_installed/x64-linux/lib; do
  [ -d "$dir" ] || continue
  echo "Processing $dir"
  for name in libvulkan.so libvulkan.so.1 libvulkan.so.1.4.357; do
    src="$dir/$name"
    if [ -L "$src" ]; then echo "  already symlink: $src -> $(readlink -f "$src")"; continue; fi
    if [ -e "$src" ]; then
      echo "  backing up existing file $src -> ${src}.orig"
      mv "$src" "${src}.orig"
    fi
    echo "  creating symlink $src -> $SYS_TARGET"
    ln -s "$SYS_TARGET" "$src"
  done
done

# Verify
echo '--- readelf build/scratch RUNPATH ---'
readelf -d build/scratch | sed -n '1,200p' | grep -i runpath || true

echo '--- ldd build/scratch (vulkan) ---'
ldd build/scratch | grep -i vulkan || true

# Show created symlinks
echo '--- created symlinks ---'
ls -l build/vcpkg_installed/x64-linux/debug/lib/libvulkan.so* || true
ls -l build/vcpkg_installed/x64-linux/lib/libvulkan.so* || true