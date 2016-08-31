#define TESTINATOR_MAIN
#include <testinator.h>

#include <algorithm>
#include <ios>
#include <iterator>
#include <numeric>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std;

// -----------------------------------------------------------------------------
// SFINAE member/functionality detection

template <typename...>
using void_t = void;

#define SFINAE_DETECT(name, expr)                                       \
  template <typename T>                                                 \
  using name##_t = decltype(expr);                                      \
  template <typename T, typename = void>                                \
  struct has_##name : public std::false_type {};                        \
  template <typename T>                                                 \
  struct has_##name<T, void_t<name##_t<T>>> : public std::true_type {};

//------------------------------------------------------------------------------

// detect CommonPrefix(string)
SFINAE_DETECT(common_prefix,
              declval<T>().CommonPrefix(std::string()))

namespace {
  template <typename T>
  auto nonce_type(T) { return [](T){}; }
}

// detect AddWords(It, It)
SFINAE_DETECT(add_words,
              declval<T>().AddWords(nonce_type<T>(), nonce_type<T>()))

//------------------------------------------------------------------------------

template <typename Engine>
class Completer
{

public:
  // Add a word to the autocomplete corpus.
  void AddWord(const string& s)
  {
    e.AddWord(s);
  }

  // Add several words to the autocomplete corpus.
  template <typename InputIt>
  void AddWords(InputIt first, InputIt last)
  {
    AddWords(first, last, has_add_words<Engine>{});
  }

  // Add several words to the autocomplete corpus.
  void AddWords(const std::initializer_list<string>& il)
  {
    AddWords(std::cbegin(il), std::cend(il));
  }

  // Get the candidates for autocompletion for a given prefix.
  vector<string> GetCandidates(const string& prefix)
  {
    return e.GetCandidates(prefix);
  }

  // Get the prefix common to all candidates. This can be computed as the common
  // prefix of all the candidates, but may also be more efficiently computed
  // depending on the engine.
  string CommonPrefix(const string& prefix)
  {
    return CommonPrefix(prefix, has_common_prefix<Engine>{});
  }

private:
  template <typename InputIt>
  void AddWords(InputIt first, InputIt last, std::true_type)
  {
    e.AddWords(first, last);
  }

  template <typename InputIt>
  void AddWords(InputIt first, InputIt last, std::false_type)
  {
    while (first != last) {
      e.AddWord(*first++);
    }
  }

  string CommonPrefix(const string& prefix, std::true_type)
  {
    return e.CommonPrefix(prefix);
  }

  string CommonPrefix(const string& prefix, std::false_type)
  {
    auto v = GetCandidates(prefix);
    if (v.empty()) return prefix;
    if (v.size() == 1) return v[0];

    // more than one candidate: accumulate the common prefix
    auto init = std::make_pair(v[0].cbegin(), v[0].cend());
    using P = decltype(init);
    auto p = std::accumulate(v.cbegin() + 1, v.cend(), init,
                             [] (const P& p, const string& s) {
                               auto newp = std::mismatch(p.first, p.second,
                                                         s.cbegin(), s.cend());
                               if (newp.second - newp.first < p.second - p.first)
                                 return newp;
                               return p;
                             });
    return string{p.first, p.second};
  }

  Engine e;
};

//------------------------------------------------------------------------------

#undef SFINAE_DETECT

//------------------------------------------------------------------------------

// A simple vector-based completion engine. Doesn't provide CommonPrefix.
class VectorEngine
{
public:
  void AddWord(const string& s)
  {
    auto i = std::lower_bound(words.cbegin(), words.cend(), s);
    words.insert(i, s);
  }

  template <typename It>
  void AddWords(It first, It last)
  {
    AddWords(first, last, typename std::iterator_traits<It>::iterator_category());
  }

  template <typename RandomIt>
  void AddWords(RandomIt first, RandomIt last, std::random_access_iterator_tag)
  {
    words.reserve(words.size() + last - first);
    AddWords(first, last, std::input_iterator_tag{});
  }

  template <typename InputIt>
  void AddWords(InputIt first, InputIt last, std::input_iterator_tag)
  {
    while (first != last) {
      words.push_back(*first++);
    }
    std::sort(words.begin(), words.end());
  }

  vector<string> GetCandidates(const string& prefix)
  {
    auto i = std::lower_bound(words.cbegin(), words.cend(), prefix);
    vector<string> v;
    auto is_prefix_of = [] (const string& p, const string& w) {
      return std::mismatch(p.cbegin(), p.cend(),
                           w.cbegin(), w.cend()).first == p.cend();
    };
    while (i != words.cend() && is_prefix_of(prefix, *i))
    {
      v.push_back(*i++);
    }
    if (v.empty()) return { prefix };
    return v;
  }

private:
  vector<string> words;
};

