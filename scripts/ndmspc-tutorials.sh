#!/bin/bash
# Run the ndmspc tutorials into the current directory.
#
# Start in an empty, writable directory: every output file is created here.
#
#   mkdir ndmspc-tutorials && cd ndmspc-tutorials
#   ndmspc-tutorials
#
# The tutorial macros are read from the ndmspc installation (next to this script,
# or from NDMSPC_ROOT / NDMSPC_DIR when set). The hsimple.root sample is also
# fetched here with curl, as a local fallback for when ROOT cannot read files
# over HTTP (e.g. root-net-http is not installed).

set -u
set -o pipefail

HSIMPLE_URL="https://root.cern/js/files/hsimple.root"

export NDMSPC_LOG_CONSOLE="${NDMSPC_LOG_CONSOLE:-0}"
export NDMSPC_LOG_RUN="${NDMSPC_LOG_RUN:-1}"

STEPS=(
  "tutorial/02_storage/NStorage01Gaus.C"
  "tutorial/03_parameter/NParameter01Gaus.C"
  "tutorial/04_customization/NCustomization01Gaus.C"
  "tutorial/05_single_bining/NSingleBinning01Gaus.C"
  'tutorial/05_single_bining/NSingleBinning01Gaus.C("NNested01Gaus.root")'
  "tutorial/06_binnings/NBinnings01Gaus.C"
  'tutorial/06_binnings/NBinnings01Gaus.C("NBinnings01Gaus_odd.root",true)'
  "tutorial/06_binnings/NBinnings01GausStress.C"
  "macros/root/cernstaff/tree500_cernbuild.C"
)

usage()
{
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run the ndmspc tutorials in the current directory. All files the tutorials
produce are written here, so start in an empty, writable directory.

The hsimple.root sample is downloaded from
$HSIMPLE_URL
as a local fallback for when ROOT cannot open files over HTTP.

Options:
  -h, --help   Show this help and exit
  -l, --list   List the tutorial steps and exit
EOF
}

find_ndmspc_root()
{
  local candidate script_dir
  for candidate in "${NDMSPC_ROOT:-}" "${NDMSPC_DIR:-}"; do
    if [ -n "$candidate" ] && [ -d "$candidate/tutorial" ]; then
      (cd "$candidate" && pwd -P)
      return 0
    fi
  done
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
  for candidate in "$script_dir/../share/ndmspc" "$script_dir/.."; do
    if [ -d "$candidate/tutorial" ]; then
      (cd "$candidate" && pwd -P)
      return 0
    fi
  done
  return 1
}

download_hsimple()
{
  local tmp=".hsimple.root.part"

  if [ -s hsimple.root ]; then
    echo "hsimple.root already present, keeping it"
    return 0
  fi

  echo "Downloading hsimple.root from $HSIMPLE_URL"
  if command -v curl >/dev/null 2>&1; then
    if curl -fsSL --retry 3 -o "$tmp" "$HSIMPLE_URL"; then
      mv -f "$tmp" hsimple.root
      return 0
    fi
  elif command -v wget >/dev/null 2>&1; then
    if wget -q -O "$tmp" "$HSIMPLE_URL"; then
      mv -f "$tmp" hsimple.root
      return 0
    fi
  else
    echo "Neither curl nor wget found, cannot download hsimple.root" >&2
    return 1
  fi

  rm -f "$tmp"
  echo "Could not download hsimple.root, continuing without it" >&2
  return 1
}

case "${1:-}" in
  -h|--help)
    usage
    exit 0
    ;;
  -l|--list)
    printf '%s\n' "${STEPS[@]}"
    exit 0
    ;;
  "")
    ;;
  *)
    echo "Unknown option: $1" >&2
    usage >&2
    exit 1
    ;;
esac

NDMSPC_ROOT="$(find_ndmspc_root)" || {
  echo "Cannot find the ndmspc tutorials. Set NDMSPC_ROOT to the installation prefix" >&2
  echo "(the directory containing 'tutorial' and 'macros')." >&2
  exit 1
}

if ! command -v root >/dev/null 2>&1; then
  echo "The 'root' executable is not in PATH. Load a ROOT environment first." >&2
  exit 1
fi

if [ ! -w "$PWD" ]; then
  echo "Current directory '$PWD' is not writable; run the tutorials in a writable directory." >&2
  exit 1
fi

echo "ndmspc tutorials from $NDMSPC_ROOT"
echo "Writing output to $PWD"

download_hsimple || true

failed=()
index=0
for step in "${STEPS[@]}"; do
  index=$((index + 1))
  macro="$NDMSPC_ROOT/$step"
  # A step may carry macro arguments, e.g. Macro.C("out.root"); the file is
  # everything before the first parenthesis.
  macro_file="${macro%%(*}"

  echo
  echo "[$index/${#STEPS[@]}] $step"
  if [ ! -f "$macro_file" ]; then
    echo "FAILED: macro not found: $macro_file" >&2
    failed+=("$step")
    continue
  fi

  if ! root -l -b -q "$macro"; then
    echo "FAILED: $step" >&2
    failed+=("$step")
  fi
done

echo
echo "Output files in $PWD:"
ls -1 -- *.root 2>/dev/null || true

if [ ${#failed[@]} -ne 0 ]; then
  echo
  echo "Failed steps (${#failed[@]}):" >&2
  printf '  %s\n' "${failed[@]}" >&2
  exit 1
fi

echo
echo "All tutorial steps finished successfully."
