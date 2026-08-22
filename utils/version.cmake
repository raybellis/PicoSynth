# Writes version.h with the current git revision.
#
# Run at build time rather than configure time, so that the number on
# the display tracks commits without needing cmake to be re-run - the
# whole point of it is to answer "what is actually on the board?", and
# a stale answer is worse than none.
#
# copy_if_different keeps that from relinking on every build: the file
# is only touched when the revision actually changes.

set(VERSION "nogit")

find_package(Git QUIET)

if (GIT_FOUND)
	execute_process(
		COMMAND			${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${SRC}
		OUTPUT_VARIABLE	REV
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	if (REV)
		# a trailing + means the build had uncommitted changes, so the
		# revision alone does not identify what is running
		execute_process(
			COMMAND			${GIT_EXECUTABLE} status --porcelain --untracked-files=no
			WORKING_DIRECTORY ${SRC}
			OUTPUT_VARIABLE	DIRTY
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)

		if (DIRTY STREQUAL "")
			set(VERSION "${REV}")
		else()
			set(VERSION "${REV}+")
		endif()
	endif()
endif()

file(WRITE ${OUT}.tmp "#pragma once\n\n#define GIT_VERSION \"${VERSION}\"\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}.tmp ${OUT})
file(REMOVE ${OUT}.tmp)
