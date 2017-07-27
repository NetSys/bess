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
#include <tuple>

using bess::utils::Trie;

namespace {

TEST(TrieTest, Match) {
  Trie<int32_t> trie;

  trie.Insert("Hello world!", 5);
  trie.Insert("123456", 10);

  EXPECT_FALSE(trie.Match("234"));
  EXPECT_FALSE(trie.Match("ello"));
  EXPECT_FALSE(trie.Match("H"));
  EXPECT_FALSE(trie.Match(""));

  EXPECT_TRUE(trie.Match("Hello world!"));
  EXPECT_TRUE(trie.Match("123456"));
}

TEST(TrieTest, MatchPrefix) {
  Trie<int32_t> trie;

  trie.Insert("Hello world!", 5);
  trie.Insert("123456", 10);

  EXPECT_FALSE(trie.MatchPrefix("234"));
  EXPECT_FALSE(trie.MatchPrefix("ello"));

  EXPECT_TRUE(trie.MatchPrefix("H"));
  EXPECT_TRUE(trie.MatchPrefix(""));
  EXPECT_TRUE(trie.MatchPrefix("Hello"));
  EXPECT_TRUE(trie.MatchPrefix("123456"));
}

TEST(TrieTest, Lookup) {
  Trie<int32_t> trie;

  trie.Insert("Hello world!", 5);
  trie.Insert("123456", 10);

  EXPECT_FALSE(trie.Lookup("234").first);
  EXPECT_FALSE(trie.Lookup("ello").first);
  EXPECT_FALSE(trie.Lookup("H").first);
  EXPECT_FALSE(trie.Lookup("").first);

  EXPECT_TRUE(trie.Lookup("Hello world!").first);
  EXPECT_EQ(trie.Lookup("Hello world!").second, 5);
  EXPECT_TRUE(trie.Lookup("123456").first);
  EXPECT_EQ(trie.Lookup("123456").second, 10);
}

TEST(TrieTest, LookupWithEmptyValue) {
  Trie<std::tuple<>> trie;

  trie.Insert("Hello world!", {});
  trie.Insert("123456", {});

  EXPECT_FALSE(trie.Lookup("234").first);
  EXPECT_FALSE(trie.Lookup("ello").first);
  EXPECT_FALSE(trie.Lookup("H").first);
  EXPECT_FALSE(trie.Lookup("").first);

  EXPECT_TRUE(trie.Lookup("Hello world!").first);
  EXPECT_TRUE(trie.Lookup("123456").first);
}

// Check whether prefix keys work, especially in combination with non-prefix
// keys.
TEST(TrieTest, InsertPrefixes) {
  Trie<int32_t> trie;

  trie.Insert("Hel", 1, true);
  trie.Insert("Hello", 2, true);
  trie.Insert("12", 3, true);
  trie.Insert("Hello World", 4, false);

  EXPECT_FALSE(trie.Lookup("He2").first);
  EXPECT_FALSE(trie.Lookup("1").first);
  EXPECT_FALSE(trie.Match("He2"));
  EXPECT_FALSE(trie.Match("1"));

  EXPECT_TRUE(trie.Match("Hel"));
  EXPECT_TRUE(trie.Match("Hell"));
  EXPECT_TRUE(trie.Match("Hello"));
  EXPECT_TRUE(trie.Match("Hello World"));
  EXPECT_TRUE(trie.Match("12"));
  EXPECT_TRUE(trie.Match("123"));
  EXPECT_TRUE(trie.Match("1234"));

  EXPECT_TRUE(trie.Lookup("Hel").first);
  EXPECT_TRUE(trie.Lookup("Hell").first);
  EXPECT_TRUE(trie.Lookup("Hello").first);
  EXPECT_TRUE(trie.Lookup("Hello ").first);

  EXPECT_EQ(trie.Lookup("Hel").second, 1);
  EXPECT_EQ(trie.Lookup("Hell").second, 1);
  EXPECT_EQ(trie.Lookup("Hello").second, 2);
  EXPECT_EQ(trie.Lookup("Hello y'all").second, 2);
  EXPECT_EQ(trie.Lookup("Hello World").second, 4);
  EXPECT_EQ(trie.Lookup("Hello World!!!").second, 2);

  EXPECT_TRUE(trie.Lookup("12").first);
  EXPECT_TRUE(trie.Lookup("123").first);
  EXPECT_TRUE(trie.Lookup("123456").first);

  EXPECT_EQ(trie.Lookup("12").second, 3);
  EXPECT_EQ(trie.Lookup("123").second, 3);
  EXPECT_EQ(trie.Lookup("123456").second, 3);
}

// Whether an empty Trie behaves correctly
TEST(TrieTest, Empty) {
  Trie<int32_t> trie;

  EXPECT_FALSE(trie.Match("234"));
  EXPECT_FALSE(trie.Match("ello"));
  EXPECT_FALSE(trie.Match("Hello"));
  EXPECT_FALSE(trie.Match("H"));

  EXPECT_TRUE(trie.MatchPrefix(""));
  EXPECT_FALSE(trie.MatchPrefix(" "));

  EXPECT_FALSE(trie.Lookup("234").first);
  EXPECT_FALSE(trie.Lookup("ello").first);
  EXPECT_FALSE(trie.Lookup("Hello").first);
  EXPECT_FALSE(trie.Lookup("H").first);
  EXPECT_FALSE(trie.Lookup("").first);
}

// Whether an Trie with the "" prefix behaves correctly
TEST(TrieTest, EmptyPrefix) {
  Trie<int32_t> trie;
  trie.Insert("", 2, true);

  EXPECT_TRUE(trie.Match(""));
  EXPECT_TRUE(trie.Match("234"));
  EXPECT_TRUE(trie.Match("ello"));
  EXPECT_TRUE(trie.Match("Hello"));
  EXPECT_TRUE(trie.Match("H"));
  EXPECT_TRUE(trie.MatchPrefix("Hello"));
  EXPECT_TRUE(trie.MatchPrefix(""));

  EXPECT_TRUE(trie.Lookup("Hello").first);
  EXPECT_EQ(trie.Lookup("Hello").second, 2);

  EXPECT_TRUE(trie.Lookup("H").first);
  EXPECT_EQ(trie.Lookup("H").second, 2);
}

// Test the copy constructor
TEST(TrieTest, Copy) {
  Trie<int32_t> trie0;

  Trie<int32_t> trie1 = trie0;
  EXPECT_FALSE(trie1.Match("Hello"));
  EXPECT_FALSE(trie1.Match("H"));
  EXPECT_FALSE(trie1.Match(""));
  EXPECT_FALSE(trie1.MatchPrefix("234"));
  EXPECT_FALSE(trie1.MatchPrefix("ello"));
  EXPECT_TRUE(trie1.MatchPrefix(""));

  trie0.Insert("Hello world!", 1);
  trie0.Insert("Hello", 2, true);
  trie0.Insert("123456", 3);
  Trie<int32_t> trie2 = trie0;

  EXPECT_FALSE(trie2.Match("234"));
  EXPECT_FALSE(trie2.Match("ello"));

  EXPECT_TRUE(trie2.Match("Hello"));
  EXPECT_TRUE(trie2.Match("Hello y'all"));
  EXPECT_TRUE(trie2.MatchPrefix("H"));
  EXPECT_TRUE(trie2.MatchPrefix(""));
  EXPECT_TRUE(trie2.MatchPrefix("1"));

  EXPECT_TRUE(trie2.Lookup("Hello").first);
  EXPECT_TRUE(trie2.Lookup("Hello y'all").first);
  EXPECT_TRUE(trie2.Lookup("Hello World").first);

  EXPECT_EQ(trie2.Lookup("Hello").second, 2);
  EXPECT_EQ(trie2.Lookup("Hello y'all").second, 2);
  EXPECT_EQ(trie2.Lookup("Hello World").second, 2);
}

}  // namespace (unnamed)
