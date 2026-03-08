timg Screenshot_2026_0220_173156.png
echo "digson toolset 0.7"
echo "Build give(chmod) command"
cd give
gcc source.c -o give -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
cd ..
echo "Build take(mv) command"
cd take
gcc take.c -o take -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
cd ..
echo "Build have command"
cd have
gcc source.c -o have -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
cd ..
echo "Build say(echo) command"
cd say
gcc source.c -o say -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
cd ..
echo "Build see(ls) command"
cd see
gcc source.c -o see -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
cd ..
echo "Build get(wget curl) command"
cd get
gcc source.c -o get -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9
