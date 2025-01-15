#!/bin/bash
set -e
set -o pipefail

COPR_DEBUG=${1-0}

if [[ $COPR_DEBUG -eq 1 ]]; then
  echo "COPR_LOGIN=$COPR_LOGIN"
  echo "COPR_TOKEN=$COPR_TOKEN"
  echo "COPR_USER=$COPR_USER"
fi

[ -n "$COPR_LOGIN" ] || {
  echo "COPR_LOGIN is epmty !!!"
  exit 1
}
[ -n "$COPR_TOKEN" ] || {
  echo "COPR_TOKEN is epmty !!!"
  exit 1
}
[ -n "$COPR_USER" ] || {
  echo "COPR_USER is epmty !!!"
  exit 1
}

rm -rf ~/.config | true
mkdir ~/.config
cp ci/copr ~/.config/copr

sed -i 's/^login.*/login = '$COPR_LOGIN'/' ~/.config/copr
sed -i 's/^token.*/token = '$COPR_TOKEN'/' ~/.config/copr
sed -i 's/^username.*/username = '$COPR_USER'/' ~/.config/copr

[[ $COPR_DEBUG -eq 0 ]] || cat ~/.config/copr
