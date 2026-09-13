#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <linux/i2c-dev.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

extern "C" {
#include "lsm6dsv16x_reg.h"
}

namespace {
constexpr int kDefaultBus = 1;
constexpr uint8_t kDefaultAddress = LSM6DSV16X_I2C_ADD_L >> 1;
constexpr int kDrdyGpio = 27;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegPerRad = 180.0f / kPi;

std::atomic<bool> running{true};

void stop_handler(int) { running = false; }

struct I2cDevice {
  int fd = -1;
  explicit I2cDevice(int bus, uint8_t address) {
    const std::string path = "/dev/i2c-" + std::to_string(bus);
    fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error(path + ": " + std::strerror(errno));
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
      const std::string error = "I2C address 0x" + std::to_string(address) +
                                ": " + std::strerror(errno);
      close(fd);
      fd = -1;
      throw std::runtime_error(error);
    }
  }
  ~I2cDevice() { if (fd >= 0) close(fd); }
  I2cDevice(const I2cDevice&) = delete;
};

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *data,
                       uint16_t length) {
  auto &device = *static_cast<I2cDevice *>(handle);
  std::array<uint8_t, 256> buffer{};
  if (static_cast<size_t>(length) + 1 > buffer.size()) return -EINVAL;
  buffer[0] = reg;
  std::memcpy(buffer.data() + 1, data, length);
  return write(device.fd, buffer.data(), length + 1) == length + 1 ? 0 : -errno;
}

int32_t platform_read(void *handle, uint8_t reg, uint8_t *data,
                      uint16_t length) {
  auto &device = *static_cast<I2cDevice *>(handle);
  if (write(device.fd, &reg, 1) != 1) return -errno;
  return read(device.fd, data, length) == length ? 0 : -errno;
}

void platform_delay(uint32_t milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

struct Quaternion {
  float w = 1.0f;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  void normalize() {
    const float length = std::sqrt(w * w + x * x + y * y + z * z);
    if (length > 0.0f) {
      w /= length; x /= length; y /= length; z /= length;
    }
  }

  void integrate(const std::array<float, 3> &gyro_rad_s, float dt) {
    const float half_dt = 0.5f * dt;
    const Quaternion delta{1.0f,
                           gyro_rad_s[0] * half_dt,
                           gyro_rad_s[1] * half_dt,
                           gyro_rad_s[2] * half_dt};
    const Quaternion old = *this;
    w = old.w * delta.w - old.x * delta.x - old.y * delta.y - old.z * delta.z;
    x = old.w * delta.x + old.x * delta.w + old.y * delta.z - old.z * delta.y;
    y = old.w * delta.y - old.x * delta.z + old.y * delta.w + old.z * delta.x;
    z = old.w * delta.z + old.x * delta.y - old.y * delta.x + old.z * delta.w;
    normalize();
  }

  std::array<float, 3> gravity_body() const {
    return {2.0f * (x * z - w * y), 2.0f * (w * x + y * z),
            w * w - x * x - y * y + z * z};
  }

  void correct_acceleration(std::array<float, 3> acceleration, float gain) {
    const float length = std::sqrt(acceleration[0] * acceleration[0] +
                                   acceleration[1] * acceleration[1] +
                                   acceleration[2] * acceleration[2]);
    if (length < 0.5f || length > 1.5f) return;
    for (float &value : acceleration) value /= length;
    const auto expected = gravity_body();
    const std::array<float, 3> error{
        expected[1] * acceleration[2] - expected[2] * acceleration[1],
        expected[2] * acceleration[0] - expected[0] * acceleration[2],
        expected[0] * acceleration[1] - expected[1] * acceleration[0]};
    const Quaternion correction{1.0f, gain * error[0], gain * error[1],
                                 gain * error[2]};
    const Quaternion old = *this;
    w = old.w * correction.w - old.x * correction.x - old.y * correction.y - old.z * correction.z;
    x = old.w * correction.x + old.x * correction.w + old.y * correction.z - old.z * correction.y;
    y = old.w * correction.y - old.x * correction.z + old.y * correction.w + old.z * correction.x;
    z = old.w * correction.z + old.x * correction.y - old.y * correction.x + old.z * correction.w;
    normalize();
  }

  std::array<float, 3> degrees() const {
    return {std::atan2(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)) * kDegPerRad,
            std::asin(std::clamp(2.0f * (w * y - z * x), -1.0f, 1.0f)) * kDegPerRad,
            std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z)) * kDegPerRad};
  }
};

}  // namespace

