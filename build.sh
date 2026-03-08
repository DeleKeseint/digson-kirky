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
