# Botzone Submission Guide

## How to Submit to Botzone

### Step 1: Prepare Your Code
1. Make sure `amazons.cpp` compiles without errors
2. Test locally with sample inputs

### Step 2: Create Submission File
For Botzone, you need to submit a single C++ file. The current `amazons.cpp` is ready for submission.

### Step 3: Submit to Botzone Platform

1. Go to https://www.botzone.org.cn
2. Log in to your account
3. Navigate to "Amazons" game page
4. Click "Submit Bot" or "Upload Code"
5. Upload `amazons.cpp`
6. Configure build settings:
   - Language: C++
   - Compiler: g++ 
   - Compile command: `g++ -std=c++11 -O2 -o amazons amazons.cpp -ljsoncpp`
   - Run command: `./amazons`

### Step 4: Test Your Bot
1. After submission, Botzone will compile your bot
2. Run test matches against sample bots
3. Check the match logs for any errors

### Step 5: Compete
1. Once verified, your bot will be added to the ranking system
2. It will play matches against other bots automatically
3. Monitor your ranking at: https://botzone.org/game/ranklist/59463fb292a2ea07a5c6b5a8

## Local Testing

### Build and Run
```bash
make clean
make
./test.sh
```

### Manual Testing
```bash
# Test first move (Black)
echo '{"requests":[],"responses":[]}' | ./amazons

# Test response to opponent move
echo '{"requests":[[0,2,0,4,0,3]],"responses":[[0,2,0,4,0,3]]}' | ./amazons
```

### Visualize Games
```bash
python3 visualize.py
```

## Troubleshooting

### Compilation Errors
- Make sure jsoncpp is installed: `sudo apt-get install libjsoncpp-dev`
- Check C++11 support: `g++ --version`

### Timeout Issues
- The bot is configured to use 2.8 seconds per move
- Botzone typically allows 3 seconds
- If timing out, reduce MAX_DEPTH in the code

### Invalid Move Errors
- Check that the bot generates valid moves
- Verify board state is correctly maintained
- Test with various game positions

## Performance Optimization Tips

1. **Increase Search Depth**: Modify MAX_DEPTH constant (currently 4)
2. **Tune Evaluation Weights**: Adjust territory/mobility/centrality weights
3. **Improve Move Ordering**: Better move ordering = more alpha-beta cutoffs
4. **Add Opening Book**: Pre-compute first few moves
5. **Parallel Search**: Use multiple threads (if Botzone allows)

## Bot Strategy

The current implementation uses:
- **Search**: Alpha-beta negamax with iterative deepening
- **Evaluation**: Territory control (10x) + Mobility (2x) + Centrality (1x)
- **Move Ordering**: Prioritizes center control and opponent restriction
- **Time Management**: Iterative deepening with early termination

## Expected Performance

- **Against Random Bot**: Should win 99%+ games
- **Against Simple Bots**: Should win 80%+ games
- **Against Advanced Bots**: Performance depends on search depth and evaluation

## Further Improvements

Consider implementing:
1. **Monte Carlo Tree Search (MCTS)**: Better for late game
2. **Endgame Database**: Pre-computed wins/losses
3. **Opening Book**: Memorize strong openings
4. **Neural Network Evaluation**: Train on game data
5. **Parallel Alpha-Beta**: Multi-threaded search

## Support

For issues with:
- **Code**: Check GitHub repository
- **Botzone Platform**: Visit https://www.botzone.org.cn/help
- **Game Rules**: https://wiki.botzone.org.cn/index.php?title=Amazons
