/* Copyright (c) 2012 Stanford University
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

#include <cinttypes>
#include <condition_variable>
#include <memory>
#include <mutex>

#ifndef LOGCABIN_CORE_RWPTR_H
#define LOGCABIN_CORE_RWPTR_H

namespace LogCabin {
namespace Core {

// forward declaration
template<typename T>
class RWPtr;

/**
 * This controls the concurrent access to and lifetime of an object. You can
 * think of this as combining a readers-writer lock (also known as a
 * shared-exclusive lock) with a smart pointer.
 *
 * Sometimes you want to wrap a readers-writer lock around a class to protect
 * its internals; then in order to destroy that class, you need exclusive
 * access to it. You could place a readers-writers lock next to a pointer to
 * the object, and ask people to hold the lock while they access the object.
 * However, that's error-prone and makes it hard to return the object across
 * module boundaries safely. This class combines the lock and the pointer; to
 * access the object, this returns you a smart pointer that will unlock
 * automatically when you're done with it.
 *
 * \warning
 *      This implementation gives full priority to exclusive accesses.
 *      Thus, exclusive accesses can starve shared accesses.
 *
 * \warning
 *      This type of lock may not be used recursively.
 *
 * \tparam T
 *      The type of the managed object.
 */
template<typename T>
class RWManager {
  public:

    /**
     * Constructor.
     * \param ptr
     *      An object to manage (defaults to NULL).
     *      This class takes ownership of the object and will delete it later
     *      (using the 'delete' keyword).
     */
    explicit RWManager(T* ptr = NULL)
        : mutex()
        , ptr(ptr)
        , numActive(0)
        , sharedRunning(false)
        , exclusiveWaiting(0)
        , sharedProgress()
        , exclusiveProgress()
    {
    }

    /**
     * Destructor. Calls reset().
     */
    ~RWManager() {
        reset();
    }

    /**
     * Acquire a shared lock and return a const pointer to the object.
     * When the returned object is destroyed, this shared lock will be
     * released.
     * \return
     *      A const pointer to the managed object (may be NULL).
     */
    RWPtr<const T> getSharedAccess() {
        std::unique_lock<std::mutex> lockGuard(mutex);
        while (numActive > 0 && (!sharedRunning || exclusiveWaiting > 0))
            sharedProgress.wait(lockGuard);
        if (!ptr) {
            return RWPtr<const T>();
        } else {
            ++numActive;
            sharedRunning = true;
            return RWPtr<const T>(this, ptr.get());
        }
    }

    /**
     * Acquire an exclusive lock and return a mutable pointer to the object.
     * When the returned object is destroyed, this exclusive lock will be
     * released.
     * \return
     *      A pointer to the managed object (may be NULL).
     */
    RWPtr<T> getExclusiveAccess() {
        std::unique_lock<std::mutex> lockGuard(mutex);
        ++exclusiveWaiting;
        while (numActive > 0)
            exclusiveProgress.wait(lockGuard);
        --exclusiveWaiting;
        if (!ptr) {
            return RWPtr<T>();
        } else {
            ++numActive;
            return RWPtr<T>(this, ptr.get());
        }
    }

    /**
     * Destroy the managed object, and optionally start to manage a new object.
     * This method internally acquires exclusive access to change the object
     * managed.
     * \param newPtr
     *      A new object to manage (defaults to NULL).
     *      This class takes ownership of the object and will delete it later
     *      (using the 'delete' keyword).
     */
    void reset(T* newPtr = NULL) {
        // This isn't quite monitor style, because it's much less code to write
        // this in terms of getExclusiveAccess().
        RWPtr<T> w = getExclusiveAccess();
        ptr.reset(newPtr);
    }

  private:
    /**
     * This is called by RWPtr to release its lock (shared or exclusive).
     */
    void done() {
        std::unique_lock<std::mutex> lockGuard(mutex);
        --numActive;
        if (numActive == 0) {
            sharedRunning = false;
            if (exclusiveWaiting > 0)
                exclusiveProgress.notify_one();
            else
                sharedProgress.notify_all();
        }
    }

    /**
     * The mutex protecting all other members of this class.
     * (This class is written in a monitor style.)
     */
    std::mutex mutex;

    /**
     * The managed object, or NULL if there is one.
     */
    std::unique_ptr<T> ptr;

    /**
     * If the lock is unlocked, this is 0. If the lock is owned in exclusive
     * mode, this is 1. If the lock is owned in shared mode, this is the number
     * of RWPtr objects sharing the lock.
     */
    uint32_t numActive;

    /**
     * Set to true if the lock is owned in shared mode, false if it is unlocked
     * or owned in exclusive mode.
     */
    bool sharedRunning;

    /**
     * The number of threads waiting to acquire an exclusive lock.
     * If this is non-zero, new shared locks will wait.
     */
    uint32_t exclusiveWaiting;

    /**
     * The condition variable that acquiring a shared lock waits on.
     * This should always be used with notify_all().
     */
    std::condition_variable sharedProgress;

    /**
     * The condition variable that acquiring an exclusive lock waits on.
     */
    std::condition_variable exclusiveProgress;

    // RWPtr needs to call done(), which is dangerous to expose publicly.
    friend class RWPtr<T>;       // exclusive
    friend class RWPtr<const T>; // shared

    // RWPtr is not copyable.
    RWManager<T>(const RWManager<T>&) = delete;
    RWManager<T>& operator=(const RWManager<T>&) = delete;
};

/**
 * A smart pointer to an object managed by an RWManager.
 * This will automatically release its shared or exclusive lock when it is
 * destroyed.
 * \tparam T
 *      The type of the managed object. This will be const for shared locks and
 *      non-const for exclusive locks.
 */
template<typename T>
class RWPtr {
    /**
     * T with its const-ness removed. This is used to refer to the correct
     * RWManager type (RWmanager<NonConstT>).
     */
    typedef typename std::remove_const<T>::type NonConstT;

    // The RWManager class needs access to this private constructor.
    friend class RWManager<NonConstT>;

    /// Constructor used by RWManager.
    RWPtr(RWManager<NonConstT>* manager, T* ptr)
        : manager(manager)
        , ptr(ptr)
    {
    }

  public:
    /// Default constructor.
    RWPtr()
        : manager(NULL)
        , ptr(NULL)
    {
    }

    /// Move constructor.
    RWPtr(RWPtr<T>&& other) // NOLINT
        : manager(other.manager)
        , ptr(other.ptr)
    {
        other.manager = NULL;
        other.ptr = NULL;
    }

    /// Destructor.
    ~RWPtr() {
        if (manager != NULL)
            manager->done();
    }

    /// Move assignment.
    RWPtr<T>& operator=(RWPtr<T>&& other) {
        if (manager != NULL)
            manager->done();
        manager = other.manager;
        ptr = other.ptr;
        other.manager = NULL;
        other.ptr = NULL;
        return *this;
    }

    /// Dereference operator.
    T& operator*() const { return *ptr; }
    /// Pointer to member operator.
    T* operator->() const { return ptr; }
    /// Return a pointer to the managed object.
    T* get() const { return ptr; }

  private:
    /// The RWManager which owns #ptr and which we must unlock, or NULL.
    RWManager<NonConstT>* manager;

    /// The managed object, or NULL.
    T* ptr;

    // RWPtr is not copyable.
    RWPtr<T>(const RWPtr<T>&) = delete;
    RWPtr<T>& operator=(const RWPtr<T>&) = delete;
};

} // namespace LogCabin::Core
} // namespace LogCabin

#endif /* LOGCABIN_CORE_RWPTR_H */
