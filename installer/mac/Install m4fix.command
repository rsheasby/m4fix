#!/bin/bash
# Double-click in Finder to install, update or remove m4fix.
cd "$(dirname "$0")"
bash ./m4fix.sh
code=$?
echo
read -r -p "Press Return to close this window." _
exit $code
