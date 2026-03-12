timg Screenshot_2026_0220_173156.png
echo "digson toolset 1.3/setup 1"
echo "warning: If one of the compiles fails, the version is called Kirky version"
echo "warning: All desktop environments in this toolset require a makefile to build applications(build command:cd status-nast && make)"
echo "Build give(chmod) command"
cd give
gcc source.c -o give -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build take(mv) command"
cd take
gcc take.c -o take -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build have command"
cd have
gcc source.c -o have -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build say(echo) command"
cd say
gcc source.c -o say -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build see(ls) command"
cd see
gcc source.c -o see -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build get(wget curl) command"
cd get
gcc source.c -o get -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "build think(mini echo) command"
cd think
gcc source.c -o think -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "build work command"
cd work
gcc source.c -o work -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
echo "build eat(rm) command"
cd ..
cd eat
gcc source.c -o eat -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build setting command"
cd setting
gcc source2.c -o setting -lm -lcurl -lssh -D LIBSSH_VERSION_MAJOR=0 -D LIBSSH_VERSION_MINOR=9 -lncurses -lbluetooth -Wall -O2
cd ..
echo "Build Notification command"
cd Notification
gcc source.c -o notification -Wall -O2

# 安装
cp notification $PREFIX/bin/
chmod +x $PREFIX/bin/notification
