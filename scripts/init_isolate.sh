#!/bin/bash
# Initialize isolate sandbox boxes for the judge worker.
# Must be run with sudo.
# Usage: sudo ./scripts/init_isolate.sh [NUM_BOXES] [START_ID]

NUM_BOXES=${1:-5}
START_ID=${2:-1}
ISOLATE_BIN="/usr/local/bin/isolate"

echo "Initializing $NUM_BOXES isolate boxes starting at ID $START_ID..."

for ((i = 0; i < NUM_BOXES; i++)); do
    box_id=$((START_ID + i))
    echo -n "  Box $box_id... "
    if "$ISOLATE_BIN" --init --box-id="$box_id" 2>/dev/null; then
        echo "OK"
    else
        echo "FAILED"
    fi
done

echo "Done."
