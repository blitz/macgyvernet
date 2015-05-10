# -*- Mode: Python -*-

env = Environment(
    CPPFLAGS = ' -DASIO_STANDALONE',
    CPPPATH = ['asio/asio/include'],
    CCFLAGS = '-g -march=native -Os',
    CXXFLAGS = ' -std=c++14 ',
    CXX = 'clang++',
    CFLAGS = ' -std=c11 ',
    CC  = 'clang',
    LINKFLAGS = '-g -Os -march=native -pthread ',
    LINK = 'clang++')

env.Program('sockslwip', ['main.cpp'])

# EOF
