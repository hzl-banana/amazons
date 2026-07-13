#!/bin/bash

# Test script for Amazons AI bot

echo "=== Test 1: First move (Black) ==="
echo '{"requests":[],"responses":[]}' | ./amazons
echo ""

echo "=== Test 2: Second move (White response) ==="
echo '{"requests":[[0,2,0,4,0,3]],"responses":[[0,2,0,4,0,3]]}' | ./amazons
echo ""

echo "=== Test 3: Third move (Black) ==="
echo '{"requests":[[0,2,0,4,0,3],[0,5,0,3,0,4]],"responses":[[0,2,0,4,0,3],[0,5,0,3,0,4]]}' | ./amazons
echo ""

echo "=== All tests completed ==="
