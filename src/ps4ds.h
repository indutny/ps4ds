#ifndef SRC_PS4DS_H_
#define SRC_PS4DS_H_

#include "queue.h"
#include "common.h"
#include "common-inl.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>

namespace ps4ds {

// Forward declarations
class Device;

class Manager {
 public:
  Manager(SInt32 vendor, SInt32 product);
  ~Manager();

  inline CFRunLoopRef loop() const { return loop_; }

 private:
  CFDictionaryRef Filter(SInt32 vendor, SInt32 product);

  void OnDevice(IOHIDDeviceRef dev);

  static void OnMatchingDevice(void* ctx,
                               IOReturn ret,
                               void* sender,
                               IOHIDDeviceRef dev);

  CFRunLoopRef loop_;
  IOHIDManagerRef hid_manager_;
  QUEUE devices_;
};

class Device {
 public:
  Device(Manager* m, IOHIDDeviceRef dev);
  ~Device();

  struct HIDOutput {
    struct {
      uint8_t left;
      uint8_t right;
    } rumble;

    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
    } color;
  };

  void SendHIDOutput(HIDOutput* out);
  static Device* FromQueue(QUEUE* q);

  inline QUEUE* member() { return &member_; }


 private:
  IOReturn Send(const uint8_t* data, int len);
  void OnReport(IOHIDReportType type,
                uint32_t id,
                uint8_t* report,
                int length);
  void Update();

  static void Update(CFRunLoopTimerRef timer, void* ctx);
  static void OnReport(void* ctx,
                       IOReturn ret,
                       void* sender,
                       IOHIDReportType type,
                       uint32_t report_id,
                       uint8_t* report,
                       CFIndex report_length);
  static void OnRemove(void* ctx, IOReturn ret, void* sender);

  static const int kReportSize = 1024;
  uint8_t report_[kReportSize];

  Manager* manager_;
  IOHIDDeviceRef dev_;
  QUEUE member_;
  CFRunLoopTimerRef updater_;
};

}  // namespace ps4ds

#endif  // SRC_PS4DS_H_
