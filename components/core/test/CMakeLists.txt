idf_component_register(
                SRCS 
                  "./expressions_unit_test.cpp"
                  "./RaftJsmn_test.cpp"
                  "./RaftJson_test.cpp"
                  "./ConfigBase_test.cpp"
                  "./RaftJson_perf_test.cpp"
                  "./exprCtx_unit_test.cpp"
                INCLUDE_DIRS
                  "."
                REQUIRES
                  unity
                  core
                  comms
                )

