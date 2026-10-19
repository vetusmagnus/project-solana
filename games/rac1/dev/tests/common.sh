# dev/tests/common.sh: settings the test scripts share (sourced, not run).
#
#   RAC1_ISO   your PAL disc image (required)
#   WRAPPER    the build to test (default: build/linux/rac1_wrapper of this tree)
#   OUT        where runs go (default: dev/tests/out)
#
# The game is run headless from a level jump (UYA_LEVEL=N@300), with the intro movies
# skipped and a fixed set of key presses, so two runs of the same build see the same inputs.
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
R=$(cd "$T/../.." && pwd)
: "${RAC1_ISO:?set RAC1_ISO to your disc image, e.g. export RAC1_ISO=\$HOME/rac1.iso}"
WRAPPER=${WRAPPER:-$R/build/linux/rac1_wrapper}
OUT=${OUT:-$T/out}
mkdir -p "$OUT"
if [ ! -x "$WRAPPER" ]; then
    echo "no build at $WRAPPER"
    echo "build it with every level: python3 build.py --iso \"\$RAC1_ISO\" --levels 00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18 -j 8"
    exit 1
fi
# key presses: name@vsync+length (platform_linux.c lists the names)
STILL_KEYS="cross@1000+10"
WALK_KEYS="cross@1300+10,lup@1400+220,rright@1500+80,lleft@1620+100,rleft@1680+60"
# lines that mean a run went wrong (4 "unimplemented call" lines at boot are normal and not counted)
problems() { grep -v "^ALSA lib" "$1" | grep -ciE "rc_bad|crash|SIGSEGV|segmentation|[^e]fault"; }
