# -*- Mode: Python -*-

env = Environment(
    CPPFLAGS = ' -DASIO_STANDALONE',
    CPPPATH = ['.',
               'asio/asio/include',
               'lwip-contrib/ports/unix/include',
               'lwip/src/include',
               'lwip/src/include/ipv4',               
               ],
    CCFLAGS = '-g -march=native -Os',
    CXXFLAGS = ' -std=c++14 ',
    CXX = 'clang++',
    CFLAGS = ' -std=c11 ',
    CC  = 'clang',
    LINKFLAGS = '-g -Os -march=native -pthread ',
    LINK = 'clang++')


env.ParseConfig('pkg-config --cflags --libs libglog')

env.Program('sockslwip',
            ['main.cpp', 'tun.cpp',
             'lwip-contrib/ports/unix/sys_arch.c'] +
            Glob('lwip/src/core/*.c') +
            Glob('lwip/src/core/ipv4/*.c') +            
            Glob('lwip/src/api/*.c') +
            Glob('lwip/src/netif/*.c'))

# EOF
