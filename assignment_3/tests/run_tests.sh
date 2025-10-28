#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROG="$SCRIPT_DIR/../xsh"
TEST_DIR=$(mktemp -d)
cd "$TEST_DIR"

echo "Running xsh smoke tests in $TEST_DIR"

# Test 1: echo output
cat > input1 <<'EOF'
echo hello
exit
EOF
$PROG < input1 > out1 2>&1 || true
if grep -q "hello" out1; then
    echo "[PASS] echo produced output"
else
    echo "[FAIL] echo did not produce output"
    cat out1
    exit 1
fi

# Test 2: redirection >
cat > input2 <<'EOF'
echo redirtest > out.txt
exit
EOF
$PROG < input2 > out2 2>&1 || true
if [ -f out.txt ] && grep -q "redirtest" out.txt; then
    echo "[PASS] redirection '>' created out.txt"
else
    echo "[FAIL] redirection '>' failed"
    ls -la
    [ -f out.txt ] && cat out.txt
    exit 1
fi

# (append >> not supported anymore)

# Test 4: env expansion
cat > input4 <<'EOF'
echo $PATH
exit
EOF
$PROG < input4 > out4 2>&1 || true
if grep -q "/" out4; then
    echo "[PASS] env expansion printed PATH-like content"
else
    echo "[WARN] env expansion may have failed or PATH empty"
    cat out4
fi

# Test 5: env expansion for $HOME
cat > input5 <<'EOF'
echo $HOME
exit
EOF
$PROG < input5 > out5 2>&1 || true
if grep -q "${HOME}" out5; then
    echo "[PASS] env expansion printed HOME"
else
    echo "[FAIL] env expansion for HOME failed"
    cat out5
    exit 1
fi

# Test 6: double redirection (ls > input.txt ; sort < input.txt > output.txt)
mkdir -p testdir_for_ls
cd testdir_for_ls
printf '%s
' c b a > a.txt
cat a.txt > input.txt
cd - >/dev/null
cat > input6 <<'EOF'
ls testdir_for_ls > input.txt
sort < input.txt > output.txt
exit
EOF
$PROG < input6 > out6 2>&1 || true
if [ -f output.txt ] && grep -q "a.txt" output.txt; then
    # basic check: sorted output should include the filename
    echo "[PASS] double redirection (ls -> input.txt ; sort < input.txt -> output.txt) succeeded"
else
    echo "[FAIL] double redirection test failed"
    ls -la
    [ -f output.txt ] && cat output.txt
    echo "--- stdout/stderr ---"
    cat out6
    exit 1
fi

# Cleanup
cd - >/dev/null
rm -rf "$TEST_DIR"

echo "All tests completed"
