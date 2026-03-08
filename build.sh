timg Screenshot_2026_0220_173156.png
echo "digson toolset 0.6"
echo "Build give(chmod) command"
cd give
gcc source.c -o give
cd ..
echo "Build take(mv) command"
cd take
gcc take.c -o take
cd ..
echo "Build have command"
cd have
gcc source.c -o have
cd ..
echo "Build say(echo) command"
cd say
gcc source.c -o say
cd ..
echo "Build see(ls) command"
cd see
gcc source.c -o see
cd ..
echo "Build get(wget curl) command"
cd get
gcc source.c -o get
