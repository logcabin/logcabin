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

#include <string.h>
#include <unordered_map>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/cryptlib.h>
#include <cryptopp/crc.h>
#include <cryptopp/adler32.h>
#include <cryptopp/md5.h>
#include <cryptopp/sha.h>
#include <cryptopp/whrlpool.h>
#include <cryptopp/tiger.h>
#include <cryptopp/ripemd.h>

#include "Core/Debug.h"
#include "Core/Checksum.h"
#include "Core/StringUtil.h"
#include "Core/Util.h"

namespace LogCabin {
namespace Core {
namespace Checksum {

using Core::Util::downCast;
using Core::StringUtil::format;

namespace {

/**
 * Different algorithms implement different hash functions.
 */
class Algorithm {
  protected:
    Algorithm()
        : hashFn(NULL)
        , name()
        , outputSize(0)
    {
    }
  public:
    virtual ~Algorithm() {}

    /// A short name for the hash function.
    std::string getName() const { return name; }

    /**
     * Calculate the checksum for some data.
     * \param data
     *      An list of (pointer, length) pairs describing what to checksum.
     * \param[out] result
     *      The result of the hash function will be placed here.
     *      This will be a null-terminated, printable C-string.
     * \return
     *      The number of valid characters in 'output', including the null
     *      terminator. This is guaranteed to be greater than 1.
     */
    uint32_t
    writeChecksum(std::initializer_list<std::pair<const void*, uint64_t>> data,
                  char result[MAX_LENGTH]) {
        // copy name and : to result
        memcpy(result, name.c_str(), name.length());
        result = result + name.length();
        *result = ':';
        ++result;

        // calculate binary digest
        uint32_t binarySize = downCast<uint32_t>(hashFn->DigestSize());
        uint8_t binary[binarySize];

        for (auto it = data.begin(); it != data.end(); ++it) {
            hashFn->Update(static_cast<const uint8_t*>(it->first),
                           it->second);
        }
        hashFn->Final(binary);

        // add hex digest to result
        const char* hexArray = "0123456789abcdef";
        for (uint32_t i = 0; i < binarySize; ++i) {
            *result = hexArray[binary[i] >> 4];
            ++result;
            *result = hexArray[binary[i] & 15];
            ++result;
        }

        // add null terminator to result and return total length
        *result = '\0';
        return outputSize;
    }

  protected:
    // Called by derived class's constructor to initialize this class
    void setHashFn(CryptoPP::HashTransformation& hashFn) {
        const std::string name = hashFn.AlgorithmName();
        size_t outputSize = name.length() + 1 + hashFn.DigestSize() * 2 + 1;
        assert(outputSize <= MAX_LENGTH);

        this->hashFn = &hashFn;
        this->name = name;
        this->outputSize = downCast<uint32_t>(outputSize);
    }

  private:
    CryptoPP::HashTransformation* hashFn;
    std::string name;
    uint32_t outputSize;
    Algorithm(const Algorithm& other) = delete;
    Algorithm& operator=(const Algorithm& other) = delete;
};

/**
 * A container for a set of Algorithm implementations.
 */
namespace Algorithms {

    namespace Internal {

        /**
         * Concrete implementations of Algorithms.
         */
        template<typename HashFn>
        class ConcreteAlgorithm : public Algorithm {
          public:
            ConcreteAlgorithm()
                : concreteHashFn()
            {
                setHashFn(concreteHashFn);
            }
            HashFn concreteHashFn;
        };

        // These comparison functions are used to sort the algorithms
        // and find algorithms by name.

        static bool algoAlgoCmp(const Algorithm* left,
                                const Algorithm* right) {
            return strcmp(left->getName().c_str(),
                          right->getName().c_str()) < 0;
        }

        static bool nameAlgoCmp(const char* left, const Algorithm* right) {
            return strcmp(left, right->getName().c_str()) < 0;
        }
        static bool algoNameCmp(const Algorithm* left, const char* right) {
            return strcmp(left->getName().c_str(), right) < 0;
        }

        /**
         * Each thread keeps a thread-local copy of Algorithm objects, which
         * are reused every time a new checksum is calculated.
         * This should be accessed via get().
         */
        __thread std::vector<Algorithm*>* algorithms;

