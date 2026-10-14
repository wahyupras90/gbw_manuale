#pragma once
// Handler assert LVGL (dipasang lewat LV_ASSERT_HANDLER di lv_conf.h).
// Didefinisikan di main.cpp: simpan checkpoint "lvgl_assert" ke NVS lalu esp_restart().
#ifdef __cplusplus
extern "C" {
#endif
void lvgl_assert_fail(void);
#ifdef __cplusplus
}
#endif
