
// =================================================================================================
// This file is part of the CLTune project, which loosely follows the Google C++ styleguide and uses
// a tab-size of two spaces and a max-width of 100 characters per line.
//
// Author: cedric.nugteren@surfsara.nl (Cedric Nugteren)
//
// This file implements a bunch of C++11 classes that act as wrappers around OpenCL objects and API
// calls. The main benefits are increased abstraction, automatic memory management, and portability.
// Portability here means that a similar header exists for CUDA with the same classes and
// interfaces. In other words, moving from the OpenCL API to the CUDA API becomes a one-line change.
//
// This file is taken from the Claduc project <https://github.com/CNugteren/Claduc> and therefore
// contains the following header copyright notice:
//
// =================================================================================================
//
// Copyright 2015 SURFsara
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//  http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// =================================================================================================

// Difference with standard Claduc header: the Buffer class is not templated in this case, the size
// has to be multiplied by sizeof(T) by the user of the class.

#ifndef CLTUNE_CLPP11_H_
#define CLTUNE_CLPP11_H_

// C++
#include <algorithm> // std::copy
#include <string>    // std::string
#include <vector>    // std::vector
#include <memory>    // std::shared_ptr
#include <stdexcept> // std::runtime_error
#include <numeric>   // std::accumulate

// OpenCL
#if defined(__APPLE__) || defined(__MACOSX)
  #include <OpenCL/opencl.h>
#else
  #include <CL/opencl.h>
#endif

namespace cltune {
// =================================================================================================

// Error occurred in the C++11 OpenCL header (this file)
inline void Error(const std::string &message) {
  throw std::runtime_error("Internal OpenCL error: "+message);
}

// Error occurred in OpenCL
inline void CheckError(const cl_int status) {
  if (status != CL_SUCCESS) {
    throw std::runtime_error("Internal OpenCL error: "+std::to_string(status));
  }
}

// =================================================================================================

// C++11 version of 'cl_event'
class Event {
 public:

  // Constructor based on the regular OpenCL data-type
  explicit Event(const cl_event event): event_(event) { }

  // Regular constructor
  explicit Event() { }

  // Retrieves the elapsed time of the last recorded event. Note that no error checking is done on
  // the 'clGetEventProfilingInfo' function, since there is a bug in Apple's OpenCL implementation:
  // http://stackoverflow.com/questions/26145603/clgeteventprofilinginfo-bug-in-macosx
  float GetElapsedTime() const {
    CheckError(clWaitForEvents(1, &event_));
    auto bytes = size_t{0};
    clGetEventProfilingInfo(event_, CL_PROFILING_COMMAND_START, 0, nullptr, &bytes);
    auto time_start = size_t{0};
    clGetEventProfilingInfo(event_, CL_PROFILING_COMMAND_START, bytes, &time_start, nullptr);
    clGetEventProfilingInfo(event_, CL_PROFILING_COMMAND_END, 0, nullptr, &bytes);
    auto time_end = size_t{0};
    clGetEventProfilingInfo(event_, CL_PROFILING_COMMAND_END, bytes, &time_end, nullptr);
    return (time_end - time_start) * 1.0e-6f;
  }

  // Accessor to the private data-member
  cl_event& operator()() { return event_; }
 private:
  cl_event event_;
};

// =================================================================================================

// C++11 version of 'cl_platform_id'
class Platform {
 public:

  // Constructor based on the regular OpenCL data-type
  explicit Platform(const cl_platform_id platform): platform_(platform) { }

  // Initializes the platform
  explicit Platform(const size_t platform_id) {
    auto num_platforms = cl_uint{0};
    CheckError(clGetPlatformIDs(0, nullptr, &num_platforms));
    if (num_platforms == 0) { Error("no platforms found"); }
    auto platforms = std::vector<cl_platform_id>(num_platforms);
    CheckError(clGetPlatformIDs(num_platforms, platforms.data(), nullptr));
    if (platform_id >= num_platforms) { Error("invalid platform ID "+std::to_string(platform_id)); }
    platform_ = platforms[platform_id];
  }

