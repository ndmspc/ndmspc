#!/bin/bash
# slightly malformed input data
input_start=${1-"2024-01-01"}
input_end=${2-"2024-08-31"}
diff=${3-"day"}
group_id=${4-6534672}
sleep=1
OUT_BASE_DIR=${OUT_BASE_DIR-"./data/gitlab"}
DRY_RUN=${DRY_RUN-1}
out_dir="$OUT_BASE_DIR/$group_id/$diff"

# Fix daylight saving problem when adding 1 day
export TZ=UTC


[ -n "$GITLAB_TOKEN" ] || { echo "Error: 'GITLAB_TOKEN' is missing !!!"; exit 1; }

[[ $CLEAN -eq 1 ]] && rm -rf $out_dir

[ -d $out_dir ] || mkdir -p $out_dir

# After this, startdate and enddate will be valid ISO 8601 dates,
# or the script will have aborted when it encountered unparseable data
# such as input_end=abcd
startdate=$(date -Is -d "$input_start") || exit -1
enddate=$(date -Is -d "$input_end")     || exit -1

echo "group=$group_id : $startdate - $enddate"

d="$startdate"
while [ "$d" != "$enddate" ]; do
  dAfter=$(date -d "$d" "+%Y-%m-%dT%H:00:00Z")
  dBefore=$(date -d "$d $diff" "+%Y-%m-%dT%H:00:00Z")
  f="$out_dir/$(date -d "$d" "+%Y-%m-%d-%H-00-00")"

  [[ "$diff" = "day" ]] && d=$(date -Is -d "$d 24 hours") || d=$(date -Is -d "$d 1 $diff")
  echo "$dAfter - $dBefore to '$f'"
  [[ $DRY_RUN -eq 1 ]] && sleep $sleep && continue
  [ -d $f ] && continue
  mkdir -p $f
  curl -s "https://gitlab.com/api/v4/groups/$group_id/issues?page=1&per_page=100&updated_after=$dAfter&updated_before=$dBefore&private_token=$GITLAB_TOKEN" > $f/issues.json
  sleep $sleep
  curl -s "https://gitlab.com/api/v4/groups/$group_id/merge_requests?page=1&per_page=100&updated_after=$dAfter&updated_before=$dBefore&private_token=$GITLAB_TOKEN" > ${f}/mr.json
  sleep $sleep
done

echo "Done."

