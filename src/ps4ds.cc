#include "ps4ds.h"
#include "crc32.h"

#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>


namespace ps4ds {

Manager::Manager(SInt32 vendor, SInt32 product) {
  loop_ = CFRunLoopGetMain();
  assert(loop_ != NULL);

  QUEUE_INIT(&devices_);

  hid_manager_ = IOHIDManagerCreate(NULL, 0);
  assert(hid_manager_ != NULL);

  IOHIDManagerRegisterDeviceMatchingCallback(hid_manager_,
                                             OnMatchingDevice,
                                             this);
  IOHIDManagerScheduleWithRunLoop(hid_manager_, loop_, kCFRunLoopDefaultMode);

  // Accept every device, until filter will be set
  CFDictionaryRef filter = Filter(vendor, product);
  IOHIDManagerSetDeviceMatching(hid_manager_, filter);
  CFRelease(filter);

  IOReturn ret = IOHIDManagerOpen(hid_manager_, 0);
  assert(ret == kIOReturnSuccess);
}


Manager::~Manager() {
  while (!QUEUE_EMPTY(&devices_))
    delete Device::FromQueue(QUEUE_HEAD(&devices_));

  IOHIDManagerUnscheduleFromRunLoop(hid_manager_, loop_, kCFRunLoopDefaultMode);
  IOHIDManagerClose(hid_manager_, 0);
  CFRelease(hid_manager_);
  hid_manager_ = NULL;
}


void Manager::OnMatchingDevice(void* ctx,
                               IOReturn ret,
                               void* sender,
                               IOHIDDeviceRef dev) {
  Manager* manager;

  assert(ret == kIOReturnSuccess);
  manager = reinterpret_cast<Manager*>(ctx);
  manager->OnDevice(dev);
}


CFDictionaryRef Manager::Filter(SInt32 vendor, SInt32 product) {
  CFMutableDictionaryRef res = CFDictionaryCreateMutable(
      NULL,
      2,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  assert(res != NULL);

  CFNumberRef val = CFNumberCreate(NULL, kCFNumberSInt32Type, &vendor);
  assert(val != NULL);
  CFDictionaryAddValue(res, CFSTR(kIOHIDVendorIDKey), val);
  CFRelease(val);

  val = CFNumberCreate(NULL, kCFNumberSInt32Type, &product);
  assert(val != NULL);
  CFDictionaryAddValue(res, CFSTR(kIOHIDProductIDKey), val);
  CFRelease(val);

  return res;
}


void Manager::OnDevice(IOHIDDeviceRef dev) {
  Device* d = new Device(this, dev);

  QUEUE_INSERT_TAIL(&devices_, d->member());
}


Device::Device(Manager* m, IOHIDDeviceRef dev) : manager_(m), dev_(dev) {
  IOHIDDeviceRegisterInputReportCallback(dev_,
                                         report_,
                                         sizeof(report_),
                                         OnReport,
                                         this);
  IOHIDDeviceScheduleWithRunLoop(dev_, manager_->loop(), kCFRunLoopDefaultMode);

  CFRunLoopTimerContext timer_ctx = {
    .version = 0,
    .info = this,
    .retain = NULL,
    .release = NULL,
    .copyDescription = NULL
  };
  updater_ = CFRunLoopTimerCreate(NULL,
                                  CFAbsoluteTimeGetCurrent(),
                                  0.05,
                                  0,
                                  0,
                                  Update,
                                  &timer_ctx);
  CFRunLoopAddTimer(manager_->loop(), updater_, kCFRunLoopCommonModes);

  // Destroy on disconnect
  IOHIDDeviceRegisterRemovalCallback(dev_, OnRemove, this);
}


Device::~Device() {
  IOHIDDeviceUnscheduleFromRunLoop(dev_,
                                   manager_->loop(),
                                   kCFRunLoopDefaultMode);
  CFRunLoopRemoveTimer(manager_->loop(), updater_, kCFRunLoopCommonModes);
  CFRelease(updater_);

  HIDOutput out;
  memset(&out, 0, sizeof(out));
  out.color.r = 127;
  out.color.g = 127;
  out.color.b = 127;
  SendHIDOutput(&out);

  QUEUE_REMOVE(&member_);
}


Device* Device::FromQueue(QUEUE* q) {
  return ContainerOf(&Device::member_, q);
}


void Device::OnReport(void* ctx,
                      IOReturn ret,
                      void* sender,
                      IOHIDReportType type,
                      uint32_t report_id,
                      uint8_t* report,
                      CFIndex report_length) {
  Device* d = reinterpret_cast<Device*>(ctx);

  d->OnReport(type, report_id, report, report_length);
}


IOReturn Device::Send(const uint8_t* data, int len) {
  return IOHIDDeviceSetReport(dev_, kIOHIDReportTypeOutput, data[0], data, len);
}

void Device::SendHIDOutput(HIDOutput* out) {
  uint8_t data[] = {
    0xa2, 0x11, 0xc0, 0x20,
    0xf3, 0x04, 0x00,
    out->rumble.left, out->rumble.right,

    out->color.r, out->color.g, out->color.b,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x43, 0x43, 0x00, 0x4d, 0x85, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // crc32
    0xde, 0xad, 0xbe, 0xef
  };

  // Put LE crc32
  uint32_t crc = crc32(0, data, sizeof(data) - 4);
  data[75] = crc & 0xff;
  data[76] = (crc >> 8) & 0xff;
  data[77] = (crc >> 16) & 0xff;
  data[78] = crc >> 24;

  Send(data + 1, sizeof(data) - 1);
}


void Device::OnReport(IOHIDReportType type,
                      uint32_t id,
                      uint8_t* report,
                      int length) {
  fprintf(stderr, "type %d id %d len %d\n", type, id, length);
}


void Device::Update(CFRunLoopTimerRef timer, void* ctx) {
  Device* dev = reinterpret_cast<Device*>(ctx);

  dev->Update();
}


void Device::Update() {
  struct timeval tp;
  int err;
  double time;

  err = gettimeofday(&tp, NULL);
  assert(err == 0);

  time = tp.tv_sec + (double) tp.tv_usec / 1e6;

  double rumble = 0;

  HIDOutput out = {
    .rumble = {
      .left = rumble,
      .right = rumble
    },
    .color = {
      .r = 128.0 + sin(5.0 * time) * 127.0,
      .g = 128.0 + sin(5.0 * time + M_PI / 3) * 127.0,
      .b = 128.0 + sin(5.0 * time + 2 * M_PI / 3) * 127.0
    }
  };
  SendHIDOutput(&out);
}


void Device::OnRemove(void* ctx, IOReturn ret, void* sender) {
  delete reinterpret_cast<Device*>(ctx);
}

}  // namespace ps4ds


static void on_sigint(int sig) {
  CFRunLoopStop(CFRunLoopGetMain());
}


int main(int argc, char** argv) {
  // PS4 DualShock
  ps4ds::Manager m(0x54c, 0x5c4);

  struct sigaction sa;
  sa.sa_flags = 0;
  sa.sa_handler = on_sigint;
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);

  CFRunLoopRun();

  return 0;
}