        /**
         * Get the thread-local table of Algorithm objects.
         * This will construct the table if necessary.
         */
        const std::vector<Algorithm*>&
        get()
        {
            if (algorithms == NULL) {
                // This is a small, harmless memory leak.
                algorithms = new std::vector<Algorithm*> {
                    new ConcreteAlgorithm<CryptoPP::CRC32>(),
                    new ConcreteAlgorithm<CryptoPP::Adler32>(),
                    new ConcreteAlgorithm<CryptoPP::Weak::MD5>(),
                    new ConcreteAlgorithm<CryptoPP::SHA1>(),
                    new ConcreteAlgorithm<CryptoPP::SHA224>(),
                    new ConcreteAlgorithm<CryptoPP::SHA256>(),
                    new ConcreteAlgorithm<CryptoPP::SHA384>(),
                    new ConcreteAlgorithm<CryptoPP::SHA512>(),
                    new ConcreteAlgorithm<CryptoPP::Whirlpool>(),
                    new ConcreteAlgorithm<CryptoPP::Tiger>(),
                    new ConcreteAlgorithm<CryptoPP::RIPEMD160>(),
                    new ConcreteAlgorithm<CryptoPP::RIPEMD320>(),
                    new ConcreteAlgorithm<CryptoPP::RIPEMD128>(),
                    new ConcreteAlgorithm<CryptoPP::RIPEMD256>(),
                };
                std::sort(algorithms->begin(), algorithms->end(),
                          Internal::algoAlgoCmp);
            }
            return *algorithms;
        };
    } // namespace LogCabin::Core::Checksum::<anonymous>::Algorithms::Internal

    /**
     * Find an algorithm by name.
     * \param name
     *      The short name of the algorithm. Must be null-terminated.
     * \return
     *      The Algorithm if found, NULL otherwise.
     */
    Algorithm*
    find(const char* name)
    {
        auto first = std::lower_bound(Internal::get().begin(),
                                      Internal::get().end(),
                                      name,
                                      Internal::algoNameCmp);
        if (first != Internal::get().end() &&
            !Internal::nameAlgoCmp(name, *first)) {
            return *first;
        }
        return NULL;
    }

    /// Return the names of all the algorithms in sorted order.
    std::vector<std::string>
    list()
    {
        std::vector<std::string> names;
        for (auto it = Internal::get().begin();
             it != Internal::get().end();
             ++it) {
            Algorithm* algo = *it;
            names.push_back(algo->getName());
        }
        return names;
    }
} // namespace LogCabin::Core::Checksum::<anonymous>::Algorithms

} // namespace LogCabin::Core::Checksum::<anonymous>

std::vector<std::string>
listAlgorithms()
{
    return Algorithms::list();
}

uint32_t
calculate(const char* algorithm,
          const void* data, uint64_t dataLength,
          char output[MAX_LENGTH])
{
    return calculate(algorithm, {{data, dataLength}}, output);
}

uint32_t
calculate(const char* algorithm,
          std::initializer_list<std::pair<const void*, uint64_t>> data,
          char output[MAX_LENGTH])
{
    Algorithm* algo = Algorithms::find(algorithm);
    if (algo == NULL) {
        PANIC("The hashing algorithm %s is not available",
              algorithm);
    }
    return algo->writeChecksum(data, output);
}


uint32_t
length(const char* checksum,
       uint32_t maxChecksumLength)
{
    uint32_t len = 0;
    while (true) {
        if (len == maxChecksumLength || len == MAX_LENGTH)
            return 0;
        char c = checksum[len];
        ++len;
        if (c == '\0')
            return len;
    }
}

std::string
verify(const char* checksum,
       const void* data, uint64_t dataLength)
{
    return verify(checksum, {{data, dataLength}});
}

std::string
verify(const char* checksum,
       std::initializer_list<std::pair<const void*, uint64_t>> data)
{
    if (!Core::StringUtil::isPrintable(checksum))
        return "The given checksum value is corrupt and not printable.";

    Algorithm* algo;
    { // find algo
        char algorithmName[MAX_LENGTH];
        char* colon = strchr(const_cast<char*>(checksum), ':');
        if (colon == NULL)
            return format("Missing colon in checksum: %s", checksum);

        // nameLength does not include the null terminator
        uint32_t nameLength = downCast<uint32_t>(colon - checksum);
        memcpy(algorithmName, checksum, nameLength);
        algorithmName[nameLength] = '\0';

         algo = Algorithms::find(algorithmName);
         if (algo == NULL)
             return format("No such checksum algorithm: %s",
                                 algorithmName);
    }

    // compare calculated checksum with the one given
    char calculated[MAX_LENGTH];
    algo->writeChecksum(data, calculated);
    if (strcmp(calculated, checksum) != 0) {
        return format("Checksum doesn't match: expected %s "
                            "but calculated %s", checksum, calculated);
    }

    return std::string();
}

} // namespace LogCabin::Core::Checksum
} // namespace LogCabin::Core
} // namespace LogCabin
