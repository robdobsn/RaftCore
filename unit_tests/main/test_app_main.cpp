/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit Testing for RaftCore
//
// Rob Dobson 2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

void setUp(void)
{
}

void tearDown(void)
{
}

extern "C" void app_main(void)
{
    // Wait to allow serial port to be opened
    vTaskDelay(2000);

    // Run registered tests
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();

    // Run menu
    unity_run_menu();
}
