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

#include <ctype.h>
#include <cstring>
#include <algorithm>
#include <sstream>

#include <google/protobuf/message_lite.h>

#include "DLogStorage.h"
#include "Debug.h"

namespace DLog {

namespace Storage {

namespace {

/**
 * Returns true for the ASCII characters that one would want to display in a
 * single line of text.
 */
bool
display(char c)
{
    return (32 <= c && c < 127);
}

} // anonymous namespace

// class Chunk

Ref<Chunk>
Chunk::makeChunk(const void* data, uint32_t length)
{
#if DEBUG
    ++(MakeHelper::getMakesInProgress());
#endif
    Chunk* ptr = new(malloc(sizeof(Chunk) + length)) Chunk(length);
    Ref<Chunk> ref(*ptr);
    // Ref counts start at 1 so that objects can create Refs to themselves
    // in their constructors. We decrement the ref count here after
    // creating a reference to it so that the object is not leaked.
    RefHelper<Chunk>::decRefCountAndDestroy(ref.get());
#if DEBUG
    --(MakeHelper::getMakesInProgress());
#endif
    memcpy(ptr->data, data, length);
    return ref;
}

Ref<Chunk>
Chunk::makeChunk(const ::google::protobuf::MessageLite& message)
{
    uint32_t length = downCast<uint32_t>(message.ByteSize());
#if DEBUG
    ++(MakeHelper::getMakesInProgress());
#endif
    Chunk* ptr = new(malloc(sizeof(Chunk) + length)) Chunk(length);
    Ref<Chunk> ref(*ptr);
    // Ref counts start at 1 so that objects can create Refs to themselves
    // in their constructors. We decrement the ref count here after
    // creating a reference to it so that the object is not leaked.
    RefHelper<Chunk>::decRefCountAndDestroy(ref.get());
#if DEBUG
    --(MakeHelper::getMakesInProgress());
#endif
    if (!message.SerializeToArray(ptr->data, length))
        PANIC("Serializing protocol buffer failed");
    return ref;
}

Chunk::Chunk(uint32_t length)
    : refCount()
    , length(length)
    , data()
{
}

Ref<Chunk> NO_DATA(Chunk::makeChunk(NULL, 0));

// class LogEntry

LogEntry::LogEntry(LogId logId, EntryId entryId,
                   TimeStamp createTime,
                   Ref<Chunk> data,
                   const std::vector<EntryId>& invalidations)
    : logId(logId)
    , entryId(entryId)
    , createTime(createTime)
    , invalidations(invalidations)
    , data(data)
{
}

LogEntry::LogEntry(LogId logId, EntryId entryId,
                   TimeStamp createTime,
                   const std::vector<EntryId>& invalidations)
    : logId(logId)
    , entryId(entryId)
    , createTime(createTime)
    , invalidations(invalidations)
    , data(NO_DATA)
{
}

std::string
LogEntry::toString() const
{
    std::ostringstream s;
    s << "(" << logId << ", " << entryId << ") ";
    if (data == NO_DATA) {
        s << "NODATA";
    } else {
        const char* begin = static_cast<const char*>(data->getData());
        const char* end = begin + data->getLength();
        bool printable = std::all_of(begin, end - 1, display);
        if (printable && begin != end) {
            if (*(end - 1) == '\0')
                --end;
            else if (!display(*(end - 1)))
                printable = false;
        }
        if (printable)
            s << "'" << std::string(begin, end) << "'";
        else
            s << "BINARY";
    }
    if (!invalidations.empty()) {
        s << " [inv ";
        for (uint32_t i = 0; i != invalidations.size(); ++i) {
            s << invalidations.at(i);
            if (i < invalidations.size() - 1)
                s << ", ";
        }
        s << "]";
    }
    return s.str();
}

// class Log

Log::Log(LogId logId)
    : refCount()
    , logId(logId)
    , destructorCompletions()
{
}

Log::~Log()
{
    for (auto it = destructorCompletions.begin();
         it != destructorCompletions.end();
         ++it) {
        (*it)->destructorCallback(logId);
    }
}

void
Log::addDestructorCallback(Ref<DestructorCallback> destructorCompletion)
{
    destructorCompletions.push_back(std::move(destructorCompletion));
}

// class StorageModule

StorageModule::StorageModule()
    : refCount()
{
}

} // namespace DLog::Storage

// class RefHelper<Storage::Chunk>

void
RefHelper<Storage::Chunk>::incRefCount(Storage::Chunk* obj)
{
    obj->refCount.inc();
}

void
RefHelper<Storage::Chunk>::decRefCountAndDestroy(Storage::Chunk* obj)
{
    if (obj->refCount.dec() == 0)
        free(obj);
}

} // namespace DLog
