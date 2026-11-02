# Included by every BL616 firmware's CMakeLists.txt after setting LEAKCAM_IMAGE (its name: full, setup, setup_test,
# powertest): leakcam_version.h with LEAKCAM_IMAGE and LEAKCAM_VERSION, regenerated before every build
# (version_gen.cmake); a define set at configure time went stale, the incremental build never reconfigures (host.mk).
set(LEAKCAM_GEN ${CMAKE_CURRENT_BINARY_DIR}/leakcam_gen)
add_custom_target(leakcam_version
                  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_LIST_DIR}/../.. -DIMAGE=${LEAKCAM_IMAGE}
                          -DOUT=${LEAKCAM_GEN}/leakcam_version.h -P ${CMAKE_CURRENT_LIST_DIR}/version_gen.cmake
                  BYPRODUCTS ${LEAKCAM_GEN}/leakcam_version.h)
add_dependencies(app leakcam_version)
target_include_directories(app PRIVATE ${LEAKCAM_GEN})
