/**
 *    Copyright (C) 2024-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/stdx/unordered_map.h"
#include "mongo/util/tracking/allocator.h"
#include "mongo/util/tracking/context.h"

#include <scoped_allocator>

namespace mongo::tracking {

template <class Key,
          class Value,
          class Hasher = DefaultHasher<Key>,
          class KeyEqual = std::equal_to<Key>>
using unordered_map =
    stdx::unordered_map<Key,
                        Value,
                        Hasher,
                        KeyEqual,
                        std::scoped_allocator_adaptor<Allocator<std::pair<const Key, Value>>>>;

template <class Key,
          class Value,
          class Hasher = DefaultHasher<Key>,
          class KeyEqual = std::equal_to<Key>>
unordered_map<Key, Value, Hasher> make_unordered_map(Context& Context) {
    return unordered_map<Key, Value, Hasher, KeyEqual>(Context.makeAllocator<Value>());
}

}  // namespace mongo::tracking
