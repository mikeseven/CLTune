
// =================================================================================================
// This file is part of the CLTune project, which loosely follows the Google C++ styleguide and uses
// a tab-size of two spaces and a max-width of 100 characters per line.
//
// Author: cedric.nugteren@surfsara.nl (Cedric Nugteren)
//
// This file implements the Memory class (see the header for information about the class).
//
// -------------------------------------------------------------------------------------------------
//
// Copyright 2014 SURFsara
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

#include "tuner/internal/memory.h"

namespace cltune {
// =================================================================================================

// Specific implementations of the helper structure to get the memory-type based on a template
// argument. Supports all enumerations of MemType.
template <> const MemType Memory<int>::type = kInt;
template <> const MemType Memory<float>::type = kFloat;
template <> const MemType Memory<double>::type = kDouble;

// Initializes the memory class, creating a host array with zeroes and an uninitialized device
// buffer.
template <typename T>
Memory<T>::Memory(const size_t size, cl::Context context, cl::CommandQueue queue):
    size_(size),
    host_(size),
    device_(new cl::Buffer(context, CL_MEM_READ_WRITE, size*sizeof(T))),
    context_(context),
    queue_(queue) {
  for (auto &item: host_) { item = static_cast<T>(0); }
}

// As above, but now initializes to a specific value based on a source vector.
template <typename T>
Memory<T>::Memory(const size_t size, cl::Context context, cl::CommandQueue queue,
                  std::vector<T> &source):
    size_(size),
    host_(source),
    device_(new cl::Buffer(context, CL_MEM_READ_WRITE, size*sizeof(T))),
    context_(context),
    queue_(queue) {
}

// =================================================================================================

// Uses the OpenCL C++ function enqueueWriteBuffer to upload the data to the device
template <typename T>
void Memory<T>::UploadToDevice() {
  cl_int status = queue_.enqueueWriteBuffer(*device_, CL_TRUE, 0, size_*sizeof(T), host_.data());
  if (status != CL_SUCCESS) { throw OpenCLException("Write buffer error", status); }
}

// Uses the OpenCL C++ function enqueueReadBuffer to download the data from the device
template <typename T>
void Memory<T>::DownloadFromDevice() {
  cl_int status = queue_.enqueueReadBuffer(*device_, CL_TRUE, 0, size_*sizeof(T), host_.data());
  if (status != CL_SUCCESS) { throw OpenCLException("Write buffer error", status); }
}

// =================================================================================================

// Compiles the templated class for all datatypes supported by MemType
template class Memory<int>;
template class Memory<float>;
template class Memory<double>;

// =================================================================================================
} // namespace cltune