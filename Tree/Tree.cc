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

#include <cassert>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Tree/Tree.h"

namespace LogCabin {
namespace Tree {

using Core::StringUtil::format;
using namespace Internal; // NOLINT

////////// enum Status //////////

std::ostream&
operator<<(std::ostream& os, Status status)
{
    switch (status) {
        case Status::OK:
            os << "Status::OK";
            break;
        case Status::INVALID_ARGUMENT:
            os << "Status::INVALID_ARGUMENT";
            break;
        case Status::LOOKUP_ERROR:
            os << "Status::LOOKUP_ERROR";
            break;
        case Status::TYPE_ERROR:
            os << "Status::TYPE_ERROR";
            break;
    }
    return os;
}

////////// struct Result //////////

Result::Result()
    : status(Status::OK)
    , error()
{
}

namespace Internal {

////////// class File //////////

File::File()
    : contents()
{
}

////////// class Directory //////////

Directory::Directory()
    : directories()
    , files()
{
}

std::vector<std::string>
Directory::getChildren() const
{
    std::vector<std::string> children;
    for (auto it = directories.begin(); it != directories.end(); ++it)
        children.push_back(it->first + "/");
    for (auto it = files.begin(); it != files.end(); ++it)
        children.push_back(it->first);
    return children;
}

Directory*
Directory::lookupDirectory(const std::string& name)
{
    return const_cast<Directory*>(
        const_cast<const Directory*>(this)->lookupDirectory(name));
}

const Directory*
Directory::lookupDirectory(const std::string& name) const
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    auto it = directories.find(name);
    if (it == directories.end())
        return NULL;
    return &it->second;
}


Directory*
Directory::makeDirectory(const std::string& name)
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    if (lookupFile(name) != NULL)
        return NULL;
    return &directories[name];
}

void
Directory::removeDirectory(const std::string& name)
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    directories.erase(name);
}

File*
Directory::lookupFile(const std::string& name)
{
    return const_cast<File*>(
        const_cast<const Directory*>(this)->lookupFile(name));
}

const File*
Directory::lookupFile(const std::string& name) const
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    auto it = files.find(name);
    if (it == files.end())
        return NULL;
    return &it->second;
}

File*
Directory::makeFile(const std::string& name)
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    if (lookupDirectory(name) != NULL)
        return NULL;
    return &files[name];
}

void
Directory::removeFile(const std::string& name)
{
    assert(!name.empty());
    assert(!Core::StringUtil::endsWith(name, "/"));
    files.erase(name);
}

////////// class Path //////////

Path::Path(const std::string& symbolic)
    : result()
    , symbolic(symbolic)
    , parents()
    , target()
{
    if (!Core::StringUtil::startsWith(symbolic, "/")) {
        result.status = Status::INVALID_ARGUMENT;
        result.error = format("'%s' is not a valid path",
                              symbolic.c_str());
        return;
    }

    // Add /root prefix (see docs for Tree::superRoot)
    parents.push_back("root");

    // Split the path into a list of parent components and a target.
    std::string word;
    for (auto it = symbolic.begin(); it != symbolic.end(); ++it) {
        if (*it == '/') {
            if (!word.empty()) {
                parents.push_back(word);
                word.clear();
            }
        } else {
            word += *it;
        }
    }
    if (!word.empty())
        parents.push_back(word);
    target = parents.back();
    parents.pop_back();
}

std::string
Path::parentsThrough(std::vector<std::string>::const_iterator end) const
{
    auto it = parents.begin();
    ++it; // skip "root"
    ++end; // end was inclusive, now exclusive
    if (it == end)
        return "/";
    std::string ret;
    do {
        ret += "/" + *it;
        ++it;
    } while (it != end);
    return ret;
}

} // LogCabin::Tree::Internal

////////// class Tree //////////

Tree::Tree()
    : superRoot()
{
    // Create the root directory so that users don't have to explicitly
    // call makeDirectory("/").
    superRoot.makeDirectory("root");
}

Result
Tree::normalLookup(const Path& path, Directory** parent)
{
    return normalLookup(path,
                        const_cast<const Directory**>(parent));
}

Result
Tree::normalLookup(const Path& path, const Directory** parent) const
{
    *parent = NULL;
    Result result;
    const Directory* current = &superRoot;
    for (auto it = path.parents.begin(); it != path.parents.end(); ++it) {
        const Directory* next = current->lookupDirectory(*it);
        if (next == NULL) {
            if (current->lookupFile(*it) == NULL) {
                result.status = Status::LOOKUP_ERROR;
                result.error = format("Parent %s of %s does not exist",
                                      path.parentsThrough(it).c_str(),
                                      path.symbolic.c_str());
            } else {
                result.status = Status::TYPE_ERROR;
                result.error = format("Parent %s of %s is a file",
                                      path.parentsThrough(it).c_str(),
                                      path.symbolic.c_str());
            }
            return result;
        }
        current = next;
    }
    *parent = current;
    return result;
}