// A more complex ternary tree based completion engine. Provides an
// implementation of CommonPrefix.
class TernaryTreeEngine
{
public:
  void AddWord(const string& s)
  {
    Node** n = &tree;
    for (auto it = s.cbegin(); ;)
    {
      if (!*n) {
        *n = new Node(*it);
      }
      if (*it < (*n)->value) {
        n = &((*n)->left);
      } else if (*it == (*n)->value) {
        ++it;
        if (it == s.cend()) {
          (*n)->wordend = true;
          break;
        }
        n = &((*n)->center);
      } else {
        n = &((*n)->right);
      }
    }
  }

  vector<string> GetCandidates(const string& prefix)
  {
    string s;
    const Node* n;
    std::tie(s, n) = TraversePrefix(prefix, tree);
    if (!n) return { prefix };
    return WordsFrom(n, s);
  }

  string CommonPrefix(const string& prefix)
  {
    string s;
    const Node* n;
    std::tie(s, n) = TraversePrefix(prefix, tree);

    // fill in the rest of the unambiguous prefix
    while (n && !n->left && !n->right) {
        s.push_back(n->value);
        if (n->wordend) break;
        n = n->center;
    }

    if (s.size() < prefix.size()) return prefix;
    return s;
  }

  ~TernaryTreeEngine() { delete tree; }

private:
  struct Node
  {
    Node(char c) : value(c) {}
    ~Node() { delete left; delete center; delete right; }
    Node* left = nullptr;
    Node* center = nullptr;
    Node* right = nullptr;
    char value;
    bool wordend = false;
  };

  // Traverse the tree up to the point directed by the prefix. Return the
  // resulting string and the node reached.
  std::pair<string, const Node*> TraversePrefix(const string& prefix, const Node* n)
  {
    string s;
    auto it = prefix.cbegin();
    while (n && it != prefix.cend()) {
      if (*it < n->value) {
        n = n->left;
      } else if (*it == n->value) {
        s.push_back(n->value);
        ++it;
        n = n->center;
      } else {
        n = n->right;
      }
    }
    return {s, n};
  }

  // Given a node and a prefix, return all the words that can result from that
  // starting point.
  vector<string> WordsFrom(const Node* n, string& s)
  {
    auto v = [&, this] () -> vector<string> {
      if (!n->left) return {};
      string sl{s};
      return WordsFrom(n->left, sl);
    }();

    auto vr = [&, this] () -> vector<string> {
      if (!n->right) return {};
      string sr{s};
      return WordsFrom(n->right, sr);
    }();

    if (n->center || n->wordend) s.push_back(n->value);
    if (n->wordend) v.push_back(s);

    auto vc = [&, this] () -> vector<string> {
      if (!n->center) return {};
      return WordsFrom(n->center, s);
    }();

    move(vc.begin(), vc.end(), back_inserter(v));
    move(vr.begin(), vr.end(), back_inserter(v));
    return v;
  }

  Node* tree = nullptr;
};

//------------------------------------------------------------------------------

DEF_TEST(VectorCandidates, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  vector<string> expected = { "cherry", "cherry-pick" };
  auto v = c.GetCandidates("ch");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}

DEF_TEST(VectorAllCandidates, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  vector<string> expected = { "commit" ,"cherry", "cherry-pick" };
  auto v = c.GetCandidates("");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}

DEF_TEST(VectorNoCandidates, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  auto v = c.GetCandidates("push");
  return v.size() == 1 && v[0] == "push";
}

DEF_TEST(VectorPrefix, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  string expected = "cherry";
  auto s = c.CommonPrefix("ch");
  return s == expected;
}

DEF_TEST(VectorNoPrefix, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  auto s = c.CommonPrefix("p");
  return s == "p";
}

DEF_TEST(VectorAddWords, VectorEngine)
{
  Completer<VectorEngine> c;
  c.AddWords({ "cherry", "commit", "cherry-pick" });

  vector<string> expected = { "cherry", "cherry-pick" };
  auto v = c.GetCandidates("ch");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}

DEF_TEST(TernaryTreeCandidates, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  vector<string> expected = { "cherry", "cherry-pick" };
  auto v = c.GetCandidates("ch");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}

DEF_TEST(TernaryTreeAllCandidates, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  vector<string> expected = { "commit" ,"cherry", "cherry-pick" };
  auto v = c.GetCandidates("");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}

DEF_TEST(TernaryTreeNoCandidates, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  auto v = c.GetCandidates("push");
  return v.size() == 1 && v[0] == "push";
}

DEF_TEST(TernaryTreePrefix, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  string expected = "cherry";
  auto s = c.CommonPrefix("ch");
  return s == expected;
}

DEF_TEST(TernaryTreeNoPrefix, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWord("commit");
  c.AddWord("cherry");
  c.AddWord("cherry-pick");

  auto s = c.CommonPrefix("p");
  return s == "p";
}

DEF_TEST(TernaryTreeAddWords, TernaryTreeEngine)
{
  Completer<TernaryTreeEngine> c;
  c.AddWords({ "cherry", "commit", "cherry-pick" });

  vector<string> expected = { "cherry", "cherry-pick" };
  auto v = c.GetCandidates("ch");
  return std::is_permutation(v.cbegin(), v.cend(),
                             expected.cbegin(), expected.cend());
}
