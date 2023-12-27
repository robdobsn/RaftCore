idf_component_register(SRCS
        SRC_DIRS "."
        INCLUDE_DIRS "."
        REQUIRES
            esp_system
            unity
            RaftCore 
            nvs_flash
        WHOLE_ARCHIVE
        )
