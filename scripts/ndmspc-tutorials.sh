#!/bin/bash
# Run the ndmspc tutorials into the current directory.
#
# Start in an empty, writable directory: every output file is created here.
#
#   mkdir ndmspc-tutorials && cd ndmspc-tutorials
#   ndmspc-tutorials
#
# The tutorial macros are read from the ndmspc installation (next to this script,
# or from NDMSPC_ROOT / NDMSPC_DIR when set). hsimple.root is generated here from
# ROOT's hsimple.C, taken from the ROOT source on GitHub, so no ROOT tutorial
# package has to be installed.

set -u
set -o pipefail

HSIMPLE_MACRO_URL="https://raw.githubusercontent.com/root-project/root/master/tutorials/hsimple.C"

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
  "tutorial/08_systematics/NSystematics01Gaus.C"
  "tutorial/08_systematics/NSystematics02Gaus.C"
  "tutorial/06_binnings/NBinnings01GausStress.C"
  "macros/root/cernstaff/tree500_cernbuild.C"
)

usage()
{
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run the ndmspc tutorials in the current directory. All files the tutorials
produce are written here, so start in an empty, writable directory.

hsimple.root is generated from ROOT's hsimple.C, fetched from the
ROOT source on GitHub:
$HSIMPLE_MACRO_URL

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

generate_hsimple()
{
  if [ -s hsimple.root ]; then
    echo "hsimple.root already present, keeping it"
    return 0
  fi

  # The macro has to keep ROOT's own name: ROOT runs a file by calling the function it is named after
  # (`hsimple.C` -> `hsimple()`), so a made-up name like `hsimple.9q2N2O.C` derives
  # `hsimple_9q2N2O()`, finds no such function, and writes nothing. Hence a temporary *directory*
  # with the file's real name inside it, rather than a mangled file name.
  local dir tutorial
  dir="$(mktemp -d "${TMPDIR:-/tmp}/ndmspc-hsimple.XXXXXX")" || {
    echo "Cannot create a temporary directory for hsimple.C" >&2
    return 1
  }
  tutorial="$dir/hsimple.C"

  echo "Fetching hsimple.C from $HSIMPLE_MACRO_URL"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 -o "$tutorial" "$HSIMPLE_MACRO_URL" || {
      rm -rf "$dir"
      echo "Could not fetch hsimple.C; continuing without hsimple.root" >&2
      return 1
    }
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$tutorial" "$HSIMPLE_MACRO_URL" || {
      rm -rf "$dir"
      echo "Could not fetch hsimple.C; continuing without hsimple.root" >&2
      return 1
    }
  else
    rm -rf "$dir"
    echo "Neither curl nor wget found, cannot fetch hsimple.C" >&2
    return 1
  fi

  echo "Generating hsimple.root from $tutorial"
  rm -f hsimple.root
  # ROOT's macro can return a non-zero exit status after writing the file; the file is what matters.
  # Its output is kept, not discarded: when it fails, that output is the only thing that says why.
  local output
  output="$(cd "$PWD" && root -l -b -q "$tutorial" 2>&1)" || true
  rm -rf "$dir"
  if [ -s hsimple.root ]; then
    return 0
  fi

  rm -f hsimple.root
  echo "Could not generate hsimple.root, continuing without it" >&2
  if [ -n "$output" ]; then
    echo "ROOT said:" >&2
    printf '%s\n' "$output" | sed 's/^/  /' >&2
  fi
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

generate_hsimple || true

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