  // Returns the number of devices on this platform
  size_t NumDevices() const {
    auto result = cl_uint{0};
    CheckError(clGetDeviceIDs(platform_, CL_DEVICE_TYPE_ALL, 0, nullptr, &result));
    return result;
  }

  // Accessor to the private data-member
  const cl_platform_id& operator()() const { return platform_; }
 private:
  cl_platform_id platform_;
};

// =================================================================================================

// C++11 version of 'cl_device_id'
class Device {
 public:

  // Constructor based on the regular OpenCL data-type
  explicit Device(const cl_device_id device): device_(device) { }

  // Initialize the device. Note that this constructor can throw exceptions!
  explicit Device(const Platform &platform, const size_t device_id) {
    auto num_devices = platform.NumDevices();
    if (num_devices == 0) { Error("no devices found"); }
    auto devices = std::vector<cl_device_id>(num_devices);
    CheckError(clGetDeviceIDs(platform(), CL_DEVICE_TYPE_ALL, (cl_uint) num_devices, devices.data(), nullptr));
    if (device_id >= num_devices) { Error("invalid device ID "+std::to_string(device_id)); }
    device_ = devices[device_id];
  }

  // Methods to retrieve device information
  std::string Version() const { return GetInfoString(CL_DEVICE_VERSION); }
  std::string Vendor() const { return GetInfoString(CL_DEVICE_VENDOR); }
  std::string Name() const { return GetInfoString(CL_DEVICE_NAME); }
  std::string Type() const {
    auto type = GetInfo<cl_device_type>(CL_DEVICE_TYPE);
    switch(type) {
      case CL_DEVICE_TYPE_CPU: return "CPU";
      case CL_DEVICE_TYPE_GPU: return "GPU";
      case CL_DEVICE_TYPE_ACCELERATOR: return "accelerator";
      default: return "default";
    }
  }
  size_t MaxWorkGroupSize() const { return GetInfo<size_t>(CL_DEVICE_MAX_WORK_GROUP_SIZE); }
  size_t MaxWorkItemDimensions() const {
    return GetInfo(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS);
  }
  std::vector<size_t> MaxWorkItemSizes() const {
    return GetInfoVector<size_t>(CL_DEVICE_MAX_WORK_ITEM_SIZES);
  }
  size_t LocalMemSize() const {
    return static_cast<size_t>(GetInfo<cl_ulong>(CL_DEVICE_LOCAL_MEM_SIZE));
  }
  std::string Capabilities() const { return GetInfoString(CL_DEVICE_EXTENSIONS); }
  size_t CoreClock() const { return GetInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY); }
  size_t ComputeUnits() const { return GetInfo(CL_DEVICE_MAX_COMPUTE_UNITS); }
  size_t MemorySize() const { return GetInfo(CL_DEVICE_GLOBAL_MEM_SIZE); }
  size_t MemoryClock() const { return 0; } // Not exposed in OpenCL
  size_t MemoryBusWidth() const { return 0; } // Not exposed in OpenCL

  // Configuration-validity checks
  bool IsLocalMemoryValid(const size_t local_mem_usage) const {
    return (local_mem_usage <= LocalMemSize());
  }
  bool IsThreadConfigValid(const std::vector<size_t> &local) const {
    auto local_size = size_t{1};
    for (const auto &item: local) { local_size *= item; }
    for (auto i=size_t{0}; i<local.size(); ++i) {
      if (local[i] > MaxWorkItemSizes()[i]) { return false; }
    }
    if (local_size > MaxWorkGroupSize()) { return false; }
    if (local.size() > MaxWorkItemDimensions()) { return false; }
    return true;
  }

  // Accessor to the private data-member
  const cl_device_id& operator()() const { return device_; }
 private:
  cl_device_id device_;

