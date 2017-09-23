// Copyright (c) 2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_SHARED_OBJ_H_
#define BESS_SHARED_OBJ_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include "utils/common.h"  // for PairHasher functor

namespace bess {

// SharedObjectSpace provides a simple mechanism for independent modules to
// share arbitrary objects. There is a global "shared_objects" instance in the
// "bess" namespace (see shared_obj.cc). Modules (or port drivers) can use the
// global instance to create and access shared objects by name. Shared objects
// are instances of an arbitrary class T, referenced by a shared_ptr<T>. Just
// like any other shared_ptr objects, shared objects are automatically
// destructed once all references to the object have gone.
//
// Usage:
// shared_ptr<TypeFoo> bess::shared_objects::Get<TypeFoo>("foo_name");
//
// If there is no object named "foo_name", a new instance of TypeFoo is created
// (of course with its constructor). The module is expected to keep the
// shared_ptr to the object until it is no longer needed.
//
// Type safety: The type of an object must be identical for all users of the
// object. To prevent type errors, this class provides a separate namespace
// for each object type. For example, if another module requests an object
// also named "foo_name" but with a different type other than TypeFoo, a
// different object will be returned.
//
// Thread safety: Get() and Lookup() functions are thread safe as they are
// protected by a mutex. However, shared objects themselves are not protected
// by default; you should use any synchronization mechanism for objects as
// necessary.
class SharedObjectSpace {
 public:
  // By default, the Get() method creates a new object with its default
  // constructor (a constructor without any arguments). To change this behavior,
  // you can use the optional argument "creator", a callable object
  // (function pointer, functor, lambda function, etc.) that creates the object
  // in a way you'd like, e.g., to create an object with a non-default
  // constructor, or to reuse an already existing object.
  //
  // NOTE: the creator function will be called with the global mutex being held,
  //       so it's not a good idea for the creator to block on something.
  template <typename T>
  std::shared_ptr<T> Get(
      const std::string &name,
      std::function<std::shared_ptr<T>()> creator = DefaultConstructor<T>) {
    SharedObjectKey key = std::make_pair(std::type_index(typeid(T)), name);

    static std::recursive_mutex mutex;
    std::lock_guard<decltype(mutex)> lock(mutex);

    auto it = obj_map_.find(key);
    if (it != obj_map_.end()) {
      // Found, but check if the weak pointer has become stale.
      if (auto ret = it->second.lock()) {
        // Fresh. Convert from shared_ptr<void> to shared_ptr<T> and return
        return std::static_pointer_cast<T>(ret);
      }
    }

    // "creator" returns an empty shared_ptr if object allocation
    // failed or no object should be newly made.
    std::shared_ptr<T> new_object = creator();
    if (new_object) {
      obj_map_[key] = std::weak_ptr<T>(new_object);
    }

    return new_object;
  }

  // If no object with the specified type&name is found, this method does not
  // create a new one.
  template <typename T>
  std::shared_ptr<T> Lookup(const std::string &name) {
    return Get<T>(name, LookupOnly<T>);
  }

  template <typename T>
  static std::shared_ptr<T> DefaultConstructor() {
    return std::shared_ptr<T>(new T());
  }

 private:
  using SharedObjectKey = std::pair<std::type_index, std::string>;

  // Used by Lookup()
  template <typename T>
  static std::shared_ptr<T> LookupOnly() {
    return std::shared_ptr<T>();
  }

  std::unordered_map<SharedObjectKey, std::weak_ptr<void>, PairHasher> obj_map_;
};

extern SharedObjectSpace shared_objects;

}  // namespace bess

#endif  // BESS_SHARED_OBJ_H_
