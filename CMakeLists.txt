cmake_minimum_required(VERSION 3.15)
project(prochess C)

set(CMAKE_C_STANDARD 90)

add_executable(prochess prochess.c lib/types.h lib/board.c lib/board.h lib/player.c lib/player.h lib/pawn.c lib/pawn.h lib/communicator.c lib/communicator.h)