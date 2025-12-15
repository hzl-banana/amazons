#!/bin/bash
# Comprehensive bot verification

echo "=================================="
echo "Amazons AI Bot - Final Verification"
echo "=================================="
echo ""

echo "1. Build verification..."
make clean > /dev/null 2>&1
if make > /dev/null 2>&1; then
    echo "   ✅ Builds successfully"
else
    echo "   ❌ Build failed"
    exit 1
fi

echo ""
echo "2. First move test (Black)..."
OUTPUT=$(echo '{"requests":[],"responses":[]}' | ./amazons)
if [[ $OUTPUT =~ ^\[[0-9],[0-9],[0-9],[0-9],[0-9],[0-9]\]$ ]]; then
    echo "   ✅ Valid output format: $OUTPUT"
else
    echo "   ❌ Invalid output: $OUTPUT"
    exit 1
fi

echo ""
echo "3. Response move test (White)..."
OUTPUT=$(echo '{"requests":[[0,2,0,4,0,3]],"responses":[[0,2,0,4,0,3]]}' | ./amazons)
if [[ $OUTPUT =~ ^\[[0-9],[0-9],[0-9],[0-9],[0-9],[0-9]\]$ ]]; then
    echo "   ✅ Valid output format: $OUTPUT"
else
    echo "   ❌ Invalid output: $OUTPUT"
    exit 1
fi

echo ""
echo "4. Third move test (Black)..."
OUTPUT=$(echo '{"requests":[[0,2,0,4,0,3],[0,5,0,3,0,4]],"responses":[[0,2,0,4,0,3],[0,5,0,3,0,4]]}' | ./amazons)
if [[ $OUTPUT =~ ^\[[0-9],[0-9],[0-9],[0-9],[0-9],[0-9]\]$ ]]; then
    echo "   ✅ Valid output format: $OUTPUT"
else
    echo "   ❌ Invalid output: $OUTPUT"
    exit 1
fi

echo ""
echo "5. File check..."
for file in amazons.cpp Makefile test.sh visualize.py README.md BOTZONE_GUIDE.md IMPLEMENTATION_SUMMARY.md; do
    if [ -f "$file" ]; then
        echo "   ✅ $file exists"
    else
        echo "   ❌ $file missing"
        exit 1
    fi
done

echo ""
echo "=================================="
echo "✅ ALL TESTS PASSED!"
echo "=================================="
echo ""
echo "🎯 Bot is ready for Botzone submission!"
echo ""
echo "Next steps:"
echo "1. Go to https://www.botzone.org.cn"
echo "2. Upload amazons.cpp"
echo "3. Configure: g++ -std=c++11 -O2 -o amazons amazons.cpp -ljsoncpp"
echo "4. Start competing!"
echo ""
