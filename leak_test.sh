#!/bin/sh

OS="$(uname -s)"

echo "=== Escape the Flock: Leak Test ==="

echo ""
echo "[1/2] Building..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo "  Build OK"

case "$OS" in
Darwin)
	echo ""
	echo "[2/2] Running leak test..."
	./escape map.txt > /dev/null 2>&1 &
	PID=$!
	sleep 3
	REPORT=$(leaks "$PID" 2>&1)
	kill $PID 2>/dev/null
	wait $PID 2>/dev/null

	if echo "$REPORT" | grep -q "0 leaks for 0 total leaked bytes"; then
		echo "  PASS: 0 leaks"
	else
		echo "  FAIL: leaks detected"
		echo "$REPORT" | tail -5
		exit 1
	fi
	;;

Linux)
	echo ""
	echo "[2/2] Running leak test..."
	if ! command -v valgrind > /dev/null 2>&1; then
		echo "  valgrind not found. Install: sudo apt install valgrind"
		exit 1
	fi
	echo "d" | timeout 3 valgrind --leak-check=full --error-exitcode=1 \
		./escape map.txt > /dev/null 2>&1
	CODE=$?
	if [ $CODE -eq 0 ]; then
		echo "  PASS: 0 leaks"
	else
		echo "  FAIL: leaks detected (exit code: $CODE)"
		exit 1
	fi
	;;

*)
	echo "  No leak tool available for $OS"
	exit 1
	;;
esac

echo ""
echo "=== Done ==="