  // Private helper functions
  template <typename T>
  T GetInfo(const cl_device_info info) const {
    auto bytes = size_t{0};
    CheckError(clGetDeviceInfo(device_, info, 0, nullptr, &bytes));
    auto result = T(0);
    CheckError(clGetDeviceInfo(device_, info, bytes, &result, nullptr));
    return result;
  }
  size_t GetInfo(const cl_device_info info) const {
    auto bytes = size_t{0};
    CheckError(clGetDeviceInfo(device_, info, 0, nullptr, &bytes));
    auto result = cl_uint(0);
    CheckError(clGetDeviceInfo(device_, info, bytes, &result, nullptr));
    return static_cast<size_t>(result);
  }
  template <typename T>
  std::vector<T> GetInfoVector(const cl_device_info info) const {
    auto bytes = size_t{0};
    CheckError(clGetDeviceInfo(device_, info, 0, nullptr, &bytes));
    auto result = std::vector<T>(bytes/sizeof(T));
    CheckError(clGetDeviceInfo(device_, info, bytes, result.data(), nullptr));
    return result;
  }
  std::string GetInfoString(const cl_device_info info) const {
    auto bytes = size_t{0};
    CheckError(clGetDeviceInfo(device_, info, 0, nullptr, &bytes));
    auto result = std::string{};
    result.resize(bytes);
    CheckError(clGetDeviceInfo(device_, info, bytes, &result[0], nullptr));
    return std::string{result.c_str()}; // Removes any trailing '\0'-characters
  }
};

// =================================================================================================

// C++11 version of 'cl_context'
class Context {
 public:

  // Constructor based on the regular OpenCL data-type: memory management is handled elsewhere
  explicit Context(const cl_context context):
      context_(new cl_context) {
    *context_ = context;
  }

  // Regular constructor with memory management
  explicit Context(const Device &device):
      context_(new cl_context, [](cl_context* c) { CheckError(clReleaseContext(*c)); delete c; }) {
    auto status = CL_SUCCESS;
    const cl_device_id dev = device();
    *context_ = clCreateContext(nullptr, 1, &dev, nullptr, nullptr, &status);
    CheckError(status);
  }

  // Accessor to the private data-member
  const cl_context& operator()() const { return *context_; }
 private:
  std::shared_ptr<cl_context> context_;
};

// =================================================================================================

// Enumeration of build statuses of the run-time compilation process
enum class BuildStatus { kSuccess, kError, kInvalid };

// C++11 version of 'cl_program'. Additionally holds the program's source code.
class Program {
 public:
  // Note that there is no constructor based on the regular OpenCL data-type because of extra state

  // Regular constructor with memory management
  explicit Program(const Context &context, std::string source):
      program_(new cl_program, [](cl_program* p) { CheckError(clReleaseProgram(*p)); delete p; }),
      length_(source.length()),
      source_(std::move(source)),
      source_ptr_(&source_[0]) {
    auto status = CL_SUCCESS;
    *program_ = clCreateProgramWithSource(context(), 1, &source_ptr_, &length_, &status);
    CheckError(status);
  }

  // Compiles the device program and returns whether or not there where any warnings/errors
  BuildStatus Build(const Device &device, std::vector<std::string> &options) {
    auto options_string = std::accumulate(options.begin(), options.end(), std::string{" "});
    const cl_device_id dev = device();
    auto status = clBuildProgram(*program_, 1, &dev, options_string.c_str(), nullptr, nullptr);
    if (status == CL_BUILD_PROGRAM_FAILURE) {
      return BuildStatus::kError;
    }
    else if (status == CL_INVALID_BINARY) {
      return BuildStatus::kInvalid;
    }
    else {
      CheckError(status);
      return BuildStatus::kSuccess;
    }
  }

