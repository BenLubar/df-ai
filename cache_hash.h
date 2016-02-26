#pragma once

#include <functional>
#include <map>

// an object similar to a hash, that will evaluate and cache proc for every key
template<typename K, typename V>
class cache_hash
{
    std::function<V(const K &)> proc;
    std::map<K, V> hash;

public:
    cache_hash(std::function<V(const K &)> b) :
        proc(b),
        hash()
    {
    }

    V & operator[](const K & i)
    {
        if (hash.count(i))
        {
            return hash.at(i);
        }
        return hash[i] = proc(i);
    }
};

// vim: et:sw=4:ts=4
