DIR=/home/igor/local/llvm19-assert/
$DIR/bin/clang++ main.cpp \
    -I /usr/include/c++/11 \
    -I /usr/include/x86_64-linux-gnu/c++/11 \
    -L/usr/lib/gcc/x86_64-linux-gnu/11 \
    -lstdc++ \
    `$DIR/bin/llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` \
    -O3 -o main