  // Retrieves the warning/error message from the compiler (if any)
  std::string GetBuildInfo(const Device &device) const {
    auto bytes = size_t{0};
    auto query = cl_program_build_info{CL_PROGRAM_BUILD_LOG};
    CheckError(clGetProgramBuildInfo(*program_, device(), query, 0, nullptr, &bytes));
    auto result = std::string{};
    result.resize(bytes);
    CheckError(clGetProgramBuildInfo(*program_, device(), query, bytes, &result[0], nullptr));
    return result;
  }

  // Retrieves an intermediate representation of the compiled program
  std::string GetIR() const {
    auto bytes = size_t{0};
    CheckError(clGetProgramInfo(*program_, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), &bytes, nullptr));
    auto result = std::string{};
    result.resize(bytes);
    auto result_ptr = result.data();
    CheckError(clGetProgramInfo(*program_, CL_PROGRAM_BINARIES, sizeof(char*), &result_ptr, nullptr));
    return result;
  }

  // Accessor to the private data-member
  const cl_program& operator()() const { return *program_; }
 private:
  std::shared_ptr<cl_program> program_;
  size_t length_;
  std::string source_;
  const char* source_ptr_;
};

// =================================================================================================

// C++11 version of 'cl_command_queue'
class Queue {
 public:

  // Constructor based on the regular OpenCL data-type: memory management is handled elsewhere
  explicit Queue(const cl_command_queue queue):
      queue_(new cl_command_queue) {
    *queue_ = queue;
  }

  // Regular constructor with memory management
  explicit Queue(const Context &context, const Device &device):
      queue_(new cl_command_queue, [](cl_command_queue* s) { CheckError(clReleaseCommandQueue(*s));
                                                             delete s; }) {
    auto status = CL_SUCCESS;
#ifdef CL_VERSION_2_0
    cl_queue_properties props[] = {
      CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE,
      0
    };
    *queue_ = clCreateCommandQueueWithProperties(context(), device(), props, &status);
#else
    *queue_ = clCreateCommandQueue(context(), device(), CL_QUEUE_PROFILING_ENABLE, &status);
#endif
    CheckError(status);
  }

  // Synchronizes the queue
  void Finish(Event &) const {
    Finish();
  }
  void Finish() const {
    CheckError(clFinish(*queue_));
  }

  // Retrieves the corresponding context or device
  Context GetContext() const {
    auto bytes = size_t{0};
    CheckError(clGetCommandQueueInfo(*queue_, CL_QUEUE_CONTEXT, 0, nullptr, &bytes));
    cl_context result;
    CheckError(clGetCommandQueueInfo(*queue_, CL_QUEUE_CONTEXT, bytes, &result, nullptr));
    return Context(result);
  }
  Device GetDevice() const {
    auto bytes = size_t{0};
    CheckError(clGetCommandQueueInfo(*queue_, CL_QUEUE_DEVICE, 0, nullptr, &bytes));
    cl_device_id result;
    CheckError(clGetCommandQueueInfo(*queue_, CL_QUEUE_DEVICE, bytes, &result, nullptr));
    return Device(result);
  }

  // Accessor to the private data-member
  const cl_command_queue& operator()() const { return *queue_; }
 private:
  std::shared_ptr<cl_command_queue> queue_;
};

// =================================================================================================

// C++11 version of host memory
template <typename T>
class BufferHost {
 public:

  // Regular constructor with memory management
  explicit BufferHost(const Context &, const size_t size):
      buffer_(new std::vector<T>(size)) {
  }

  // Retrieves the actual allocated size in bytes
  size_t GetSize() const {
    return buffer_->size()*sizeof(T);
  }

  // Compatibility with std::vector
  size_t size() const { return buffer_->size(); }
  T* begin() { return &(*buffer_)[0]; }
  T* end() { return &(*buffer_)[buffer_->size()-1]; }
  T& operator[](const size_t i) { return (*buffer_)[i]; }
  T* data() { return buffer_->data(); }
  const T* data() const { return buffer_->data(); }

 private:
  std::shared_ptr<std::vector<T>> buffer_;
};

// =================================================================================================

// Enumeration of buffer access types
enum class BufferAccess { kReadOnly, kWriteOnly, kReadWrite };

