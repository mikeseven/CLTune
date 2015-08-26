
// =================================================================================================
// This file is part of the CLTune project, which loosely follows the Google C++ styleguide and uses
// a tab-size of two spaces and a max-width of 100 characters per line.
//
// Author: cedric.nugteren@surfsara.nl (Cedric Nugteren)
//
// This file implements the base MLModel class (see the header for information about the class).
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
#include "internal/ml_models/linear_regression.h"

#include <vector>

namespace cltune {
// =================================================================================================

// Calls the base-class constructor
template <typename T>
LinearRegression<T>::LinearRegression(const size_t m, const size_t n):
  MLModel<T>(m, n) {
}

// =================================================================================================

// Trains the model
template <typename T>
void LinearRegression<T>::Train(const std::vector<std::vector<T>> &x, const std::vector<T> &y) {
  auto x_temp = x;

  // Modifies features to get a better model
  ComputeNormalizations(x_temp);
  NormalizeFeatures(x_temp);
  AddPolynominalFeatures(x_temp, 2); // Second order

  // Runs gradient descent to train the model
  GradientDescent(x_temp, y, 0.05, 800);

  // Verifies the trained results
  auto margin = 0.10f; // 10%
  auto success_rate = Verify(x_temp, y, margin);
  printf("%s Training success rate: %.0lf%% with +/- %.0lf%% margin\n",
         TunerImpl::kMessageResult.c_str(), success_rate, 100.0f*margin);
}

// Validates the model
template <typename T>
void LinearRegression<T>::Validate(const std::vector<std::vector<T>> &x, const std::vector<T> &y) {
  auto x_temp = x;

  // Modifies features according to the training data
  NormalizeFeatures(x_temp);
  AddPolynominalFeatures(x_temp, 2); // Second order

  // Verifies the trained results
  auto margin = 0.10f; // 10%
  auto success_rate = Verify(x_temp, y, margin);
  printf("%s Validation success rate: %.0lf%% with +/- %.0lf%% margin\n",
         TunerImpl::kMessageResult.c_str(), success_rate, 100.0f*margin);
}

// =================================================================================================

// Hypothesis-function: pass a single sample through the model and returns its hypothesis
template <typename T>
T LinearRegression<T>::Hypothesis(const std::vector<T> &x) const {
  auto n = x.size();
  auto hypothesis = static_cast<T>(0);
  for (auto nid=size_t{0}; nid<n; ++nid) {
    hypothesis += theta_[nid] * x[nid];
  }
  return hypothesis;
}

// Cost-function: computes the sum of squared differences
template <typename T>
T LinearRegression<T>::Cost(const size_t m, const size_t n,
                            const std::vector<std::vector<T>> &x, const std::vector<T> &y) const {
  auto cost = static_cast<T>(0);
  for (auto mid=size_t{0}; mid<m; ++mid) {
    auto difference = Hypothesis(x[mid]) - y[mid];
    cost += difference * difference;
  }
  return cost / (static_cast<T>(2) * static_cast<T>(m));
}

// Gradient-function: computes the gradient of the cost-function with respect to a specific feature
template <typename T>
T LinearRegression<T>::Gradient(const size_t m, const size_t n,
                                const std::vector<std::vector<T>> &x, const std::vector<T> &y,
                                const size_t gradient_id) const {
  auto gradient = static_cast<T>(0);
  for (auto mid=size_t{0}; mid<m; ++mid) {
    gradient += (Hypothesis(x[mid]) - y[mid]) * x[mid][gradient_id];
  }
  return gradient;
}

// =================================================================================================

// Compiles the class
template class LinearRegression<float>;

// =================================================================================================
} // namespace cltune