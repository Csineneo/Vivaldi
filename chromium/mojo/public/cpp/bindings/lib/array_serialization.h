// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_ARRAY_SERIALIZATION_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_ARRAY_SERIALIZATION_H_

#include <stddef.h>

#include "mojo/public/cpp/bindings/array.h"
#include "mojo/public/cpp/bindings/lib/array_serialization_traits.h"

namespace mojo {

template <typename E>
inline size_t GetSerializedSize_(const Array<E>& input,
                                 internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<Array<E>>::GetSerializedSize(input,
                                                                       context);
}

template <typename E, typename F>
inline void SerializeArray_(
    Array<E> input,
    internal::Buffer* buf,
    internal::Array_Data<F>** output,
    const internal::ArrayValidateParams* validate_params,
    internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<Array<E>>::template Serialize<F>(
      std::move(input), buf, output, validate_params, context);
}

template <typename E, typename F>
inline bool Deserialize_(internal::Array_Data<F>* input,
                         Array<E>* output,
                         internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<Array<E>>::template Deserialize<F>(
      input, output, context);
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_ARRAY_SERIALIZATION_H_
