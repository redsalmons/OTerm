# Re-sign the OceanTerm binary with a stable code-signing identity so macOS
# Keychain ACLs (bound to identifier + certificate leaf) survive rebuilds.
# Without this, ad-hoc linker signatures change every build and the Keychain
# prompts for the credential vault on every launch.
#
# Identity selection: $ENV{OTERM_SIGN_IDENTITY}, falling back to the
# "OceanTalk Dev" development certificate. Skips silently when neither exists.

if(NOT OTERM_BIN)
    message(FATAL_ERROR "SignMacOS.cmake: OTERM_BIN not set")
endif()

if(NOT DEFINED ENV{OTERM_SIGN_IDENTITY} OR "$ENV{OTERM_SIGN_IDENTITY}" STREQUAL "")
    set(OTERM_SIGN_IDENTITY "")
else()
    set(OTERM_SIGN_IDENTITY "$ENV{OTERM_SIGN_IDENTITY}")
endif()

if(NOT OTERM_SIGN_IDENTITY)
    execute_process(
        COMMAND security find-certificate -c "OceanTalk Dev" -Z
        OUTPUT_VARIABLE _cert_out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _cert_out MATCHES "SHA-1 hash: ([0-9A-Fa-f]+)")
        return()
    endif()
    set(OTERM_SIGN_IDENTITY "OceanTalk Dev")
    set(_cert_hash "${CMAKE_MATCH_1}")
else()
    execute_process(
        COMMAND security find-certificate -c "${OTERM_SIGN_IDENTITY}" -Z
        OUTPUT_VARIABLE _cert_out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _cert_out MATCHES "SHA-1 hash: ([0-9A-Fa-f]+)")
        message(WARNING "SignMacOS: identity '${OTERM_SIGN_IDENTITY}' not found; keeping ad-hoc signature")
        return()
    endif()
    set(_cert_hash "${CMAKE_MATCH_1}")
endif()

# Match the designated requirement used when packaging the .app bundle so the
# dev binary shares the same Keychain ACL subject.
set(_requirements "=designated => identifier \"com.oceanterm.app\" and certificate leaf = H\"${_cert_hash}\"")

execute_process(
    COMMAND codesign --force --sign "${OTERM_SIGN_IDENTITY}" --identifier com.oceanterm.app --requirements "${_requirements}" "${OTERM_BIN}"
    RESULT_VARIABLE _sign_result
)
if(NOT _sign_result EQUAL 0)
    message(WARNING "SignMacOS: codesign failed for ${OTERM_BIN}")
endif()
