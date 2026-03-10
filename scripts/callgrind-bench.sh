#!/usr/bin/env bash
# Run callgrind on selected tests and output results as JSON
# compatible with github-action-benchmark's "googlecpp" (Google Benchmark) format.
#
# Usage: scripts/callgrind-bench.sh <test-binary> [output.json]

set -euo pipefail

BINARY="${1:?Usage: $0 <test-binary> [output.json]}"
OUTPUT="${2:-benchmark-results.json}"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "Error: valgrind is not installed" >&2
    exit 1
fi

# Tests to benchmark -- exercise the core read/write/extract paths
# without being too heavy for CI under callgrind.
TESTS=(
    "ZipRoundTripTest.MultipleEntries"
    "ZipRoundTripTest.BinaryContent"
    "ZipRoundTripTest.WriteToOstream"
    "ZipInputStreamTest.BinaryRoundTrip"
    "ZipFileDataTest.GetInputStreamByName"
    "ZipFileDataTest.CloneCreatesIndependentCopy"
    "GZIPTest.LargeData"
)

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

BENCHMARKS=""
FIRST=true

for TEST in "${TESTS[@]}"; do
    CGOUT="$TMPDIR/callgrind.out.${TEST//[^a-zA-Z0-9]/_}"

    echo "Running callgrind: $TEST ..." >&2
    valgrind --tool=callgrind \
        --callgrind-out-file="$CGOUT" \
        --collect-jumps=yes \
        --cache-sim=no \
        "$BINARY" --gtest_filter="$TEST" \
        > "$TMPDIR/stdout.txt" 2> "$TMPDIR/stderr.txt" || true

    # Extract total instruction count from callgrind output
    INSTRUCTIONS=$(grep -oP '(?<=summary: )\d+' "$CGOUT" 2>/dev/null || echo "0")

    if [ "$INSTRUCTIONS" = "0" ]; then
        echo "Warning: failed to extract instructions for $TEST" >&2
        continue
    fi

    if [ "$FIRST" = true ]; then
        FIRST=false
    else
        BENCHMARKS+=","
    fi

    # Google Benchmark JSON format: each benchmark has name, iterations, and
    # a metric. We use "instructions" as a custom counter via real_time field
    # (the benchmark-action uses this for comparison).
    BENCHMARKS+="$(printf '\n    {
      "name": "%s",
      "run_name": "%s",
      "run_type": "iteration",
      "iterations": 1,
      "real_time": %s,
      "cpu_time": %s,
      "time_unit": "ns"
    }' "$TEST" "$TEST" "$INSTRUCTIONS" "$INSTRUCTIONS")"
    echo "  $TEST: $INSTRUCTIONS instructions" >&2
done

# Write Google Benchmark JSON envelope
cat > "$OUTPUT" <<ENDJSON
{
  "context": {
    "date": "$(date -Iseconds)",
    "num_cpus": $(nproc),
    "library_build_type": "release"
  },
  "benchmarks": [${BENCHMARKS}
  ]
}
ENDJSON

echo "Results written to $OUTPUT" >&2
