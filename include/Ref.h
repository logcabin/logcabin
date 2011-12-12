/* Copyright (c) 2011 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * \file
 * Templates for intrusive reference counting.
 */

#include <cstddef>
#include <memory>

#ifndef REF_H // TODO(ongaro): this needs a prefix
#define REF_H

namespace DLog {

/**
 * This class is used by Ref to increment and decrement reference counts and to
 * destroy the object.
 *
 * This default implementation will be sufficient for most classes.
 * It assumes you have an integer member named refCount that you've initialized
 * to zero and have made accessible to RefHelper<T>.
 *
 * You can specialize this template for your class if it needs to be destroyed
 * differently or it has a different integer name or interface.
 */
template<typename T>
class RefHelper {
  public:
    static void incRefCount(T* obj) {
        ++obj->refCount;
    }
    static void decRefCountAndDestroy(T* obj) {
        --obj->refCount;
        if (obj->refCount == 0)
            delete obj;
    }
};

/**
 * A reference to an object.
 */
template<typename T>
class Ref {
  public:
    /**
     * Constructor from raw reference.
     * \param outside
     *      The new object that is now owned by its Refs.
     */
    explicit Ref(T& outside)
        : ptr(&outside) {
        RefHelper<T>::incRefCount(ptr);
    }
    /// Constructor from Ref.
    Ref(const Ref<T>& other) // NOLINT
        : ptr(other.get()) {
        RefHelper<T>::incRefCount(ptr);
    }
    /// Assignment from Ref.
    Ref<T>& operator=(const Ref<T>& other) {
        // Inc other.ptr before dec ptr in case both pointers already point to
        // the same object.
        RefHelper<T>::incRefCount(other.get());
        RefHelper<T>::decRefCountAndDestroy(ptr);
        ptr = other.get();
        return *this;
    }
    /// Destructor.
    ~Ref() {
        RefHelper<T>::decRefCountAndDestroy(ptr);
    }
    T& operator*() const {
        return *ptr;
    }
    T* operator->() const {
        return ptr;
    }
    T* get() const {
        return ptr;
    }
  private:
    T* ptr;
};

/**
 * An optional reference to an object. This may store NULL, so be careful when
 * dereferencing it.
 */
template<typename T>
class Ptr {
  public:
    /**
     * Constructor from raw pointer.
     * \param outside
     *      The new object that is now owned by its Refs.
     */
    explicit Ptr(T* outside = NULL)
        : ptr(outside) {
        if (outside != NULL)
            RefHelper<T>::incRefCount(ptr);
    }
    /// Constructor from Ref.
    explicit Ptr(const Ref<T>& other) // NOLINT
        : ptr(other.get()) {
        RefHelper<T>::incRefCount(ptr);
    }
    /// Constructor from Ptr.
    Ptr(const Ptr<T>& other) // NOLINT
        : ptr(other.get()) {
        if (ptr != NULL)
            RefHelper<T>::incRefCount(ptr);
    }
    /// Assignment from Ref.
    Ptr<T>& operator=(const Ref<T>& other) {
        // Inc other.ptr before dec ptr in case both pointers already point to
        // the same object.
        RefHelper<T>::incRefCount(other.get());
        if (ptr != NULL)
            RefHelper<T>::decRefCountAndDestroy(ptr);
        ptr = other.get();
        return *this;
    }
    /// Assignment from Ptr.
    Ptr<T>& operator=(const Ptr<T>& other) {
        // Inc other.ptr before dec ptr in case both pointers already point to
        // the same object.
        if (other.ptr != NULL)
            RefHelper<T>::incRefCount(other.get());
        if (ptr != NULL)
            RefHelper<T>::decRefCountAndDestroy(ptr);
        ptr = other.get();
        return *this;
    }
    /// Destructor.
    ~Ptr() {
        if (ptr != NULL)
            RefHelper<T>::decRefCountAndDestroy(ptr);
    }
    T& operator*() const {
        return *ptr;
    }
    T* operator->() const {
        return ptr;
    }
    T* get() const {
        return ptr;
    }
    operator bool const() {
        return ptr != NULL;
    }
  private:
    T* ptr;
};

} // namespace DLog

#endif /* REF_H */
