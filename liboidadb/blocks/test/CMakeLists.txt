cmake_minimum_required(VERSION 3.21)

# all tests
set(stdtestlibs pthread rt)

set(stdtestfiles teststuff.c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
file(GLOB lib_src ../*.c)
include_directories(../../include)


# Add test targets
file(GLOB testtargets *_t.c)
foreach( testsourcefile ${testtargets} )
	get_filename_component( testname ${testsourcefile} NAME_WE )
	add_executable(${testname} ${testsourcefile} ${stdtestfiles}  ${src})
	target_link_libraries(${testname} ${stdtestlibs})
endforeach( testsourcefile ${testtargets} )