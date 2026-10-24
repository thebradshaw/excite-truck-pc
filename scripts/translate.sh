#!/usr/bin/env bash
# Translate Excite Truck's main.dol into C++ with the Wiicompiled translator.
# Needs: .NET 8+ SDK, and Assets/main.dol extracted from your own disc
# (e.g. `nodtool extract "Excite Truck (USA).wbfs" extracted` then copy extracted/sys/main.dol).
set -euo pipefail
cd "$(dirname "$0")/.."
WC=third_party/wiicompiled
CLI=$WC/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
[ -f Assets/main.dol ] || { echo "Missing Assets/main.dol (extract it from your own disc)"; exit 1; }
[ -f "$CLI" ] || dotnet build $WC/translator/src/Translator.Cli/Translator.Cli.csproj -c Release
P=projects/excite-truck/recomp.yml
dotnet "$CLI" translate-recursive 0x80004170 --project $P --threads "$(nproc)"
dotnet "$CLI" generate-data-init --project $P
