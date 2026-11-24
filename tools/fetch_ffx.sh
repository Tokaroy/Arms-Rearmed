#!/bin/sh
# AMD FSR 3 for the Windows build (runtime/nvn/vulkan/fsr3.cpp): the FidelityFX API's C headers (MIT) and AMD's
# signed Vulkan DLLs (AMD's binary redistribution license: Kits/FidelityFX/docs/license.md, copied next to them and
# shipped in the release as AMD-FidelityFX-LICENSE.txt), from the FSR SDK's pinned tag, each checked against its
# SHA-256. Nothing of it goes into the repository. Usage: tools/fetch_ffx.sh [dir] (default build/deps-win/ffx)
set -e
DIR=${1:-build/deps-win/ffx}
TAG=v2.4.0
BASE=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$TAG/Kits/FidelityFX
mkdir -p "$DIR/api/include/vk" "$DIR/upscalers/include" "$DIR/bin"  # (the SDK's own layout: its headers include each other by it)
get() {  # <path under Kits/FidelityFX> <destination> <sha256>
    if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status 2>/dev/null; then return 0; fi
    curl -sfL --retry 3 "$BASE/$1" -o "$2.part"
    echo "$3  $2.part" | sha256sum -c --status || { echo "fetch_ffx: $1: unexpected SHA-256" >&2; rm -f "$2.part"; exit 1; }
    mv "$2.part" "$2"
}
get api/include/ffx_api.h "$DIR/api/include/ffx_api.h" 4d9dcf243ef5efbf03340a02f16b51a22053243a22e8a9a0b3ce91c26982b4bf
get api/include/ffx_api_types.h "$DIR/api/include/ffx_api_types.h" 53f07c113a9ba493f297656e4481b6539aed0afc9f3cc8da682aa477d270c46c
get api/include/vk/ffx_api_vk.h "$DIR/api/include/vk/ffx_api_vk.h" 09ac1855bc4c2f711243d6982909385072ca3f8d30db44d1a82ee5bd637832ad
get upscalers/include/ffx_upscale.h "$DIR/upscalers/include/ffx_upscale.h" 13091e7fe5fd9c82b1fb7c044b9f08f664d9653f599d2ea500738709236fca4d
get signedbin/amd_fidelityfx_loader_vk.dll "$DIR/bin/amd_fidelityfx_loader_vk.dll" 38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9
get signedbin/amd_fidelityfx_upscaler_vk.dll "$DIR/bin/amd_fidelityfx_upscaler_vk.dll" 4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7
curl -sfL --retry 3 "$BASE/docs/license.md" -o "$DIR/LICENSE-AMD-FidelityFX.md"
touch "$DIR/.ok"
echo "fetch_ffx: AMD FidelityFX ($TAG) in $DIR"
