#!/bin/bash
# Function: Converts 1-3,5 to [1,2,3,5]
parse_range_to_array() {
  local input="$1"
  [[ -z "$input" ]] && return
  local result=()
  IFS=',' read -ra PARTS <<< "$input"
  for part in "${PARTS[@]}"; do
    if [[ "$part" =~ ([0-9]+)-([0-9]+) ]]; then
      local start=${BASH_REMATCH[1]}
      local end=${BASH_REMATCH[2]}
      for ((i=start; i<=end; i++)); do result+=("$i"); done
    else
      result+=("$part")
    fi
  done
  local IFS=","
  echo "[${result[*]}]"
}

# Function: Parses the command into the 3 required variables
parsesbat() {
  # If the first word is 'sbatch', skip it
  [[ "$1" == "sbatch" ]] && shift

  local a_raw=""
  local sbatch_opts=""
  local user_cmd=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      -a|--array)
        a_raw="$2"
        # Add to sbatch_opts string even though we are extracting it
        sbatch_opts+="$1 $2 "
        shift 2
        ;;
      -*)
        # Capture the option
        sbatch_opts+="$1 "
        # Capture the value if the next argument doesn't start with '-'
        if [[ -n "$2" && ! "$2" =~ ^- ]]; then
            sbatch_opts+="$2 "
            shift
        fi
        shift
        ;;
      *)
        # The first non-dash argument marks the start of the script + user args
        user_cmd="$*"
        break
        ;;
    esac
  done

  # Finalize the variables
  FINAL_ARRAY=$(parse_range_to_array "$a_raw")
  SBATCH_OPTIONS="${sbatch_opts% }"
  USER_SCRIPT_WITH_ARGS="$user_cmd"
}

# Results
echo "ARRAY:   $FINAL_ARRAY"
echo "OPTIONS: $SBATCH_OPTIONS"
echo "REST:    $USER_SCRIPT_WITH_ARGS"
# --- Execution Example ---
CMD="sbatch $*"

# Call the function
parsesbat $CMD


# Display the separated variables
echo "1. Array String:   $FINAL_ARRAY"
echo "2. Sbatch Options: $SBATCH_OPTIONS"
echo "3. User Script:    $USER_SCRIPT_WITH_ARGS"



JOBID=$(sbatch $SBATCH_OPTIONS ndmspc-sbatch-run.sh $USER_SCRIPT_WITH_ARGS | cut -d ' ' -f 4)

TASKS='"tasks":[1]'
[ -n "$FINAL_ARRAY" ] && TASKS='"tasks":'$FINAL_ARRAY''

echo "JOB : $JOBID"
[ -z "$JOBID" ] && { echo "error in sbatch"; exit 1; }
curl -X POST -H "Content-Type: application/json" -d '{"name":"job_'$JOBID'",'$TASKS'}' http://localhost:8080/api/jobs 
