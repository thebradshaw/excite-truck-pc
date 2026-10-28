#!/usr/bin/env bash
# Regenerate ET address rows, retranslate, rebuild and boot once. Log: run.log
set -e
W=/home/claude/work; cd $W/tools
T0="dotnet $W/wc-et/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
if [ "${REMAP:-0}" = 1 ]; then
  (cd $W/wc-et && $T0 translate-recursive 0x80004170 --project projects/excite-truck/recomp.yml --outdir generated/functions --output-metadata generated/base_translation_output.json --production-source-bundle generated/base_translation_sources.bin --no-function-files --prune-stale --threads 4 > $W/translate0.log 2>&1)
  python3 sigmatch.py $W/extracted/sys/main.dol $W/dolphin/Data/Sys/totaldb.dsy $W/wc-et/projects/excite-truck/MAP.txt > $W/sdk_names.txt 2>/dev/null
fi
ET_WORK=$W python3 align.py >/dev/null 2>&1
python3 gen_et_versions.py $W/mk2et.json $W/wiicompiled/projects/mkwii/MAP.txt $W/datamap.txt > $W/et_versions.inc 2>/dev/null
python3 - <<'PY'
p='/home/claude/work/wc-et/runtime/include/guest_abi/guest_versions.inc'
s=open(p).read(); i=s.find('// Excite Truck (REXE01)')
open(p,'w').write(s[:i]+open('/home/claude/work/et_versions.inc').read())
PY
python3 datamap.py > $W/datamap.txt 2>/dev/null
python3 gen_et_versions.py $W/mk2et.json $W/wiicompiled/projects/mkwii/MAP.txt $W/datamap.txt > $W/et_versions.inc 2>/dev/null
python3 - <<'PY'
p='/home/claude/work/wc-et/runtime/include/guest_abi/guest_versions.inc'
s=open(p).read(); i=s.find('// Excite Truck (REXE01)')
open(p,'w').write(s[:i]+open('/home/claude/work/et_versions.inc').read())
PY
cd $W/wc-et
T="dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"; P=projects/excite-truck/recomp.yml
if [ "${RETRANSLATE:-1}" = 1 ]; then
$T translate-recursive 0x80004170 --project $P --outdir generated/functions --output-metadata generated/base_translation_output.json --production-source-bundle generated/base_translation_sources.bin --no-function-files --prune-stale --threads 4 > $W/translate.log 2>&1
$T emit-build-shards --project $P --base-metadata generated/base_translation_output.json --base-functions-dir generated/functions --native-source-dir runtime/src --out generated/build_shards >> $W/translate.log 2>&1
fi
ninja -C build-linux -j4 WiiCompiled > $W/build.log 2>&1 || { grep -m5 ' error' $W/build.log; exit 1; }
cd $W && [ "${NORUN:-0}" = 1 ] && exit 0; cd $W && xvfb-run -a -s "-screen 0 1280x720x24" timeout -s KILL ${RUNSECS:-90} ./wc-et/build-linux/WiiCompiled > run.log 2>&1 || true
grep -vE 'Loaded|ALSA|pw.conf|^\s|Warning|\[aurora\]|libEGL' run.log > run.short.log || true
