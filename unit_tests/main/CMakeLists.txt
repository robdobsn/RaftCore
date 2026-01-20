idf_component_register(SRCS
        # SRC_DIRS "."
        SRCS
            "test_app_main.cpp"
            "RaftJson_test_no_path.cpp"
            "RaftJson_test_nv_pairs.cpp"
            "RaftJson_test_values.cpp"
            "RaftJsonPrefixed_test.cpp"
            "exprCtx_unit_test.cpp"
            "expressions_unit_test.cpp"
            "ProtocolOverAscii_test.cpp"
            "RaftJson_perf_test_large.cpp"
            "RaftJson_perf_test_small.cpp"
            "RaftJson_perf_extract.cpp"
            "RaftJsmn_test.cpp"
            "ForComparison/RaftJsmn.cpp"
            "ForComparison/RaftJson_jsmn.cpp"
            "SysTypeManager_test.cpp"
            "FileSystem_test.cpp"
            "RestAPIEndpointManager_test.cpp"

            # The following test requires rebooting the ESP32 so isn't
            # included in the default test suite.
            # "RaftJsonNVS_test.cpp"
    
            # The following tests are for comparison with the jsmn library
            # "RaftJson_jsmn_test.cpp"
            # "RaftJson_perf_test_jsmn.cpp"

        INCLUDE_DIRS 
            "."
            "ForComparison"
        REQUIRES
            esp_system
            unity
            RaftCore 
            nvs_flash
        WHOLE_ARCHIVE
        )
