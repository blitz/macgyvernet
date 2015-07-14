# -*- Mode: Python -*-

env = Environment(
    CPPPATH = ['.',
               'include',
               'asio/asio/include',
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

conf = Configure(env)

if not conf.CheckCXXHeader('gflags/gflags.h'):
    print("Please install gflags-devel.")
    Exit(1)

if not conf.CheckCXXHeader('glog/logging.h'):
    print("Please install glog-devel.")

env = conf.Finish()

env.Program('macgyvernet',
            Glob('*.cpp') +
            Glob('lwip/src/core/*.c') +
            Glob('lwip/src/core/ipv4/*.c') +
            Glob('lwip/src/api/*.c') +
            Glob('lwip/src/netif/*.c'))

# EOF