// C++11 version of 'cl_mem'
class Buffer {
 public:

  // Constructor based on the regular OpenCL data-type: memory management is handled elsewhere
  explicit Buffer(const cl_mem buffer):
      buffer_(new cl_mem),
      access_(BufferAccess::kReadWrite) {
    *buffer_ = buffer;
  }

  // Regular constructor with memory management
  explicit Buffer(const Context &context, const BufferAccess access, const size_t size):
      buffer_(new cl_mem, [](cl_mem* m) { CheckError(clReleaseMemObject(*m)); delete m; }),
      access_(access) {
    auto flags = cl_mem_flags{CL_MEM_READ_WRITE};
    if (access_ == BufferAccess::kReadOnly) { flags = CL_MEM_READ_ONLY; }
    if (access_ == BufferAccess::kWriteOnly) { flags = CL_MEM_WRITE_ONLY; }
    auto status = CL_SUCCESS;
    *buffer_ = clCreateBuffer(context(), flags, size, nullptr, &status);
    CheckError(status);
  }

  // As above, but now with read/write access as a default
  explicit Buffer(const Context &context, const size_t size):
    Buffer(context, BufferAccess::kReadWrite, size) {
  }

  // Copies from device to host: reading the device buffer a-synchronously
  template <typename T>
  void ReadAsync(const Queue &queue, const size_t size, T* host) {
    if (access_ == BufferAccess::kWriteOnly) { Error("reading from a write-only buffer"); }
    CheckError(clEnqueueReadBuffer(queue(), *buffer_, CL_FALSE, 0, size, host, 0,
                                   nullptr, nullptr));
  }
  template <typename T>
  void ReadAsync(const Queue &queue, const size_t size, std::vector<T> &host) {
    if (host.size()*sizeof(T) < size) { Error("target host buffer is too small"); }
    ReadAsync(queue, size, host.data());
  }
  template <typename T>
  void ReadAsync(const Queue &queue, const size_t size, BufferHost<T> &host) {
    if (host.size()*sizeof(T) < size) { Error("target host buffer is too small"); }
    ReadAsync(queue, size, host.data());
  }

  // Copies from device to host: reading the device buffer
  template <typename T>
  void Read(const Queue &queue, const size_t size, T* host) {
    ReadAsync(queue, size, host);
    queue.Finish();
  }
  template <typename T>
  void Read(const Queue &queue, const size_t size, std::vector<T> &host) {
    Read(queue, size, host.data());
  }
  template <typename T>
  void Read(const Queue &queue, const size_t size, BufferHost<T> &host) {
    Read(queue, size, host.data());
  }

  // Copies from host to device: writing the device buffer a-synchronously
  template <typename T>
  void WriteAsync(const Queue &queue, const size_t size, const T* host) {
    if (access_ == BufferAccess::kReadOnly) { Error("writing to a read-only buffer"); }
    if (GetSize() < size) { Error("target device buffer is too small"); }
    CheckError(clEnqueueWriteBuffer(queue(), *buffer_, CL_FALSE, 0, size, host, 0,
                                    nullptr, nullptr));
  }
  template <typename T>
  void WriteAsync(const Queue &queue, const size_t size, const std::vector<T> &host) {
    WriteAsync(queue, size, host.data());
  }
  template <typename T>
  void WriteAsync(const Queue &queue, const size_t size, const BufferHost<T> &host) {
    WriteAsync(queue, size, host.data());
  }

  // Copies from host to device: writing the device buffer
  template <typename T>
  void Write(const Queue &queue, const size_t size, const T* host) {
    WriteAsync(queue, size, host);
    queue.Finish();
  }
  template <typename T>
  void Write(const Queue &queue, const size_t size, const std::vector<T> &host) {
    Write(queue, size, host.data());
  }
  template <typename T>
  void Write(const Queue &queue, const size_t size, const BufferHost<T> &host) {
    Write(queue, size, host.data());
  }

