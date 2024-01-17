cmake_minimum_required(VERSION 3.21)

# all tests
set(stdtestlibs pthread rt)
set(stdtestfiles ../errors.c ../odbspec.c ../telemetry.c teststuff.c)

# By-namespace
file(GLOB lib_src ../*.c)

# Add test targets
file(GLOB testtargets *_t.c)
foreach( testsourcefile ${testtargets} )
	get_filename_component( testname ${testsourcefile} NAME_WE )
	add_executable(${testname} ${testsourcefile} ${stdtestfiles}  ${src})
	target_link_libraries(${testname} ${stdtestlibs})
endforeach( testsourcefile ${testtargets} )