int main(int argc, char **argv) {
  const int bus = argc > 1 ? std::stoi(argv[1]) : kDefaultBus;
  const uint8_t address = argc > 2 ? static_cast<uint8_t>(std::stoul(argv[2], nullptr, 0))
                                   : kDefaultAddress;
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);

  try {
    I2cDevice device(bus, address);
    stmdev_ctx_t context{};
    context.write_reg = platform_write;
    context.read_reg = platform_read;
    context.mdelay = platform_delay;
    context.handle = &device;

    uint8_t device_id = 0;
    if (lsm6dsv16x_device_id_get(&context, &device_id) != 0 ||
        device_id != LSM6DSV16X_ID) {
      std::cerr << "LSM6DSV16X not found (WHO_AM_I=0x" << std::hex
                << static_cast<int>(device_id) << ")\n";
      return 1;
    }
    lsm6dsv16x_sw_por(&context);
    platform_delay(20);
    lsm6dsv16x_block_data_update_set(&context, PROPERTY_ENABLE);
    lsm6dsv16x_xl_data_rate_set(&context, LSM6DSV16X_ODR_AT_120Hz);
    lsm6dsv16x_gy_data_rate_set(&context, LSM6DSV16X_ODR_AT_120Hz);
    lsm6dsv16x_xl_full_scale_set(&context, LSM6DSV16X_2g);
    lsm6dsv16x_gy_full_scale_set(&context, LSM6DSV16X_2000dps);

    std::cout << "quota: LSM6DSV16X ready, I2C bus " << bus << ", address 0x"
              << std::hex << static_cast<int>(address) << std::dec
              << ", GPIO " << kDrdyGpio << " reserved for optional INT1\n";
    std::cout << "Quaternion (w x y z) | Degree (roll pitch yaw)\n";

    Quaternion orientation;
    auto previous = std::chrono::steady_clock::now();
    while (running) {
      lsm6dsv16x_data_ready_t ready{};
      if (lsm6dsv16x_flag_data_ready_get(&context, &ready) != 0 || !ready.drdy_xl || !ready.drdy_gy) {
        platform_delay(2);
        continue;
      }
      int16_t raw_acceleration[3]{};
      int16_t raw_gyro[3]{};
      lsm6dsv16x_acceleration_raw_get(&context, raw_acceleration);
      lsm6dsv16x_angular_rate_raw_get(&context, raw_gyro);
      const auto now = std::chrono::steady_clock::now();
      const float dt = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.001f, 0.1f);
      previous = now;
      const std::array<float, 3> acceleration{
          lsm6dsv16x_from_fs2_to_mg(raw_acceleration[0]) / 1000.0f,
          lsm6dsv16x_from_fs2_to_mg(raw_acceleration[1]) / 1000.0f,
          lsm6dsv16x_from_fs2_to_mg(raw_acceleration[2]) / 1000.0f};
      const std::array<float, 3> gyro{
          lsm6dsv16x_from_fs2000_to_mdps(raw_gyro[0]) / 1000.0f * kPi / 180.0f,
          lsm6dsv16x_from_fs2000_to_mdps(raw_gyro[1]) / 1000.0f * kPi / 180.0f,
          lsm6dsv16x_from_fs2000_to_mdps(raw_gyro[2]) / 1000.0f * kPi / 180.0f};
      orientation.integrate(gyro, dt);
      orientation.correct_acceleration(acceleration, 0.015f);
      const auto degrees = orientation.degrees();
      std::cout << std::fixed << std::setprecision(4)
                << "Q " << orientation.w << ' ' << orientation.x << ' '
                << orientation.y << ' ' << orientation.z << " | deg "
                << std::setprecision(2) << degrees[0] << ' ' << degrees[1]
                << ' ' << degrees[2] << "\r" << std::flush;
    }
    std::cout << '\n';
  } catch (const std::exception &error) {
    std::cerr << "quota: " << error.what() << '\n';
    return 1;
  }
  return 0;
}