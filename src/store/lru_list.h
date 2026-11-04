#pragma once

namespace miniredis {

// Intrusive doubly linked list ordered from most to least recently used. Nodes provide their
// own `lru_prev`/`lru_next` pointers, so the list never allocates and never owns its nodes.
template <typename Node>
class LruList {
public:
    void push_front(Node* node) noexcept {
        node->lru_prev = nullptr;
        node->lru_next = head_;
        if (head_ != nullptr) {
            head_->lru_prev = node;
        } else {
            tail_ = node;
        }
        head_ = node;
    }

    void remove(Node* node) noexcept {
        if (node->lru_prev != nullptr) {
            node->lru_prev->lru_next = node->lru_next;
        } else {
            head_ = node->lru_next;
        }
        if (node->lru_next != nullptr) {
            node->lru_next->lru_prev = node->lru_prev;
        } else {
            tail_ = node->lru_prev;
        }
        node->lru_prev = nullptr;
        node->lru_next = nullptr;
    }

    void move_to_front(Node* node) noexcept {
        if (node != head_) {
            remove(node);
            push_front(node);
        }
    }

    Node* most_recent() const noexcept { return head_; }
    Node* least_recent() const noexcept { return tail_; }
    bool empty() const noexcept { return head_ == nullptr; }

private:
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
};

}  // namespace miniredis