Result
Tree::mkdirLookup(const Path& path, Directory** parent)
{
    *parent = NULL;
    Result result;
    Directory* current = &superRoot;
    for (auto it = path.parents.begin(); it != path.parents.end(); ++it) {
        Directory* next = current->makeDirectory(*it);
        if (next == NULL) {
            result.status = Status::TYPE_ERROR;
            result.error = format("Parent %s of %s is a file",
                                  path.parentsThrough(it).c_str(),
                                  path.symbolic.c_str());
            return result;
        }
        current = next;
    }
    *parent = current;
    return result;
}

Result
Tree::makeDirectory(const std::string& symbolicPath)
{
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    Directory* parent;
    Result result = mkdirLookup(path, &parent);
    if (result.status != Status::OK)
        return result;
    if (parent->makeDirectory(path.target) == NULL) {
        result.status = Status::TYPE_ERROR;
        result.error = format("%s already exists but is a file",
                              path.symbolic.c_str());
        return result;
    }
    return result;
}

Result
Tree::listDirectory(const std::string& symbolicPath,
                    std::vector<std::string>& children) const
{
    children.clear();
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    const Directory* parent;
    Result result = normalLookup(path, &parent);
    if (result.status != Status::OK)
        return result;
    const Directory* targetDir = parent->lookupDirectory(path.target);
    if (targetDir == NULL) {
        if (parent->lookupFile(path.target) == NULL) {
            result.status = Status::LOOKUP_ERROR;
            result.error = format("%s does not exist",
                                  path.symbolic.c_str());
        } else {
            result.status = Status::TYPE_ERROR;
            result.error = format("%s is a file",
                                  path.symbolic.c_str());
        }
        return result;
    }
    children = targetDir->getChildren();
    return result;
}

Result
Tree::removeDirectory(const std::string& symbolicPath)
{
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    Directory* parent;
    Result result = normalLookup(path, &parent);
    if (result.status == Status::LOOKUP_ERROR) {
        // no parent, already done
        return Result();
    }
    if (result.status != Status::OK)
        return result;
    Directory* targetDir = parent->lookupDirectory(path.target);
    if (targetDir == NULL) {
        if (parent->lookupFile(path.target)) {
            result.status = Status::TYPE_ERROR;
            result.error = format("%s is a file",
                                  path.symbolic.c_str());
            return result;
        } else {
            // target does not exist, already done
            return result;
        }
    }
    parent->removeDirectory(path.target);
    if (parent == &superRoot) { // removeDirectory("/")
        // If the caller is trying to remove the root directory, we remove the
        // contents but not the directory itself. The easiest way to do this
        // is to drop but then recreate the directory.
        parent->makeDirectory(path.target);
    }
    return result;
}

Result
Tree::write(const std::string& symbolicPath, const std::string& contents)
{
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    Directory* parent;
    Result result = normalLookup(path, &parent);
    if (result.status != Status::OK)
        return result;
    File* targetFile = parent->makeFile(path.target);
    if (targetFile == NULL) {
        result.status = Status::TYPE_ERROR;
        result.error = format("%s is a directory",
                              path.symbolic.c_str());
        return result;
    }
    targetFile->contents = contents;
    return result;
}

Result
Tree::read(const std::string& symbolicPath, std::string& contents) const
{
    contents.clear();
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    const Directory* parent;
    Result result = normalLookup(path, &parent);
    if (result.status != Status::OK)
        return result;
    const File* targetFile = parent->lookupFile(path.target);
    if (targetFile == NULL) {
        if (parent->lookupDirectory(path.target) != NULL) {
            result.status = Status::TYPE_ERROR;
            result.error = format("%s is a directory",
                                  path.symbolic.c_str());
        } else {
            result.status = Status::LOOKUP_ERROR;
            result.error = format("%s does not exist",
                                  path.symbolic.c_str());
        }
        return result;
    }
    contents = targetFile->contents;
    return result;
}

Result
Tree::removeFile(const std::string& symbolicPath)
{
    Path path(symbolicPath);
    if (path.result.status != Status::OK)
        return path.result;
    Directory* parent;
    Result result = normalLookup(path, &parent);
    if (result.status == Status::LOOKUP_ERROR) {
        // no parent, already done
        return Result();
    }
    if (result.status != Status::OK)
        return result;
    if (parent->lookupDirectory(path.target) != NULL) {
        result.status = Status::TYPE_ERROR;
        result.error = format("%s is a directory",
                              path.symbolic.c_str());
        return result;
    }
    parent->removeFile(path.target);
    return result;
}

} // namespace LogCabin::Tree
} // namespace LogCabin
