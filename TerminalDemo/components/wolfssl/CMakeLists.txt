set(WOLFSSL_ROOT "/Users/m5-vincent/Documents/SSHKEYBOARD/esp-wolfssl")

set(WOLFSSL_SRCDIRS
    "${WOLFSSL_ROOT}/wolfssl/src/"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/port/Espressif/"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/port/atmel/"
)

set(WOLFSSL_INCLUDE_DIRS
    "${WOLFSSL_ROOT}/port"
    "${WOLFSSL_ROOT}/wolfssl"
)

set(WOLFSSL_EXCLUDE_SRCS
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/aes_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/evp.c"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/misc.c"
    "${WOLFSSL_ROOT}/wolfssl/src/bio.c"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sp_x86_64_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sha512_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sha256_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/chacha_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/aes_gcm_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/aes_gcm_x86_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/aes_xts_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/poly1305_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/fe_x25519_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sha3_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sm3_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/sp_sm2_x86_64_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/wc_mldsa_asm.S"
    "${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/wc_mlkem_asm.S"
)

idf_component_register(
    SRC_DIRS ${WOLFSSL_SRCDIRS}
    INCLUDE_DIRS ${WOLFSSL_INCLUDE_DIRS}
    REQUIRES lwip
    EXCLUDE_SRCS ${WOLFSSL_EXCLUDE_SRCS}
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-cpp -Wno-maybe-uninitialized)
set_source_files_properties("${WOLFSSL_ROOT}/wolfssl/src/ssl.c" PROPERTIES COMPILE_FLAGS "-Wno-format-truncation -Wno-char-subscripts")
set_source_files_properties("${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/random.c" PROPERTIES COMPILE_FLAGS "-Wno-implicit-function-declaration")
set_source_files_properties("${WOLFSSL_ROOT}/wolfssl/wolfcrypt/src/port/Espressif/esp32_aes.c" PROPERTIES COMPILE_FLAGS "-Wno-incompatible-pointer-types")
target_compile_definitions(${COMPONENT_LIB} PUBLIC WOLFSSL_USER_SETTINGS)
