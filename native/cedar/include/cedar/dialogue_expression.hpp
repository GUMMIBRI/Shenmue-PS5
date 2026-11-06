// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by pinned new-yokosuka; see docs/DIALOGUE-EXPRESSION.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cedar {
enum class DialogueOperandKind : std::uint8_t { literal, state_bank, runtime_selector, actor_identity, spatial_result };
struct DialogueOperandRequest {
    DialogueOperandKind kind{DialogueOperandKind::literal};
    std::uint8_t native_value_type{};
    // Bank/actor/spatial index or runtime mode; literal is held separately.
    std::uint16_t index{};
};
struct DialogueExpressionNode {
    // Zero denotes a leaf, 1..12 the source operator numbers.
    std::uint8_t operation{};
    DialogueOperandRequest operand{};
    double literal{};
    std::size_t left{}, right{}, subtree_begin{}, depth{};
};
struct DialogueExpressionValue { bool resolved{}; double value{}; };
using DialogueExpressionProvider = DialogueExpressionValue (*)(void*, const DialogueOperandRequest&) noexcept;
enum class DialogueExpressionError : std::uint8_t {
    none, invalid_buffer, address_overflow, overlapping_storage, offset_out_of_range,
    truncated_operand, unsupported_token, unsupported_operator, stack_underflow,
    empty_expression, unterminated, node_capacity, stack_capacity, resource_limit,
    invalid_view, value_capacity, unsupported_operation, non_finite
};
struct DialogueExpressionResult {
    static constexpr std::size_t no_offset = static_cast<std::size_t>(-1);
    DialogueExpressionError error{DialogueExpressionError::none};
    std::size_t offset{no_offset};
    explicit operator bool() const noexcept { return error == DialogueExpressionError::none; }
};
class DialogueExpressionView {
public:
    static constexpr std::size_t max_nodes = 65536, max_tokens = 131072, max_depth = 1024;
    bool parsed() const noexcept { return nodes_ != nullptr; }
    std::size_t node_count() const noexcept { return count_; }
    std::size_t root_index() const noexcept { return root_; }
    std::size_t remaining_roots() const noexcept { return roots_; }
    std::size_t end_offset() const noexcept { return end_; }
    const DialogueExpressionNode* node(std::size_t i) const noexcept { return i < count_ ? nodes_ + i : nullptr; }
private:
    const DialogueExpressionNode* nodes_{};
    std::size_t count_{}, root_{}, roots_{}, end_{};
    friend DialogueExpressionResult parse_dialogue_expression(const std::uint8_t*, std::size_t,
        std::size_t, DialogueExpressionNode*, std::size_t, std::size_t*, std::size_t,
        DialogueExpressionView&) noexcept;
};

// Start is immediately AFTER F0. Scratch nodes/stack can change on failure;
// output view stays unchanged. Only a successful view is valid for evaluation.
// Full declared spans must be aligned/disjoint from each other/input/view.
DialogueExpressionResult parse_dialogue_expression(const std::uint8_t* bytes, std::size_t size,
    std::size_t start, DialogueExpressionNode* nodes, std::size_t node_capacity,
    std::size_t* stack, std::size_t stack_capacity, DialogueExpressionView& output) noexcept;

struct DialogueExpressionEvaluation {
    DialogueExpressionError error{DialogueExpressionError::none};
    bool resolved{};
    double value{}; // Meaningful only when error==none && resolved.
    std::size_t node_index{DialogueExpressionResult::no_offset};
    explicit operator bool() const noexcept { return error == DialogueExpressionError::none; }
};
// Values are scratch (may change on failure), capacity >= view.node_count().
// Provider is optional/read-only; missing data stays unresolved. Reachable
// assign/increment/decrement fail unsupported without invoking any providers.
// Node storage/view/values/context and provider-owned data stay valid/disjoint
// and immutable during the call (values alone are writable).
DialogueExpressionEvaluation evaluate_dialogue_expression(const DialogueExpressionView& view,
    DialogueExpressionProvider provider, void* context, DialogueExpressionValue* values,
    std::size_t value_capacity) noexcept;
} // namespace cedar
