// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_WTF_ARRAY_SERIALIZATION_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_WTF_ARRAY_SERIALIZATION_H_

#include <stddef.h>

#include "mojo/public/cpp/bindings/lib/array_serialization_traits.h"
#include "mojo/public/cpp/bindings/wtf_array.h"

namespace mojo {

template <typename E>
inline size_t GetSerializedSize_(const WTFArray<E>& input,
                                 internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<WTFArray<E>>::GetSerializedSize(
      input, context);
}

template <typename E, typename F>
inline void SerializeArray_(
    WTFArray<E> input,
    internal::Buffer* buf,
    internal::Array_Data<F>** output,
    const internal::ArrayValidateParams* validate_params,
    internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<WTFArray<E>>::template Serialize<F>(
      std::move(input), buf, output, validate_params, context);
}

template <typename E, typename F>
inline bool Deserialize_(internal::Array_Data<F>* input,
                         WTFArray<E>* output,
                         internal::SerializationContext* context) {
  return internal::ArraySerializationImpl<WTFArray<E>>::template Deserialize<F>(
      input, output, context);
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_WTF_ARRAY_SERIALIZATION_H_
