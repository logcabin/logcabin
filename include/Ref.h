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
 *
 * To set up a class MyClass to be reference counted:
 * 1. The first member of MyClass should be:
 *      RefHelper<MyClass>::RefCount refCount;
 *    Placing this member early in the class declaration allows the constructor
 *    to hand out Refs to this object right away.
 * 2. The refCount member should be default-initialized.
 * 3. The constructor should be private.
 * 4. The class declaration should include:
 *      friend class DLog::MakeHelper;
 *      friend class DLog::RefHelper<MyClass>;
 *
 * It is illegal for a constructor to hand out Refs to this object and /then/
 * throw an exception. The assert in ~DefaultRefCountWrapper attempts to catch
 * this sort of bug.
 *
 * Every instance of the class must now be created as follows:
 *      Ref<MyClass> newInstance = DLog::make<MyClass>(...constructor args...);
 * The constructor arguments will be forwarded to your private constructor(s).
 * Using the constructor(s) directly is illegal.
 */

#include "Common.h"

#ifndef REF_H // TODO(ongaro): this needs a prefix
#define REF_H

namespace DLog {

/**
 * A small object that holds a reference count.
 * This class's primary purpose is to ensure that the reference count is
 * initialized to the correct value.
 * \param Numeric
 *      The underlying integer type to use.
 */
template<typename Numeric>
class DefaultRefCountWrapper {
  public:
    DefaultRefCountWrapper()
        : value(1) {
        assertMakeInProgress();
    }
    ~DefaultRefCountWrapper() {
        // 'value' is usually 0. It can be 1 if the object's constructor threw
        // an exception. If it's higher than that, the object's constructor
        // threw an exception after handing out Refs to itself, which will lead
        // to memory corruption. It's safer to kill the program.
        assert(value <= 1);
    }
    Numeric inc() { return ++value; }
    Numeric dec() { return --value; }
    // get is here for testing purposes
    Numeric get() { return value; }
  private:
    static void assertMakeInProgress(); // defined below due to circular dep
    Numeric value;
};

/**
 * This class is used by Ref and Ptr to increment and decrement reference
 * counts and to destroy the object.
 *
 * This default implementation will be sufficient for most classes. You can
 * specialize this template for your class if it needs to be destroyed
 * differently or it has a different reference count type.
 */
template<typename T>
class RefHelper {
  public:
    typedef DefaultRefCountWrapper<uint32_t> RefCount;
    static void incRefCount(T* obj) {
        obj->refCount.inc();
    }
    static void decRefCountAndDestroy(T* obj) {
        if (obj->refCount.dec() == 0)
            delete obj;
    }
};

/**
 * A reference to an object that may not be NULL.
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
    Ptr(const Ref<T>& other) // NOLINT
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
    operator bool() const {
        return ptr != NULL;
    }

  private:
    T* ptr;
};

// Equality comparisons for Ptr and Ref.

// Ref vs Ref
template<typename T>
bool operator==(const Ref<T>& a, const Ref<T>& b) {
    return a.get() == b.get();
}
template<typename T>
bool operator!=(const Ref<T>& a, const Ref<T>& b) {
    return a.get() != b.get();
}
// Ptr vs Ptr
template<typename T>
bool operator==(const Ptr<T>& a, const Ptr<T>& b) {
    return a.get() == b.get();
}
template<typename T>
bool operator!=(const Ptr<T>& a, const Ptr<T>& b) {
    return a.get() != b.get();
}
// Ref vs Ptr
template<typename T>
bool operator==(const Ref<T>& a, const Ptr<T>& b) {
    return a.get() == b.get();
}
template<typename T>
bool operator!=(const Ref<T>& a, const Ptr<T>& b) {
    return a.get() != b.get();
}
// Ptr vs Ref
template<typename T>
bool operator==(const Ptr<T>& a, const Ref<T>& b) {
    return a.get() == b.get();
}
template<typename T>
bool operator!=(const Ptr<T>& a, const Ref<T>& b) {
    return a.get() != b.get();
}

/**
 * This class is a container for the implementation of make().
 * It exists because it's more convenient to friend than a template.
 */
class MakeHelper {
  public:
    template<typename T, typename... Args>
    Ref<T>
    static make(Args&&... args) {
#if DEBUG
        ++(getMakesInProgress());
#endif
        Ref<T> obj(*new T(static_cast<Args&&>(args)...));
        // Ref counts start at 1 so that objects can create Refs to themselves
        // in their constructors. We decrement the ref count here after
        // creating a reference to it so that the object is not leaked.
        RefHelper<T>::decRefCountAndDestroy(obj.get());
#if DEBUG
        --(getMakesInProgress());
#endif
        return obj;
    }
#if DEBUG
    static uint32_t& getMakesInProgress() {
        // TODO(ongaro): This should be thread-local.
        static uint32_t makesInProgress = 0;
        return makesInProgress;
    }
#endif
};

/**
 * Construct an object of a reference-counted class.
 * Use this as follows:
 *      Ref<MyClass> newInstance = DLog::make<MyClass>(...constructor args...);
 * The constructor arguments will be forwarded to the constructor(s) of T.
 */
template<typename T, typename... Args>
Ref<T>
make(Args&&... args)
{
    return MakeHelper::make<T>(static_cast<Args&&>(args)...);
}

template<typename Numeric>
void
DefaultRefCountWrapper<Numeric>::assertMakeInProgress()
{
    // This check tries to make sure you're constructing a
    // reference-counted object with make() instead of using the
    // Constructor directly. Some illegal uses may get by, but this should
    // catch most of them.
    assert(MakeHelper::getMakesInProgress() > 0);
}

} // namespace DLog

#endif /* REF_H */
