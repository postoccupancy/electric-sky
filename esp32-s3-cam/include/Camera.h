#pragma once

#include "esp_camera.h"

class Camera {
public:
  bool begin();
  camera_fb_t* capture();
  void release(camera_fb_t* fb);
};
