#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_element/touch_slider.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

bool button_pressed = false;
bool long_press = false;
const int LONG_PRESS_TIME = 1000;  // 长按阈值设定为 2000 毫秒（2秒）
unsigned long press_time = 0;
int coll_light_pwm = 0;  // 存储冷光的 PWM 值
int warm_light_pwm = 0;  // 存储暖光的 PWM 值
bool light_on = false;   // 灯的状态
bool is_cool_light = true;


static const char *TAG = "Touch Slider Example";
#define TOUCH_SLIDER_CHANNEL_NUM 9

static touch_slider_handle_t slider_handle;

static const touch_pad_t channel_array[TOUCH_SLIDER_CHANNEL_NUM] = {
  TOUCH_PAD_NUM1,
  TOUCH_PAD_NUM2,
  TOUCH_PAD_NUM3,
  TOUCH_PAD_NUM4,
  TOUCH_PAD_NUM5,
  TOUCH_PAD_NUM6,
  TOUCH_PAD_NUM7,
  TOUCH_PAD_NUM8,
  TOUCH_PAD_NUM9,
};


static const float channel_sens_array[TOUCH_SLIDER_CHANNEL_NUM] = {
  0.252F,
  0.246F,
  0.277F,
  0.250F,
  0.257F,
  0.252F,
  0.246F,
  0.277F,
  0.250F,
};

uint32_t coll_light, warm_light, tmp;

static void slider_handler_task(void *arg) {
  (void)arg;
  touch_elem_message_t element_message;
  while (1) {
    /* 触摸条回调线程 */
    if (touch_element_message_receive(&element_message, portMAX_DELAY) == ESP_OK) {
      if (element_message.element_type != TOUCH_ELEM_TYPE_SLIDER) {
        continue;
      }
      const touch_slider_message_t *slider_message = touch_slider_get_message(&element_message);
      if (slider_message->event == TOUCH_SLIDER_EVT_ON_PRESS) {
        Serial.printf("Slider Press, position: %" PRIu32, slider_message->position);
        Serial.printf("\n");
        if (light_on) {
          tmp = slider_message->position;
        }
      } else if (slider_message->event == TOUCH_SLIDER_EVT_ON_RELEASE) {
        Serial.printf("Slider Release, position: %" PRIu32, slider_message->position);
        Serial.printf("\n");

      } else if (slider_message->event == TOUCH_SLIDER_EVT_ON_CALCULATION) {
        Serial.printf("Slider Calculate, position: %" PRIu32, slider_message->position);
        Serial.printf("\n");
        if (light_on) {
          uint32_t new_position = slider_message->position;
          if (tmp < new_position) {
            if (is_cool_light) {
              coll_light += new_position - tmp;
              coll_light = (coll_light > 511) ? 511 : coll_light;  // 限制最大值
            } else {
              warm_light += new_position - tmp;
              warm_light = (warm_light > 511) ? 511 : warm_light;  // 限制最大值
            }
          } else if (tmp > new_position) {
            int32_t delta = tmp - new_position;
            if (is_cool_light) {
              coll_light = (coll_light > delta) ? coll_light - delta : 0;  // 防止负数下溢
            } else {
              warm_light = (warm_light > delta) ? warm_light - delta : 0;  // 防止负数下溢
            }
          }

          // 更新亮度
          if (is_cool_light) {
            ledcWrite(1, coll_light);
          } else {
            ledcWrite(0, warm_light);
          }

          tmp = new_position;
        }
      }
    }
  }
}

void app_main(void) {
  /* Initialize Touch Element library */
  touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
  ESP_ERROR_CHECK(touch_element_install(&global_config));
  Serial.printf("Touch element library installed");

  touch_slider_global_config_t slider_global_config = TOUCH_SLIDER_GLOBAL_DEFAULT_CONFIG();
  ESP_ERROR_CHECK(touch_slider_install(&slider_global_config));
  Serial.printf("Touch slider installed");
  /* Create Touch slider */
  touch_slider_config_t slider_config = {
    .channel_array = channel_array,
    .sensitivity_array = channel_sens_array,
    .channel_num = (sizeof(channel_array) / sizeof(channel_array[0])),
    .position_range = 255
  };
  ESP_ERROR_CHECK(touch_slider_create(&slider_config, &slider_handle));
  /* Subscribe touch slider events (On Press, On Release, On Calculation) */
  ESP_ERROR_CHECK(touch_slider_subscribe_event(slider_handle,
                                               TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_CALCULATION, NULL));
  ESP_ERROR_CHECK(touch_slider_set_dispatch_method(slider_handle, TOUCH_ELEM_DISP_EVENT));
  /* Create a handler task to handle event messages */
  xTaskCreate(&slider_handler_task, "slider_handler_task", 4 * 1024, NULL, 5, NULL);

  Serial.printf("Touch slider created");
  touch_element_start();
  Serial.printf("Touch element library start");
}

#define LEDC_TIMER_12_BIT 9

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000);

  esp_task_wdt_init(3000,1);

  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  ESP_ERROR_CHECK(esp_task_wdt_status(NULL));


  esp_task_wdt_reset();

  app_main();
  pinMode(11, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);

  ledcSetup(0, 60 * 1000, LEDC_TIMER_12_BIT);
  ledcAttachPin(39, 0);  //暖光
  ledcSetup(1, 60 * 1000, LEDC_TIMER_12_BIT);
  ledcAttachPin(40, 1);  //冷光
}

void loop() {
  esp_task_wdt_reset();
  vTaskDelay(100);
  if (digitalRead(10) == LOW) {
    // 切换冷暖光控制
    is_cool_light = !is_cool_light;
    vTaskDelay(200);  // 防抖延时
  }

  if (digitalRead(11) == LOW) {
    if (!button_pressed) {
      button_pressed = true;
      press_time = millis();
    } else if (millis() - press_time > LONG_PRESS_TIME) {
      long_press = true;
    }
  } else {
    if (button_pressed) {
      if (long_press) {
        // 处理长按事件
        light_on = !light_on;  // 切换灯的状态
        if (light_on) {
          // 恢复灯光到之前的 PWM 值
          warm_light = 511;
          coll_light = 511;
          ledcWrite(0, warm_light);
          ledcWrite(1, coll_light);
        } else {
          // 存储当前 PWM 值并关闭灯光
          ledcWrite(0, 0);
          ledcWrite(1, 0);
        }
      } else {
        // 处理短按事件
        // 切换灯光状态
        if (light_on) {
          ledcWrite(0, 0);
          ledcWrite(1, 0);
        } else {
          ledcWrite(0, warm_light);
          ledcWrite(1, coll_light);
        }
        light_on = !light_on;
      }
      button_pressed = false;
      long_press = false;
    }
  }
}
