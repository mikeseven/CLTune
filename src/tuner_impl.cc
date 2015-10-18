
// =================================================================================================
// This file is part of the CLTune project, which loosely follows the Google C++ styleguide and uses
// a tab-size of two spaces and a max-width of 100 characters per line.
//
// Author: cedric.nugteren@surfsara.nl (Cedric Nugteren)
//
// This file implements the TunerImpl class (see the header for information about the class).
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

// The corresponding header file
#include "internal/tuner_impl.h"

// The search strategies
#include "internal/searchers/full_search.h"
#include "internal/searchers/random_search.h"
#include "internal/searchers/annealing.h"
#include "internal/searchers/pso.h"

// The machine learning models
#include "internal/ml_models/linear_regression.h"

#include <fstream> // std::ifstream, std::stringstream
#include <iostream> // FILE
#include <limits> // std::numeric_limits
#include <algorithm> // std::min
#include <memory> // std::unique_ptr
#include <tuple> // std::tuple

namespace cltune {
// =================================================================================================

// Messages printed to stdout (in colours)
const std::string TunerImpl::kMessageFull    = "\x1b[32m[==========]\x1b[0m";
const std::string TunerImpl::kMessageHead    = "\x1b[32m[----------]\x1b[0m";
const std::string TunerImpl::kMessageRun     = "\x1b[32m[ RUN      ]\x1b[0m";
const std::string TunerImpl::kMessageInfo    = "\x1b[32m[   INFO   ]\x1b[0m";
const std::string TunerImpl::kMessageOK      = "\x1b[32m[       OK ]\x1b[0m";
const std::string TunerImpl::kMessageWarning = "\x1b[33m[  WARNING ]\x1b[0m";
const std::string TunerImpl::kMessageFailure = "\x1b[31m[   FAILED ]\x1b[0m";
const std::string TunerImpl::kMessageResult  = "\x1b[32m[ RESULT   ]\x1b[0m";
const std::string TunerImpl::kMessageBest    = "\x1b[35m[     BEST ]\x1b[0m";
  
// =================================================================================================

// Initializes the platform and device to the default 0
TunerImpl::TunerImpl():
    platform_(Platform(size_t{0})),
    device_(Device(platform_, size_t{0})),
    context_(Context(device_)),
    queue_(Queue(context_, device_)),
    has_reference_(false),
    suppress_output_(false),
    output_search_process_(false),
    search_log_filename_(std::string{}),
    search_method_(SearchMethod::FullSearch),
    search_args_(0),
    argument_counter_(0) {
  if (!suppress_output_) {
    fprintf(stdout, "\n%s Initializing OpenCL on platform 0 device 0\n", kMessageFull.c_str());
    auto opencl_version = device_.Version();
    auto device_name = device_.Name();
    fprintf(stdout, "%s Device name: '%s' (%s)\n", kMessageFull.c_str(),
            device_name.c_str(), opencl_version.c_str());
  }
}

// Initializes with a custom platform and device
TunerImpl::TunerImpl(size_t platform_id, size_t device_id):
    platform_(Platform(platform_id)),
    device_(Device(platform_, device_id)),
    context_(Context(device_)),
    queue_(Queue(context_, device_)),
    has_reference_(false),
    suppress_output_(false),
    output_search_process_(false),
    search_log_filename_(std::string{}),
    search_method_(SearchMethod::FullSearch),
    search_args_(0),
    argument_counter_(0) {
  if (!suppress_output_) {
    fprintf(stdout, "\n%s Initializing OpenCL on platform %zu device %zu\n",
            kMessageFull.c_str(), platform_id, device_id);
    auto opencl_version = device_.Version();
    auto device_name = device_.Name();
    fprintf(stdout, "%s Device name: '%s' (%s)\n", kMessageFull.c_str(),
            device_name.c_str(), opencl_version.c_str());
  }
}

// End of the tuner
TunerImpl::~TunerImpl() {
  for (auto &reference_output: reference_outputs_) {
    delete[] static_cast<int*>(reference_output);
  }
  if (!suppress_output_) {
    fprintf(stdout, "\n%s End of the tuning process\n\n", kMessageFull.c_str());
  }
}

// =================================================================================================

// Starts the tuning process. First, the reference kernel is run if it exists (output results are
// automatically verified with respect to this reference run). Next, all permutations of all tuning-
// parameters are computed for each kernel and those kernels are run. Their timing-results are
// collected and stored into the tuning_results_ vector.
void TunerImpl::Tune() {

  // Runs the reference kernel if it is defined
  if (has_reference_) {
    PrintHeader("Testing reference "+reference_kernel_->name());
    RunKernel(reference_kernel_->source(), *reference_kernel_, 0, 1);
    StoreReferenceOutput();
  }
  
  // Iterates over all tunable kernels
  for (auto &kernel: kernels_) {
    PrintHeader("Testing kernel "+kernel.name());

    // If there are no tuning parameters, simply run the kernel and store the results
    if (kernel.parameters().size() == 0) {

        // Compiles and runs the kernel
      auto tuning_result = RunKernel(kernel.source(), kernel, 0, 1);
      tuning_result.status = VerifyOutput();

      // Stores the result of the tuning
      tuning_results_.push_back(tuning_result);

    // Else: there are tuning parameters to iterate over
    } else {

      // Computes the permutations of all parameters and pass them to a (smart) search algorithm
      kernel.SetConfigurations();

      // Creates the selected search algorithm
      std::unique_ptr<Searcher> search;
      switch (search_method_) {
        case SearchMethod::FullSearch:
          search.reset(new FullSearch{kernel.configurations()});
          break;
        case SearchMethod::RandomSearch:
          search.reset(new RandomSearch{kernel.configurations(), search_args_[0]});
          break;
        case SearchMethod::Annealing:
          search.reset(new Annealing{kernel.configurations(), search_args_[0], search_args_[1]});
          break;
        case SearchMethod::PSO:
          search.reset(new PSO{kernel.configurations(), kernel.parameters(), search_args_[0],
                               static_cast<size_t>(search_args_[1]), search_args_[2],
                               search_args_[3], search_args_[4]});
          break;
      }

      // Iterates over all possible configurations (the permutations of the tuning parameters)
      for (auto p=0UL; p<search->NumConfigurations(); ++p) {
        auto permutation = search->GetConfiguration();

        // Adds the parameters to the source-code string as defines
        auto source = std::string{};
        for (auto &config: permutation) {
          source += config.GetDefine();
        }
        source += kernel.source();

        // Updates the local range with the parameter values
        kernel.ComputeRanges(permutation);

        // Compiles and runs the kernel
        auto tuning_result = RunKernel(source, kernel, p, search->NumConfigurations());
        tuning_result.status = VerifyOutput();

        // Gives timing feedback to the search algorithm and calculates the next index
        search->PushExecutionTime(tuning_result.time);
        search->CalculateNextIndex();

        // Stores the parameters and the timing-result
        tuning_result.configuration = permutation;
        tuning_results_.push_back(tuning_result);
        if (tuning_result.time == std::numeric_limits<double>::max()) {
          tuning_result.time = 0.0;
          PrintResult(stdout, tuning_result, kMessageFailure);
          tuning_result.time = std::numeric_limits<float>::max();
        }
        else if (!tuning_result.status) {
          PrintResult(stdout, tuning_result, kMessageWarning);
        }
      }

      // Prints a log of the searching process. This is disabled per default, but can be enabled
      // using the "OutputSearchLog" function.
      if (output_search_process_) {
        auto file = fopen(search_log_filename_.c_str(), "w");
        search->PrintLog(file);
        fclose(file);
      }
    }
  }
}

// =================================================================================================

// Compiles the kernel and checks for OpenCL error messages, sets all output buffers to zero,
// launches the kernel, and collects the timing information.
TunerImpl::TunerResult TunerImpl::RunKernel(const std::string &source, const KernelInfo &kernel,
                                            const size_t configuration_id,
                                            const size_t num_configurations) {

  // Note: the following code is disabled because of GCC 4.8.0 compatibility
  auto processed_source = source;
  /*
  // Removes the use of C++11 string literals (if any) from the kernel source code
  auto string_literal_start = std::regex{"R\"\\("};
  auto string_literal_end = std::regex{"\\)\";"};
  auto processed_source = std::regex_replace(source, string_literal_start, "");
  processed_source = std::regex_replace(processed_source, string_literal_end, "");
  */

  // In case of an exception, skip this run
  try {

    // Compiles the kernel and prints the compiler errors/warnings
    auto program = Program(context_, processed_source);
    auto options = std::vector<std::string>{};
    auto build_status = program.Build(device_, options);
    if (build_status == BuildStatus::kError) {
      auto message = program.GetBuildInfo(device_);
      fprintf(stdout, "OpenCL compiler error/warning: %s\n", message.c_str());
      throw std::runtime_error("OpenCL compiler error/warning occurred ^^\n");
    }
    if (build_status == BuildStatus::kInvalid) {
      throw std::runtime_error("Invalid program binary");
    }

    // Sets the output buffer(s) to zero
    for (auto &output: arguments_output_) {
      switch (output.type) {
        case MemType::kInt: ResetMemArgument<int>(output); break;
        case MemType::kSizeT: ResetMemArgument<size_t>(output); break;
        case MemType::kFloat: ResetMemArgument<float>(output); break;
        case MemType::kDouble: ResetMemArgument<double>(output); break;
        case MemType::kFloat2: ResetMemArgument<float2>(output); break;
        case MemType::kDouble2: ResetMemArgument<double2>(output); break;
        default: throw std::runtime_error("Unsupported reference output data-type");
      }
    }

    // Sets the kernel and its arguments
    auto tune_kernel = Kernel(program, kernel.name());
    for (auto &i: arguments_input_) { tune_kernel.SetArgument(i.index, i.buffer()); }
    for (auto &i: arguments_output_) { tune_kernel.SetArgument(i.index, i.buffer()); }
    for (auto &i: arguments_int_) { tune_kernel.SetArgument(i.first, i.second); }
    for (auto &i: arguments_size_t_) { tune_kernel.SetArgument(i.first, i.second); }
    for (auto &i: arguments_float_) { tune_kernel.SetArgument(i.first, i.second); }
    for (auto &i: arguments_double_) { tune_kernel.SetArgument(i.first, i.second); }
    for (auto &i: arguments_float2_) { tune_kernel.SetArgument(i.first, i.second); }
    for (auto &i: arguments_double2_) { tune_kernel.SetArgument(i.first, i.second); }

    // Sets the global and local thread-sizes
    auto global = kernel.global();
    auto local = kernel.local();

    // Verifies the local memory usage of the kernel
    auto local_mem_usage = tune_kernel.LocalMemUsage(device_);
    if (!device_.IsLocalMemoryValid(local_mem_usage)) {
      throw std::runtime_error("Using too much local memory");
    }

    // Prepares the kernel
    queue_.Finish();

    // Runs the kernel (this is the timed part)
    fprintf(stdout, "%s Running %s\n", kMessageRun.c_str(), kernel.name().c_str());
    auto events = std::vector<Event>(kNumRuns);
    for (auto t=size_t{0}; t<kNumRuns; ++t) {
      tune_kernel.Launch(queue_, global, local, events[t]);
      queue_.Finish(events[t]);
    }
    queue_.Finish();

    // Collects the timing information
    auto elapsed_time = std::numeric_limits<float>::max();
    for (auto t=size_t{0}; t<kNumRuns; ++t) {
      auto this_elapsed_time = events[t].GetElapsedTime();
      elapsed_time = std::min(elapsed_time, this_elapsed_time);
    }

    // Prints diagnostic information
    fprintf(stdout, "%s Completed %s (%.0lf ms) - %zu out of %zu\n",
            kMessageOK.c_str(), kernel.name().c_str(), elapsed_time,
            configuration_id+1, num_configurations);

    // Computes the result of the tuning
    auto local_threads = size_t{1};
    for (auto &item: local) { local_threads *= item; }
    TunerResult result = {kernel.name(), elapsed_time, local_threads, false, {}};
    return result;
  }

  // There was an exception, now return an invalid tuner results
  catch(std::exception& e) {
    fprintf(stdout, "%s Kernel %s failed\n", kMessageFailure.c_str(), kernel.name().c_str());
    fprintf(stdout, "%s   catched exception: %s\n", kMessageFailure.c_str(), e.what());
    TunerResult result = {kernel.name(), std::numeric_limits<float>::max(), 0, false, {}};
    return result;
  }
}

// =================================================================================================

// Creates a new array of zeroes and copies it to the target OpenCL buffer
template <typename T> 
void TunerImpl::ResetMemArgument(MemArgument &argument) {

  // Create an array with zeroes
  std::vector<T> buffer(argument.size, T{0});

  // Copy the new array to the OpenCL buffer on the device
  auto bytes = sizeof(T)*argument.size;
  argument.buffer.Write(queue_, bytes, buffer);
}

// =================================================================================================

// Loops over all reference outputs, creates per output a new host buffer and copies the OpenCL
// buffer from the device onto the host. This function is specialised for different data-types.
void TunerImpl::StoreReferenceOutput() {
  reference_outputs_.clear();
  for (auto &output_buffer: arguments_output_) {
    switch (output_buffer.type) {
      case MemType::kInt: DownloadReference<int>(output_buffer); break;
      case MemType::kSizeT: DownloadReference<size_t>(output_buffer); break;
      case MemType::kFloat: DownloadReference<float>(output_buffer); break;
      case MemType::kDouble: DownloadReference<double>(output_buffer); break;
      case MemType::kFloat2: DownloadReference<float2>(output_buffer); break;
      case MemType::kDouble2: DownloadReference<double2>(output_buffer); break;
      default: throw std::runtime_error("Unsupported reference output data-type");
    }
  }
}
template <typename T> void TunerImpl::DownloadReference(MemArgument &device_buffer) {
  auto host_buffer = new T[device_buffer.size];
  auto bytes = sizeof(T)*device_buffer.size;
  device_buffer.buffer.Read(queue_, bytes, host_buffer);
  reference_outputs_.push_back(host_buffer);
}

// =================================================================================================

// In case there is a reference kernel, this function loops over all outputs, creates per output a
// new host buffer and copies the OpenCL buffer from the device onto the host. Following, it
// compares the results to the reference output. This function is specialised for different
// data-types. These functions return "true" if everything is OK, and "false" if there is a warning.
bool TunerImpl::VerifyOutput() {
  auto status = true;
  if (has_reference_) {
    auto i = size_t{0};
    for (auto &output_buffer: arguments_output_) {
      switch (output_buffer.type) {
        case MemType::kInt: status &= DownloadAndCompare<int>(output_buffer, i); break;
        case MemType::kSizeT: status &= DownloadAndCompare<size_t>(output_buffer, i); break;
        case MemType::kFloat: status &= DownloadAndCompare<float>(output_buffer, i); break;
        case MemType::kDouble: status &= DownloadAndCompare<double>(output_buffer, i); break;
        case MemType::kFloat2: status &= DownloadAndCompare<float2>(output_buffer, i); break;
        case MemType::kDouble2: status &= DownloadAndCompare<double2>(output_buffer, i); break;
        default: throw std::runtime_error("Unsupported output data-type");
      }
      ++i;
    }
  }
  return status;
}

// See above comment
template <typename T>
bool TunerImpl::DownloadAndCompare(MemArgument &device_buffer, const size_t i) {
  auto l2_norm = 0.0;

  // Downloads the results to the host
  std::vector<T> host_buffer(device_buffer.size);
  auto bytes = sizeof(T)*device_buffer.size;
  device_buffer.buffer.Read(queue_, bytes, host_buffer);

  // Compares the results (L2 norm)
  T* reference_output = static_cast<T*>(reference_outputs_[i]);
  for (auto j=0UL; j<device_buffer.size; ++j) {
    l2_norm += AbsoluteDifference(reference_output[j], host_buffer[j]);
  }

  // Verifies if everything was OK, if not: print the L2 norm
  // TODO: Implement a choice of comparisons for the client to choose from
  if (std::isnan(l2_norm) || l2_norm > kMaxL2Norm) {
    fprintf(stderr, "%s Results differ: L2 norm is %6.2e\n", kMessageWarning.c_str(), l2_norm);
    return false;
  }
  return true;
}

// Computes the absolute difference
template <typename T>
double TunerImpl::AbsoluteDifference(const T reference, const T result) {
  return fabs(static_cast<double>(reference) - static_cast<double>(result));
}
template <> double TunerImpl::AbsoluteDifference(const float2 reference, const float2 result) {
  auto real = fabs(static_cast<double>(reference.real()) - static_cast<double>(result.real()));
  auto imag = fabs(static_cast<double>(reference.imag()) - static_cast<double>(result.imag()));
  return real + imag;
}
template <> double TunerImpl::AbsoluteDifference(const double2 reference, const double2 result) {
  auto real = fabs(reference.real() - result.real());
  auto imag = fabs(reference.imag() - result.imag());
  return real + imag;
}

// =================================================================================================

// Trains a model and predicts all remaining configurations
void TunerImpl::ModelPrediction(const Model model_type, const float validation_fraction,
                                const size_t test_top_x_configurations) {

  // Iterates over all tunable kernels
  for (auto &kernel: kernels_) {

    // Retrieves the number of training samples and features
    auto validation_fraction = 0.20f; // 20%
    auto validation_samples = static_cast<size_t>(tuning_results_.size()*validation_fraction);
    auto training_samples = tuning_results_.size() - validation_samples;
    auto features = tuning_results_[0].configuration.size();

    // Sets the raw training and validation data
    auto x_train = std::vector<std::vector<float>>(training_samples, std::vector<float>(features));
    auto y_train = std::vector<float>(training_samples);
    for (auto s=size_t{0}; s<training_samples; ++s) {
      y_train[s] = tuning_results_[s].time;
      for (auto f=size_t{0}; f<features; ++f) {
        x_train[s][f] = static_cast<float>(tuning_results_[s].configuration[f].value);
      }
    }
    auto x_validation = std::vector<std::vector<float>>(validation_samples, std::vector<float>(features));
    auto y_validation = std::vector<float>(validation_samples);
    for (auto s=size_t{0}; s<validation_samples; ++s) {
      y_validation[s] = tuning_results_[s+training_samples].time;
      for (auto f=size_t{0}; f<features; ++f) {
        x_validation[s][f] = static_cast<float>(tuning_results_[s + training_samples].configuration[f].value);
      }
    }

    // Pointer to one of the machine learning models
    std::unique_ptr<MLModel<float>> model;

    // Sets the learning parameters
    auto learning_iterations = 800UL; // For gradient descent
    auto learning_rate = 0.05f; // For gradient descent
    auto lambda = 0.5f; // Regularization parameter
    auto debug_display = true; // Output learned data to stdout

    // Trains a linear regression model
    if (model_type == Model::kLinearRegression) {
      PrintHeader("Training a linear regression model");
      model = std::unique_ptr<MLModel<float>>(
        new LinearRegression<float>(learning_iterations, learning_rate, lambda, debug_display)
      );
      model->Train(x_train, y_train);
      model->Validate(x_validation, y_validation);
    }

    // Unknown model
    else {
      throw std::runtime_error("Unknown machine learning model");
    }

    // Iterates over all configurations (the permutations of the tuning parameters)
    PrintHeader("Predicting the remaining configurations using the model");
    auto model_results = std::vector<std::tuple<size_t,float>>();
    auto p = size_t{0};
    for (auto &permutation: kernel.configurations()) {

      // Runs the trained model to predicts the result
      auto x_test = std::vector<float>();
      for (auto &setting: permutation) {
        x_test.push_back(static_cast<float>(setting.value));
      }
      auto predicted_time = model->Predict(x_test);
      model_results.push_back(std::make_tuple(p, predicted_time));
      ++p;
    }

    // Sorts the modelled results by performance
    std::sort(begin(model_results), end(model_results),
      [](const std::tuple<size_t,float> &t1, const std::tuple<size_t,float> &t2) {
        return std::get<1>(t1) < std::get<1>(t2);
      }
    );

    // Tests the best configurations on the device to verify the results
    PrintHeader("Testing the best-found configurations");
    for (auto i=size_t{0}; i<test_top_x_configurations && i<model_results.size(); ++i) {
      auto result = model_results[i];
      printf("[ -------> ] The model predicted: %.3lf ms\n", std::get<1>(result));
      auto p = std::get<0>(result);
      auto permutations = kernel.configurations();
      auto permutation = permutations[p];

      // Adds the parameters to the source-code string as defines
      auto source = std::string{};
      for (auto &config: permutation) {
        source += config.GetDefine();
      }
      source += kernel.source();

      // Updates the local range with the parameter values
      kernel.ComputeRanges(permutation);

      // Compiles and runs the kernel
      auto tuning_result = RunKernel(source, kernel, p, test_top_x_configurations);
      tuning_result.status = VerifyOutput();

      // Stores the parameters and the timing-result
      tuning_result.configuration = permutation;
      tuning_results_.push_back(tuning_result);
      if (tuning_result.time == std::numeric_limits<double>::max()) {
        tuning_result.time = 0.0;
        PrintResult(stdout, tuning_result, kMessageFailure);
        tuning_result.time = std::numeric_limits<float>::max();
      }
      else if (!tuning_result.status) {
        PrintResult(stdout, tuning_result, kMessageWarning);
      }
    }
  }
}

// =================================================================================================

// Prints a result by looping over all its configuration parameters
void TunerImpl::PrintResult(FILE* fp, const TunerResult &result, const std::string &message) const {
  fprintf(fp, "%s %s; ", message.c_str(), result.kernel_name.c_str());
  fprintf(fp, "%6.0lf ms;", result.time);
  for (auto &setting: result.configuration) {
    fprintf(fp, "%9s;", setting.GetConfig().c_str());
  }
  fprintf(fp, "\n");
}

// =================================================================================================

// Loads a file into a stringstream and returns the result as a string
std::string TunerImpl::LoadFile(const std::string &filename) {
  std::ifstream file(filename);
  if (file.fail()) { throw std::runtime_error("Could not open kernel file: "+filename); }
  std::stringstream file_contents;
  file_contents << file.rdbuf();
  return file_contents.str();
}

// =================================================================================================

// Converts a C++ string to a C string and print it out with nice formatting
void TunerImpl::PrintHeader(const std::string &header_name) const {
  if (!suppress_output_) {
    fprintf(stdout, "\n%s %s\n", kMessageHead.c_str(), header_name.c_str());
  }
}

// =================================================================================================

// Get the MemType based on a template argument
template <> MemType TunerImpl::GetType<int>() { return MemType::kInt; }
template <> MemType TunerImpl::GetType<size_t>() { return MemType::kSizeT; }
template <> MemType TunerImpl::GetType<float>() { return MemType::kFloat; }
template <> MemType TunerImpl::GetType<double>() { return MemType::kDouble; }
template <> MemType TunerImpl::GetType<float2>() { return MemType::kFloat2; }
template <> MemType TunerImpl::GetType<double2>() { return MemType::kDouble2; }

// =================================================================================================
} // namespace cltune
