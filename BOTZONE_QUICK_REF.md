# Quick Botzone Submission Reference

## Fixed Issues (2024-12-15)

### ✅ Compilation Error Fixed
**Problem**: `fatal error: json.h: No such file or directory`

**Solution**: Added conditional compilation in `amazons.cpp`:
```cpp
#ifdef _BOTZONE_ONLINE
#include <json/json.h>
#else
#include <jsoncpp/json/json.h>
#endif
```

Botzone's compiler automatically defines `_BOTZONE_ONLINE`, so the correct header path is used.

### ✅ Time Limit Fixed
**Problem**: Botzone requires moves within 1 second

**Solution**: Reduced `TIME_LIMIT` from 2800ms to 950ms (0.95 seconds) to ensure completion within 1 second.

## Botzone Submission Steps

1. **Go to**: https://www.botzone.org.cn
2. **Upload**: `amazons.cpp` (single file submission)
3. **Compiler Settings**:
   - Language: C++
   - Compile command: `g++-7 -D_BOTZONE_ONLINE -D_GLIBCXX_USE_CXX11_ABI=0 -O2 -Wall -mavx -mavx2 -std=c++1z -x c++ ./tmp/[filename].cpp17 -ljson -lpthread -o ./tmp/[output]`
   - Run command: `./amazons`

**Note**: The `-D_BOTZONE_ONLINE` flag is automatically added by Botzone, which activates the correct JSON include path.

## Expected Performance

- Compilation: ✅ Success
- Time compliance: ✅ < 1 second per move
- Move generation: ✅ Valid JSON format
- Strategy: Alpha-beta search (depth 4) with territory-based evaluation

## Verification

Local testing confirms:
- Compiles successfully
- Generates valid moves: `[start_x, start_y, end_x, end_y, arrow_x, arrow_y]`
- Completes within time limit
- All test scenarios pass

## Support

If issues persist:
1. Verify the compile command includes `-ljson` (not `-ljsoncpp`)
2. Ensure `-D_BOTZONE_ONLINE` flag is set
3. Check that TIME_LIMIT in code is 950ms (not 2800ms)

The bot is ready for competition! 🎯
