# A scripted run (--run) must not write the user's settings.
#
#   cmake -DAPP=<nebulascope binary> -DSRC=<repo root> -DWORK=<scratch dir>
#         -P tests/settings_isolation.cmake
#
# The app runs tests/settings_isolation.nsc with HOME (and XDG_CONFIG_HOME)
# pointing at an empty scratch directory, standing in for the user's home:
# Qt resolves the IniFormat/UserScope settings directory from them on Unix.
# The script opens a file and changes a preference; afterwards the stand-in
# home must hold no settings file at all (shortcuts.ini, recent.ini and
# preferences.ini would all appear there without the isolation in main.cpp).
foreach(v APP SRC WORK)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "settings_isolation.cmake: -D${v}=... is required")
    endif()
endforeach()

set(home "${WORK}/home")
file(REMOVE_RECURSE "${home}")
file(MAKE_DIRECTORY "${home}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            "HOME=${home}" "XDG_CONFIG_HOME=${home}/.config"
            QT_QPA_PLATFORM=offscreen
            "${APP}" --run "${SRC}/tests/settings_isolation.nsc"
    WORKING_DIRECTORY "${SRC}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the script itself failed (exit code ${rc})")
endif()

file(GLOB_RECURSE written "${home}/*.ini")
if(written)
    message(FATAL_ERROR "a scripted run wrote settings files:\n  ${written}")
endif()
message(STATUS "no settings file written by the scripted run")
