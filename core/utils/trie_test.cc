// Copyright (c) 2016-2017, Nefeli Networks, Inc.
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

#include "trie.h"

#include <gtest/gtest.h>

using bess::utils::Trie;

namespace {

// Whether Lookup function matches the prefix correctly
TEST(TrieTest, Lookup) {
  Trie trie;

  trie.Insert("Hello world!");
  trie.Insert("123456");

  EXPECT_FALSE(trie.Lookup("234"));
  EXPECT_FALSE(trie.Lookup("ello"));

  EXPECT_TRUE(trie.Lookup("Hello"));
  EXPECT_TRUE(trie.Lookup("H"));
  EXPECT_TRUE(trie.Lookup(""));
  EXPECT_TRUE(trie.Lookup("1"));
  EXPECT_TRUE(trie.Lookup("123456"));
}

// Whether LookupKey function matches the key against prefixes correctly
TEST(TrieTest, LookupKey) {
  Trie trie;

  trie.Insert("Hel");
  trie.Insert("12");

  EXPECT_FALSE(trie.LookupKey("He1"));
  EXPECT_FALSE(trie.LookupKey("1"));

  EXPECT_TRUE(trie.LookupKey("Hel"));
  EXPECT_TRUE(trie.LookupKey("Hello"));
  EXPECT_TRUE(trie.LookupKey("12"));
  EXPECT_TRUE(trie.LookupKey("123"));
  EXPECT_TRUE(trie.LookupKey("123456"));
}

// Whether an empty Trie behaves correctly with LookupKey and Lookup
TEST(TrieTest, Empty) {
  Trie trie;

  EXPECT_FALSE(trie.Lookup("234"));
  EXPECT_FALSE(trie.Lookup("ello"));
  EXPECT_FALSE(trie.Lookup("Hello"));
  EXPECT_FALSE(trie.Lookup("H"));

  EXPECT_TRUE(trie.Lookup(""));

  EXPECT_FALSE(trie.LookupKey("234"));
  EXPECT_FALSE(trie.LookupKey("ello"));
  EXPECT_FALSE(trie.LookupKey("Hello"));
  EXPECT_FALSE(trie.LookupKey("H"));
  EXPECT_FALSE(trie.LookupKey(""));
}

// Test the copy constructor
TEST(TrieTest, Copy) {
  Trie trie0;

  Trie trie1 = trie0;
  EXPECT_FALSE(trie1.Lookup("Hello"));
  EXPECT_FALSE(trie1.Lookup("H"));
  EXPECT_TRUE(trie1.Lookup(""));
  EXPECT_FALSE(trie1.LookupKey("234"));
  EXPECT_FALSE(trie1.LookupKey("ello"));
  EXPECT_FALSE(trie1.LookupKey(""));

  trie0.Insert("Hello world!");
  trie0.Insert("123456");
  Trie trie2 = trie0;

  EXPECT_FALSE(trie2.Lookup("234"));
  EXPECT_FALSE(trie2.Lookup("ello"));
  EXPECT_TRUE(trie2.Lookup("Hello"));
  EXPECT_TRUE(trie2.Lookup("H"));
  EXPECT_TRUE(trie2.Lookup(""));
  EXPECT_TRUE(trie2.Lookup("1"));
  EXPECT_TRUE(trie2.Lookup("123456"));
  EXPECT_FALSE(trie2.LookupKey("Hello"));
  EXPECT_FALSE(trie2.LookupKey("1"));
  EXPECT_TRUE(trie2.LookupKey("Hello world!"));
  EXPECT_TRUE(trie2.LookupKey("123456"));
  EXPECT_TRUE(trie2.LookupKey("1234567"));
}

}  // namespace (unnamed)