  // Copies the contents of this buffer into another device buffer
  void CopyToAsync(const Queue &queue, const size_t size, const Buffer &destination) {
    CheckError(clEnqueueCopyBuffer(queue(), *buffer_, destination(), 0, 0, size, 0,
                                   nullptr, nullptr));
  }
  void CopyTo(const Queue &queue, const size_t size, const Buffer &destination) {
    CopyToAsync(queue, size, destination);
    queue.Finish();
  }

  // Retrieves the actual allocated size in bytes
  size_t GetSize() const {
    auto bytes = size_t{0};
    CheckError(clGetMemObjectInfo(*buffer_, CL_MEM_SIZE, 0, nullptr, &bytes));
    auto result = size_t{0};
    CheckError(clGetMemObjectInfo(*buffer_, CL_MEM_SIZE, bytes, &result, nullptr));
    return result;
  }

  // Accessor to the private data-member
  const cl_mem& operator()() const { return *buffer_; }
 private:
  std::shared_ptr<cl_mem> buffer_;
  const BufferAccess access_;
};

// =================================================================================================

// C++11 version of 'cl_kernel'
class Kernel {
 public:

  // Constructor based on the regular OpenCL data-type: memory management is handled elsewhere
  explicit Kernel(const cl_kernel kernel):
      kernel_(new cl_kernel) {
    *kernel_ = kernel;
  }

  // Regular constructor with memory management
  explicit Kernel(const Program &program, const std::string &name):
      kernel_(new cl_kernel, [](cl_kernel* k) { CheckError(clReleaseKernel(*k)); delete k; }) {
    auto status = CL_SUCCESS;
    *kernel_ = clCreateKernel(program(), name.c_str(), &status);
    CheckError(status);
  }

  // Sets a kernel argument at the indicated position
  template <typename T>
  void SetArgument(const size_t index, const T &value) {
    CheckError(clSetKernelArg(*kernel_, static_cast<cl_uint>(index), sizeof(T), &value));
  }
  template <typename T>
  void SetArgument(const size_t index, Buffer &value) {
    SetArgument(index, value());
  }

  // Sets all arguments in one go using parameter packs. Note that this overwrites previously set
  // arguments using 'SetArgument' or 'SetArguments'.
  template <typename... Args>
  void SetArguments(Args&... args) {
    SetArgumentsRecursive(0, args...);
  }

  // Retrieves the amount of local memory used per work-group for this kernel
  size_t LocalMemUsage(const Device &device) const {
    auto bytes = size_t{0};
    auto query = cl_kernel_work_group_info{CL_KERNEL_LOCAL_MEM_SIZE};
    CheckError(clGetKernelWorkGroupInfo(*kernel_, device(), query, 0, nullptr, &bytes));
    auto result = size_t{0};
    CheckError(clGetKernelWorkGroupInfo(*kernel_, device(), query, bytes, &result, nullptr));
    return result;
  }

  // Launches a kernel onto the specified queue
  void Launch(const Queue &queue, const std::vector<size_t> &global,
              const std::vector<size_t> &local, Event &event) {
    CheckError(clEnqueueNDRangeKernel(queue(), *kernel_, static_cast<cl_uint>(global.size()),
                                      nullptr, global.data(), local.data(),
                                      0, nullptr, &(event())));
  }

  // Accessor to the private data-member
  const cl_kernel& operator()() const { return *kernel_; }
 private:
  std::shared_ptr<cl_kernel> kernel_;

  // Internal implementation for the recursive SetArguments function.
  template <typename T>
  void SetArgumentsRecursive(const size_t index, T &first) {
    SetArgument(index, first);
  }
  template <typename T, typename... Args>
  void SetArgumentsRecursive(const size_t index, T &first, Args&... args) {
    SetArgument(index, first);
    SetArgumentsRecursive(index+1, args...);
  }
};

// =================================================================================================
} // namespace cltune

// CLTUNE_CLPP11_H_
#endif
