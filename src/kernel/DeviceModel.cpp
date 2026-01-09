#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"


/*
=========================================================
 STAGE 6 — DEVICE & DRIVER MODEL
=========================================================
*/

namespace DeviceModel {

enum class DeviceType : uint8_t { Keyboard, Mouse, Disk, Display };

struct Device {
  DeviceType type;
  const char *name;
  uint32_t id;
  void *driver_data; // driver-specific storage
};

constexpr uint32_t MAX_DEVICES = 32;
static Device g_devices[MAX_DEVICES];
static uint32_t g_device_count = 0;
static uint32_t g_next_device_id = 1;

Device *register_device(DeviceType type, const char *name,
                        void *driver_data = nullptr) {
  CONTRACT_ASSERT(g_device_count < MAX_DEVICES, "Device: table full");

  Device &d = g_devices[g_device_count++];
  d.id = g_next_device_id++;
  d.type = type;
  d.name = name;
  d.driver_data = driver_data;
  return &d;
}

Device *get_device_by_id(uint32_t id) {
  for (uint32_t i = 0; i < g_device_count; i++) {
    if (g_devices[i].id == id)
      return &g_devices[i];
  }
  return nullptr;
}

bool unregister_device(uint32_t id) {
  for (uint32_t i = 0; i < g_device_count; i++) {
    if (g_devices[i].id == id) {
      for (uint32_t j = i; j < g_device_count - 1; j++)
        g_devices[j] = g_devices[j + 1];
      g_device_count--;
      return true;
    }
  }
  return false;
}

void stage6_self_test() {
  serial_log("MODEL: Beginning Stage 6 Device Model Test...");

  Device *kb = register_device(DeviceType::Keyboard, "GenericKeyboard");
  Device *mouse = register_device(DeviceType::Mouse, "GenericMouse");
  Device *disk = register_device(DeviceType::Disk, "FakeDisk");
  Device *disp = register_device(DeviceType::Display, "FrameBuffer0");

  CONTRACT_ASSERT(kb != nullptr, "Device: Keyboard registration failed");
  CONTRACT_ASSERT(mouse != nullptr, "Device: Mouse registration failed");
  CONTRACT_ASSERT(disk != nullptr, "Device: Disk registration failed");
  CONTRACT_ASSERT(disp != nullptr, "Device: Display registration failed");

  CONTRACT_ASSERT(unregister_device(kb->id),
                  "Device: Keyboard unregister failed");
  CONTRACT_ASSERT(g_device_count == 3,
                  "Device: count mismatch after unregister");

  serial_log("MODEL: Stage 6 Device Model Passed.");
}

} // namespace DeviceModel

extern "C" void stage6_self_test() { DeviceModel::stage6_self_test(); }
