#include "store/lru_list.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace miniredis {
namespace {

struct Node {
    int id = 0;
    Node* lru_prev = nullptr;
    Node* lru_next = nullptr;
};

std::vector<int> order(const LruList<Node>& list) {
    std::vector<int> ids;
    for (const Node* node = list.most_recent(); node != nullptr; node = node->lru_next) {
        ids.push_back(node->id);
    }
    return ids;
}

std::vector<int> reverse_order(const LruList<Node>& list) {
    std::vector<int> ids;
    for (const Node* node = list.least_recent(); node != nullptr; node = node->lru_prev) {
        ids.push_back(node->id);
    }
    return ids;
}

class LruListTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
            nodes_.at(static_cast<std::size_t>(i)).id = i;
        }
    }

    Node* node(std::size_t i) { return &nodes_.at(i); }

    std::array<Node, 4> nodes_{};
    LruList<Node> list_;
};

TEST_F(LruListTest, StartsEmpty) {
    EXPECT_TRUE(list_.empty());
    EXPECT_EQ(list_.least_recent(), nullptr);
}

TEST_F(LruListTest, PushFrontMakesNodeMostRecent) {
    list_.push_front(node(0));
    list_.push_front(node(1));
    list_.push_front(node(2));
    EXPECT_EQ(order(list_), (std::vector{2, 1, 0}));
    EXPECT_EQ(reverse_order(list_), (std::vector{0, 1, 2}));
}

TEST_F(LruListTest, RemoveFromHeadMiddleAndTail) {
    for (std::size_t i = 0; i < 4; ++i) {
        list_.push_front(node(i));
    }
    list_.remove(node(2));
    EXPECT_EQ(order(list_), (std::vector{3, 1, 0}));
    list_.remove(node(3));
    EXPECT_EQ(order(list_), (std::vector{1, 0}));
    list_.remove(node(0));
    EXPECT_EQ(order(list_), (std::vector{1}));
    EXPECT_EQ(reverse_order(list_), (std::vector{1}));
    list_.remove(node(1));
    EXPECT_TRUE(list_.empty());
}

TEST_F(LruListTest, MoveToFront) {
    for (std::size_t i = 0; i < 3; ++i) {
        list_.push_front(node(i));
    }
    list_.move_to_front(node(0));
    EXPECT_EQ(order(list_), (std::vector{0, 2, 1}));
    list_.move_to_front(node(0));
    EXPECT_EQ(order(list_), (std::vector{0, 2, 1}));
    EXPECT_EQ(list_.least_recent()->id, 1);
}

}  // namespace
}  // namespace miniredis
