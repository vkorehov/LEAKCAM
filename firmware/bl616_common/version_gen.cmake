# Writes leakcam_version.h on every build (leakcam_version.cmake): the product version both BL616 firmwares share
# (firmware/VERSION: the full and the setup firmware always ship as one pair, user 2026-11-02), the git commit and,
# for a tree with uncommitted changes, the build time (UTC). The crash record keeps it (crash_rec.c): it names the one
# ELF its addresses belong to. Rewritten only when it changed, so a clean tree recompiles nothing.
#   cmake -DROOT=<repo> -DIMAGE=<name> -DOUT=<header> -P version_gen.cmake
file(READ ${ROOT}/firmware/VERSION v)
string(STRIP "${v}" v)
execute_process(COMMAND git -C ${ROOT} rev-parse --short=7 HEAD OUTPUT_VARIABLE h OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "version_gen: no git commit in ${ROOT}")
endif()
execute_process(COMMAND git -C ${ROOT} status --porcelain --untracked-files=no OUTPUT_VARIABLE d)
set(ver "${v}+g${h}")
if(NOT d STREQUAL "")
    string(TIMESTAMP t "%Y%m%d%H%M" UTC)
    set(ver "${ver}-dirty-${t}")
endif()
set(text "#define LEAKCAM_IMAGE \"${IMAGE}\"\n#define LEAKCAM_VERSION \"${ver}\"\n")
set(old "")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT old STREQUAL text)
    file(WRITE ${OUT} "${text}")
endif()
