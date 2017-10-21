cmake_minimum_required(VERSION 3.4.1)

add_library(i10n-${I10N_NATIVE_VER} SHARED interruption.c)

set_target_properties(i10n-${I10N_NATIVE_VER} PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/options.ld)

set(I10N_GEN_FLAGS -g0 -Os -pipe -ffunction-sections -fdata-sections -fvisibility=hidden -fomit-frame-pointer -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -fno-use-cxa-atexit -fno-ident)
set(I10N_BASE_CFLAGS ${I10N_GEN_FLAGS} -Wall)
set(I10N_BASE_LDFLAGS ${I10N_GEN_FLAGS} -Wl,--build-id=none -Wl,--gc-sections)

if(${ANDROID_ABI} MATCHES "^armeabi$")
  # for some weird reason go.ld produces binaries of much better size for old armeabi arch
  set(I10N_EXTRA_CFLAGS ${I10N_BASE_CFLAGS})
  set(I10N_EXTRA_LDFLAGS ${I10N_BASE_LDFLAGS})
else()
  set(I10N_EXTRA_CFLAGS ${I10N_BASE_CFLAGS} -fuse-ld=bfd)
  set(I10N_EXTRA_LDFLAGS ${I10N_BASE_LDFLAGS} -fuse-ld=bfd -Wl,-T${CMAKE_CURRENT_LIST_DIR}/options.ld)
endif()

target_compile_options(i10n-${I10N_NATIVE_VER} PRIVATE ${I10N_EXTRA_CFLAGS})
target_link_libraries(i10n-${I10N_NATIVE_VER} ${I10N_EXTRA_LDFLAGS})