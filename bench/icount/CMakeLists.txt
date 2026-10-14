# One binary per scenario (layer:rate): bare-metal targets have no argv, and
# per-binary instruction totals are what the ratchet compares. The scenario
# name (after the mutap_icount_ prefix) is the key in bench/baselines.json.
set(_mutap_icount_scenarios
    fdkf_48k:0:0
    fdkf_16k:0:1
    suppressor_48k:1:0
    suppressor_16k:1:1
    shadow_48k:2:0
    shadow_16k:2:1
    chain_48k:3:0
    chain_16k:3:1
    nn_suppressor_48k:4:0
    nn_suppressor_16k:4:1
    # The AFC path at both candidate product geometries (production-readiness
    # M2a; the second index is the geometry here: 0 = block 256 / 2048 taps,
    # 1 = block 64 / 1024 taps, both 48 kHz — icount_main.cpp).
    afc_speech_256:5:0
    afc_speech_64:5:1
    afc_warped_256:6:0
    afc_warped_64:6:1
    guard_m1_256:7:0
    guard_m1_64:7:1)

foreach(_sc IN LISTS _mutap_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _layer)
    list(GET _parts 2 _rate)
    add_executable(mutap_icount_${_name} icount_main.cpp)
    target_compile_definitions(mutap_icount_${_name} PRIVATE
        MUTAP_SC_LAYER=${_layer} MUTAP_SC_RATE=${_rate})
    target_link_libraries(mutap_icount_${_name} PRIVATE
        MuTap::MuTap mutap_warnings)
    if(MUTAP_BARE_METAL)
        # The M55 leg links the semihosting startup via the toolchain's
        # linker flags exactly as the test binary does; nothing extra here.
    endif()
endforeach()
